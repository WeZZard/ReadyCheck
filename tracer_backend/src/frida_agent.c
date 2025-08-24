#include "ring_buffer.h"
#include "shared_memory.h"
#include "tracer_types.h"
#include <frida-gum.h>
#include <stdint.h>

// Define SHM constants if not in header
#ifndef SHM_ROLE_GUEST
#define SHM_ROLE_GUEST 1
#define SHM_ROLE_HOST 0
#endif

#ifndef SHM_LANE_INDEX
#define SHM_LANE_INDEX 0
#define SHM_LANE_DETAIL 1
#define SHM_LANE_CONTROL 2
#endif
#include <ctype.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach_init.h>
#include <mach/thread_info.h>
#include <pthread/pthread.h>
#endif

// Hash function for stable function IDs
static uint32_t hash_string(const char *str) {
  uint32_t hash = 5381;
  int c;
  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + c; // hash * 33 + c
  }
  return hash;
}

typedef struct {
  GumInterceptor *interceptor;
  SharedMemoryRef shm_index;
  SharedMemoryRef shm_detail;
  SharedMemoryRef shm_control;
  RingBuffer *index_ring;
  RingBuffer *detail_ring;
  ControlBlock *control_block;
  guint num_hooks_attempted;
  guint num_hooks_successful;
  guint64 module_base;
  guint32 host_pid;
  guint32 session_id;

  // Statistics
  guint64 reentrancy_blocked_count;
  guint64 events_emitted_count;
  guint64 stack_capture_failures;
} AgentContext;

typedef struct {
  AgentContext *ctx;
  guint32 function_id; // Now using stable hash-based ID
  const char *function_name;
  GumAddress function_address;
} HookData;

typedef struct {
  char *name;
  GumAddress address;
  guint32 id;
  gboolean success;
} HookResult;

static pthread_key_t g_tls_key;
static pthread_once_t g_tls_once = PTHREAD_ONCE_INIT;

// TLS for reentrancy guard with enhanced tracking
typedef struct {
  guint32 thread_id;
  guint32 call_depth;
  gboolean in_handler;
  guint64 reentrancy_attempts;
} ThreadLocalData;

static void tls_destructor(void *data) {
  if (data) {
    free(data);
  }
}

static void init_tls_key(void) {
  pthread_key_create(&g_tls_key, tls_destructor);
}

static ThreadLocalData *get_thread_local(void) {
  pthread_once(&g_tls_once, init_tls_key);

  ThreadLocalData *tls = pthread_getspecific(g_tls_key);
  if (!tls) {
    tls = calloc(1, sizeof(ThreadLocalData));
    if (!tls)
      return NULL;

#ifdef __APPLE__
    tls->thread_id = pthread_mach_thread_np(pthread_self());
#else
    tls->thread_id = (uint32_t)(uintptr_t)pthread_self();
#endif
    pthread_setspecific(g_tls_key, tls);
  }
  return tls;
}

static uint64_t platform_get_timestamp(void) { return mach_absolute_time(); }

// Signal handler for safe stack capture
static volatile sig_atomic_t segfault_occurred = 0;

static void segfault_handler(int sig) { segfault_occurred = 1; }

// Safe stack capture with boundary checking
static size_t safe_stack_capture(void *dest, void *stack_ptr, size_t max_size) {
  if (!dest || !stack_ptr)
    return 0;

  struct sigaction old_sa, new_sa;

  new_sa.sa_handler = segfault_handler;
  sigemptyset(&new_sa.sa_mask);
  new_sa.sa_flags = 0;

  // Install temporary signal handler
  sigaction(SIGSEGV, &new_sa, &old_sa);
  segfault_occurred = 0;

  size_t copied = 0;

  // Try to copy in chunks to detect boundaries
  const size_t chunk_size = 16;
  for (size_t offset = 0; offset < max_size && !segfault_occurred;
       offset += chunk_size) {
    size_t to_copy =
        (offset + chunk_size <= max_size) ? chunk_size : (max_size - offset);

    // Use volatile to prevent optimization
    volatile char test_read = *((char *)stack_ptr + offset);
    (void)test_read;

    if (!segfault_occurred) {
      memcpy((char *)dest + offset, (char *)stack_ptr + offset, to_copy);
      copied += to_copy;
    }
  }

  // Restore original handler
  sigaction(SIGSEGV, &old_sa, NULL);

  return copied;
}

// Enhanced on_enter with full ABI capture
static void on_enter(GumInvocationContext *ic, gpointer user_data) {
  HookData *hook = (HookData *)user_data;
  AgentContext *ctx = hook->ctx;
  ThreadLocalData *tls = get_thread_local();
  if (!tls)
    return;

  // Enhanced reentrancy guard with metrics
  if (tls->in_handler) {
    tls->reentrancy_attempts++;
    g_atomic_int_inc((gint *)&ctx->reentrancy_blocked_count);
    return;
  }
  tls->in_handler = TRUE;

  g_debug("[Agent] on_enter: %s\n", hook->function_name);

  // Increment call depth
  tls->call_depth++;

  uint64_t timestamp = platform_get_timestamp();

  // Capture index event (always when recording)
  if (ctx->control_block->index_lane_enabled) {
    g_debug("[Agent] Capturing index event\n");
    IndexEvent event = {.timestamp = platform_get_timestamp(),
                        .function_id = hook->function_id,
                        .thread_id = tls->thread_id,
                        .event_kind = EVENT_KIND_CALL,
                        .call_depth = tls->call_depth,
                        ._padding = 0};

    if (ring_buffer_write(ctx->index_ring, &event)) {
      g_debug("[Agent] Wrote index event\n");
      g_atomic_int_inc((gint *)&ctx->events_emitted_count);
    } else {
      g_debug("[Agent] Failed to write index event\n");
    }
  } else {
    g_debug("[Agent] Index lane disabled\n");
  }

  // Capture detail event with full ABI registers and optional stack
  if (ctx->control_block->detail_lane_enabled) {
    g_debug("[Agent] Capturing detail event\n");
    DetailEvent detail = {0};
    detail.timestamp = platform_get_timestamp();
    detail.function_id = hook->function_id;
    detail.thread_id = tls->thread_id;
    detail.event_kind = EVENT_KIND_CALL;
    detail.call_depth = tls->call_depth;

    GumCpuContext *cpu = ic->cpu_context;
    if (cpu) {
#ifdef __aarch64__
      // Capture ARM64 ABI registers
      for (int i = 0; i < 8; i++) {
        detail.x_regs[i] = cpu->x[i]; // x0-x7: arguments
      }
      // Store additional registers separately
      // x_regs only has 8 slots for arguments

      detail.lr = cpu->lr;
      detail.fp = cpu->fp;
      detail.sp = cpu->sp;
#elif defined(__x86_64__)
      // Capture x86_64 ABI registers
      detail.x_regs[0] = cpu->rdi; // arg1
      detail.x_regs[1] = cpu->rsi; // arg2
      detail.x_regs[2] = cpu->rdx; // arg3
      detail.x_regs[3] = cpu->rcx; // arg4
      detail.x_regs[4] = cpu->r8;  // arg5
      detail.x_regs[5] = cpu->r9;  // arg6
      detail.x_regs[6] = cpu->rbp; // frame pointer
      detail.x_regs[7] = cpu->rsp; // stack pointer

      detail.sp = cpu->rsp;
      detail.fp = cpu->rbp;
#endif

      // Optional stack window capture (128 bytes)
      if (ctx->control_block->capture_stack_snapshot) {
        void *stack_ptr = (void *)detail.sp;
        size_t captured = safe_stack_capture(detail.stack_snapshot, stack_ptr,
                                             sizeof(detail.stack_snapshot));
        detail.stack_size = captured;

        if (captured == 0) {
          g_atomic_int_inc((gint *)&ctx->stack_capture_failures);
        }
      }
    }

    if (ring_buffer_write(ctx->detail_ring, &detail)) {
      g_debug("[Agent] Wrote detail event\n");
      g_atomic_int_inc((gint *)&ctx->events_emitted_count);
    } else {
      g_debug("[Agent] Failed to write detail event\n");
    }
  } else {
    g_debug("[Agent] Detail lane disabled\n");
  }

  tls->in_handler = FALSE;
}

// Enhanced on_leave with return value capture
static void on_leave(GumInvocationContext *ic, gpointer user_data) {
  HookData *hook = (HookData *)user_data;
  AgentContext *ctx = hook->ctx;
  ThreadLocalData *tls = get_thread_local();
  if (!tls)
    return;

  // Reentrancy guard
  if (tls->in_handler) {
    tls->reentrancy_attempts++;
    g_atomic_int_inc((gint *)&ctx->reentrancy_blocked_count);
    return;
  }
  tls->in_handler = TRUE;

  g_debug("[Agent] on_leave: %s\n", hook->function_name);

  // Capture index event
  if (ctx->control_block->index_lane_enabled) {
    IndexEvent event = {.timestamp = platform_get_timestamp(),
                        .function_id = hook->function_id,
                        .thread_id = tls->thread_id,
                        .event_kind = EVENT_KIND_RETURN,
                        .call_depth = tls->call_depth,
                        ._padding = 0};

    if (ring_buffer_write(ctx->index_ring, &event)) {
      g_atomic_int_inc((gint *)&ctx->events_emitted_count);
    }
  }

  // Capture detail event with return value
  if (ctx->control_block->detail_lane_enabled &&
      ctx->control_block->flight_state == FLIGHT_RECORDER_RECORDING) {

    DetailEvent detail = {0};
    detail.timestamp = platform_get_timestamp();
    detail.function_id = hook->function_id;
    detail.thread_id = tls->thread_id;
    detail.event_kind = EVENT_KIND_RETURN;
    detail.call_depth = tls->call_depth;

    GumCpuContext *cpu = ic->cpu_context;
    if (cpu) {
#ifdef __aarch64__
      detail.x_regs[0] = cpu->x[0]; // Return value
      detail.sp = cpu->sp;
#elif defined(__x86_64__)
      detail.x_regs[0] = cpu->rax; // Return value
      detail.sp = cpu->rsp;
#endif
    }

    if (ring_buffer_write(ctx->detail_ring, &detail)) {
      g_atomic_int_inc((gint *)&ctx->events_emitted_count);
    }
  }

  // Decrement call depth
  if (tls->call_depth > 0) {
    tls->call_depth--;
  }

  tls->in_handler = FALSE;
}

// Send hook summary to controller via Frida messaging
static void send_hook_summary(AgentContext *ctx, HookResult *results,
                              size_t count) {
  // For now, just print the summary
  // TODO: Implement proper Frida messaging when API is available
  g_debug("[Agent] Hook Summary: attempted=%u, successful=%u, failed=%u\n",
          ctx->num_hooks_attempted, ctx->num_hooks_successful,
          ctx->num_hooks_attempted - ctx->num_hooks_successful);

  for (size_t i = 0; i < count; i++) {
    g_debug("[Agent]   %s: address=0x%llx, id=%u, %s\n", results[i].name,
            (unsigned long long)results[i].address, results[i].id,
            results[i].success ? "hooked" : "failed");
  }
}

// Forward declarations
static uint32_t g_host_pid = UINT32_MAX;
static uint32_t g_session_id = UINT32_MAX;
static AgentContext *g_ctx = NULL;

// Get singleton agent context using GLib once initialization
AgentContext *get_shared_agent_context() {
  static gsize ctx_initialized = 0;
  if (g_once_init_enter(&ctx_initialized)) {
    uint32_t session_id = g_session_id;
    uint32_t host_pid = g_host_pid;

    if (session_id == UINT32_MAX) {
      g_debug("No session id provided, trying environment\n");
      const char *sid_env = getenv("ADA_SHM_SESSION_ID");
      if (sid_env && sid_env[0] != '\0') {
        session_id = (uint32_t)strtoul(sid_env, NULL, 16);
      }
    } else {
      g_debug("Using provided session id: %u\n", session_id);
    }

    if (host_pid == UINT32_MAX) {
      g_debug("No host pid provided, trying environment\n");
      const char *host_env = getenv("ADA_SHM_HOST_PID");
      if (host_env && host_env[0] != '\0') {
        host_pid = (uint32_t)strtoul(host_env, NULL, 10);
      }
    } else {
      g_debug("Using provided host pid: %u\n", host_pid);
    }

    gboolean is_ready = TRUE;

    if (session_id == UINT32_MAX) {
      g_debug("Failed to resolve session id from environment\n");
      is_ready = FALSE;
    }

    if (host_pid == UINT32_MAX) {
      g_debug("Failed to resolve host pid from environment\n");
      is_ready = FALSE;
    }

    if (is_ready) {
      g_ctx = calloc(1, sizeof(AgentContext));
      if (g_ctx) {
        g_ctx->host_pid = host_pid;
        g_ctx->session_id = session_id;
        g_debug("Agent context initialized with host pid: %u, session id: %u\n",
                host_pid, session_id);
      } else {
        g_debug("Failed to allocate agent context\n");
      }
    } else {
      g_debug("Failed to initialize agent context: session id or host pid not "
              "provided nor can be resolved from environment\n");
      g_ctx = NULL;
    }

    g_once_init_leave(&ctx_initialized, (gsize)g_ctx);
  }
  return g_ctx;
}

// Parse initialization payload
// Payload parser for agent_init data: accepts text like
// "host_pid=1234;session_id=89abcdef" Also accepts keys: pid/host_pid and
// sid/session_id; values may be decimal or hex (0x... or plain hex)
static void parse_init_payload(const gchar *data, gint data_size,
                               uint32_t *out_host_pid,
                               uint32_t *out_session_id) {
  if (!data || data_size <= 0)
    return;
  // Copy to a null-terminated buffer
  char buf[256];
  size_t copy_len =
      (size_t)data_size < sizeof(buf) - 1 ? (size_t)data_size : sizeof(buf) - 1;
  memcpy(buf, data, copy_len);
  buf[copy_len] = '\0';

  // Normalize separators to spaces
  for (size_t i = 0; i < copy_len; i++) {
    if (buf[i] == ';' || buf[i] == ',' || buf[i] == '\n' || buf[i] == '\r' ||
        buf[i] == '\t') {
      buf[i] = ' ';
    }
  }

  // Tokenize by spaces
  char *saveptr = NULL;
  for (char *tok = strtok_r(buf, " ", &saveptr); tok != NULL;
       tok = strtok_r(NULL, " ", &saveptr)) {
    char *eq = strchr(tok, '=');
    if (!eq)
      continue;
    *eq = '\0';
    const char *key = tok;
    const char *val = eq + 1;
    if (val[0] == '\0')
      continue;

    if (strcmp(key, "host_pid") == 0 || strcmp(key, "pid") == 0) {
      *out_host_pid = (uint32_t)strtoul(val, NULL, 0);
    } else if (strcmp(key, "session_id") == 0 || strcmp(key, "sid") == 0) {
      // support raw hex without 0x prefix
      unsigned int parsed = UINT32_MAX; // UINT32_MAX means not found.
      if (strncmp(val, "0x", 2) == 0 || strncmp(val, "0X", 2) == 0) {
        parsed = (unsigned int)strtoul(val, NULL, 16);
      } else {
        // detect hex if contains [a-fA-F]
        gboolean looks_hex = FALSE;
        for (const char *p = val; *p; p++) {
          if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
            looks_hex = TRUE;
            break;
          }
        }
        parsed = (unsigned int)strtoul(val, NULL, looks_hex ? 16 : 10);
      }
      *out_session_id = (uint32_t)parsed;
    }
  }
}

// Agent initialization
__attribute__((visibility("default")))
void agent_init(const gchar *data, gint data_size) {
  // Initialize GUM first before using any GLib functions
  gum_init_embedded();

  g_debug("[Agent] Initializing with data size: %d\n", data_size);

  // Parse initialization data
  uint32_t arg_host = 0, arg_sid = 0;
  parse_init_payload(data, data_size, &arg_host, &arg_sid);
  g_host_pid = arg_host;
  g_session_id = arg_sid;

  g_debug("[Agent] Parsed host_pid=%u, session_id=%u\n", g_host_pid,
          g_session_id);

  // Get singleton context (thread-safe, initialized once)
  AgentContext *ctx = get_shared_agent_context();

  if (ctx == NULL) {
    g_debug("[Agent] Failed to allocate agent context\n");
    return;
  }

  // Create TLS key
  pthread_key_create(&g_tls_key, tls_destructor);

  // Open shared memory segments
  g_debug("[Agent] Opening shared memory segments...\n");
  ctx->shm_control = shared_memory_open_unique(ADA_ROLE_CONTROL, ctx->host_pid,
                                               ctx->session_id, 4096);
  ctx->shm_index = shared_memory_open_unique(ADA_ROLE_INDEX, ctx->host_pid,
                                             ctx->session_id, 32 * 1024 * 1024);
  ctx->shm_detail = shared_memory_open_unique(
      ADA_ROLE_DETAIL, ctx->host_pid, ctx->session_id, 32 * 1024 * 1024);

  if (!ctx->shm_control || !ctx->shm_index || !ctx->shm_detail) {
    g_debug("[Agent] Failed to open shared memory (control=%p, index=%p, "
            "detail=%p)\n",
            ctx->shm_control, ctx->shm_index, ctx->shm_detail);
    return;
  }

  g_debug("[Agent] Successfully opened all shared memory segments\n");

  // Map control block
  ctx->control_block =
      (ControlBlock *)shared_memory_get_address(ctx->shm_control);
  g_debug("[Agent] Control block mapped at %p\n", ctx->control_block);

  // Attach to existing ring buffers (already initialized by controller)
  void *index_addr = shared_memory_get_address(ctx->shm_index);
  void *detail_addr = shared_memory_get_address(ctx->shm_detail);
  g_debug("[Agent] Ring buffer addresses: index=%p, detail=%p\n", index_addr,
          detail_addr);

  ctx->index_ring =
      ring_buffer_attach(index_addr, 32 * 1024 * 1024, sizeof(IndexEvent));
  g_debug("[Agent] Index ring attached: %p\n", ctx->index_ring);

  ctx->detail_ring =
      ring_buffer_attach(detail_addr, 32 * 1024 * 1024, sizeof(DetailEvent));
  g_debug("[Agent] Detail ring attached: %p\n", ctx->detail_ring);

  // Get interceptor
  ctx->interceptor = gum_interceptor_obtain();
  g_debug("[Agent] Got interceptor: %p\n", ctx->interceptor);

  // Begin transaction for batch hooking
  gum_interceptor_begin_transaction(ctx->interceptor);
  g_debug("[Agent] Beginning hook installation...\n");

  // Define functions to hook
  const char *functions_to_hook[] = {
      // test_cli functions
      "fibonacci", "process_file", "calculate_pi", "recursive_function",
      // test_runloop functions
      "simulate_network", "monitor_file", "dispatch_work", "signal_handler",
      "timer_callback", NULL};

  // Track results for summary
  HookResult results[20];
  size_t result_count = 0;

  // Hook functions with stable IDs
  for (const char **fname = functions_to_hook; *fname != NULL; fname++) {
    g_debug("[Agent] Finding symbol: %s\n", *fname);
    ctx->num_hooks_attempted++;

    // Generate stable function ID from name
    uint32_t function_id = hash_string(*fname);

    // Find function address
    GumModule* main_module = gum_process_get_main_module();
    GumAddress func_addr = gum_module_find_symbol_by_name(main_module, *fname);

    HookResult *result = &results[result_count++];
    result->name = g_strdup(*fname);
    result->address = func_addr;
    result->id = function_id;
    result->success = (func_addr != 0);

    if (func_addr != 0) {
      g_debug("[Agent] Found symbol: %s at 0x%llx\n", *fname, func_addr);
      HookData *hook = g_new0(HookData, 1);
      hook->ctx = ctx;
      hook->function_id = function_id;
      hook->function_name = g_strdup(*fname);
      hook->function_address = func_addr;

      GumInvocationListener *listener =
          gum_make_call_listener(on_enter, on_leave, hook, g_free);

      gum_interceptor_attach(g_ctx->interceptor, GSIZE_TO_POINTER(func_addr),
                             listener, hook, GUM_ATTACH_FLAGS_NONE);

      ctx->num_hooks_successful++;
      g_debug("[Agent] Hooked: %s at 0x%llx (id=%u)\n", *fname,
              (unsigned long long)func_addr, function_id);
    } else {
      g_debug("[Agent] Failed to find: %s\n", *fname);
    }
  }

  // End transaction
  gum_interceptor_end_transaction(ctx->interceptor);

  // Send hook summary to controller
  send_hook_summary(ctx, results, result_count);

  // Free result names
  for (size_t i = 0; i < result_count; i++) {
    g_free((char *)results[i].name);
  }

  g_debug("[Agent] Initialization complete: %u/%u hooks installed\n",
          ctx->num_hooks_successful, ctx->num_hooks_attempted);
}

// Agent cleanup
__attribute__((destructor)) G_GNUC_INTERNAL void agent_deinit(void) {
  if (!g_ctx)
    return;

  g_debug(
      "[Agent] Shutting down (emitted=%llu events, blocked=%llu reentrancy)\n",
      (unsigned long long)g_ctx->events_emitted_count,
      (unsigned long long)g_ctx->reentrancy_blocked_count);

  // Print final statistics
  g_debug("[Agent] Final stats: events_emitted=%llu, reentrancy_blocked=%llu, "
          "stack_failures=%llu\n",
          (unsigned long long)g_ctx->events_emitted_count,
          (unsigned long long)g_ctx->reentrancy_blocked_count,
          (unsigned long long)g_ctx->stack_capture_failures);

  // Detach all hooks
  if (g_ctx->interceptor) {
    // Note: In production, we'd need to track listeners and detach individually
    // For POC, just unref the interceptor
    g_object_unref(g_ctx->interceptor);
  }

  // Cleanup ring buffers
  ring_buffer_destroy(g_ctx->index_ring);
  ring_buffer_destroy(g_ctx->detail_ring);

  // Cleanup shared memory
  shared_memory_destroy(g_ctx->shm_control);
  shared_memory_destroy(g_ctx->shm_index);
  shared_memory_destroy(g_ctx->shm_detail);

  free(g_ctx);
  g_ctx = NULL;

  pthread_key_delete(g_tls_key);

  gum_deinit_embedded();
}