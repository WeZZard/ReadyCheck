#include "frida_controller.h"
#include "shared_memory.h"
#include "ring_buffer.h"
#include <frida-core.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>

#ifdef __APPLE__
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif

#define INDEX_LANE_SIZE (32 * 1024 * 1024)  // 32MB
#define DETAIL_LANE_SIZE (32 * 1024 * 1024) // 32MB
#define CONTROL_BLOCK_SIZE 4096

struct FridaController {
    // Frida objects
    FridaDeviceManager* manager;
    FridaDevice* device;
    FridaSession* session;
    FridaScript* script;
    
    // Process management
    guint pid;
    ProcessState state;
    
    // Shared memory
    SharedMemory* shm_control;
    SharedMemory* shm_index;
    SharedMemory* shm_detail;
    ControlBlock* control_block;
    
    // Ring buffers
    RingBuffer* index_ring;
    RingBuffer* detail_ring;
    
    // Drain thread
    pthread_t drain_thread;
    bool drain_running;
    
    // Output
    char output_dir[256];
    FILE* output_file;
    
    // Statistics
    TracerStats stats;
    
    // Event loop
    GMainLoop* main_loop;
};

// Forward declarations
static void* drain_thread_func(void* arg);
static void on_detached(FridaSession* session, FridaSessionDetachReason reason, 
                        FridaCrash* crash, gpointer user_data);
static void on_message(FridaScript* script, const gchar* message, 
                       GBytes* data, gpointer user_data);

FridaController* frida_controller_create(const char* output_dir) {
    FridaController* controller = calloc(1, sizeof(FridaController));
    if (!controller) return NULL;
    
    // Initialize Frida
    frida_init();
    
    // Copy output directory
    strncpy(controller->output_dir, output_dir, sizeof(controller->output_dir) - 1);
    
    // Create device manager
    controller->manager = frida_device_manager_new();
    
    // Get local device
    GError* error = NULL;
    FridaDeviceList* devices = frida_device_manager_enumerate_devices_sync(
        controller->manager, NULL, &error);
    
    if (error) {
        g_error_free(error);
        frida_device_manager_close_sync(controller->manager, NULL, NULL);
        frida_unref(controller->manager);
        free(controller);
        return NULL;
    }
    
    // Find local device
    gint num_devices = frida_device_list_size(devices);
    for (gint i = 0; i < num_devices; i++) {
        FridaDevice* device = frida_device_list_get(devices, i);
        if (frida_device_get_dtype(device) == FRIDA_DEVICE_TYPE_LOCAL) {
            controller->device = g_object_ref(device);
        }
        g_object_unref(device);
    }
    
    frida_unref(devices);
    
    if (!controller->device) {
        frida_device_manager_close_sync(controller->manager, NULL, NULL);
        frida_unref(controller->manager);
        free(controller);
        return NULL;
    }
    
    // Create shared memory segments
    controller->shm_control = shared_memory_create("ada_control", CONTROL_BLOCK_SIZE);
    controller->shm_index = shared_memory_create("ada_index", INDEX_LANE_SIZE);
    controller->shm_detail = shared_memory_create("ada_detail", DETAIL_LANE_SIZE);
    
    if (!controller->shm_control || !controller->shm_index || !controller->shm_detail) {
        frida_controller_destroy(controller);
        return NULL;
    }
    
    // Initialize control block
    controller->control_block = (ControlBlock*)controller->shm_control->address;
    controller->control_block->process_state = PROCESS_STATE_INITIALIZED;
    controller->control_block->flight_state = FLIGHT_RECORDER_IDLE;
    controller->control_block->index_lane_enabled = 1;
    controller->control_block->detail_lane_enabled = 0;
    controller->control_block->pre_roll_ms = 1000;
    controller->control_block->post_roll_ms = 1000;
    
    // Create ring buffers
    controller->index_ring = ring_buffer_create(controller->shm_index->address,
                                                INDEX_LANE_SIZE,
                                                sizeof(IndexEvent));
    controller->detail_ring = ring_buffer_create(controller->shm_detail->address,
                                                 DETAIL_LANE_SIZE,
                                                 sizeof(DetailEvent));
    
    // Create event loop
    controller->main_loop = g_main_loop_new(NULL, FALSE);
    
    // Start drain thread
    controller->drain_running = true;
    pthread_create(&controller->drain_thread, NULL, drain_thread_func, controller);
    
    controller->state = PROCESS_STATE_INITIALIZED;
    
    return controller;
}

void frida_controller_destroy(FridaController* controller) {
    if (!controller) return;
    
    // Stop drain thread
    controller->drain_running = false;
    if (controller->drain_thread) {
        pthread_join(controller->drain_thread, NULL);
    }
    
    // Cleanup Frida objects
    if (controller->script) {
        frida_script_unload_sync(controller->script, NULL, NULL);
        frida_unref(controller->script);
    }
    
    if (controller->session) {
        frida_session_detach_sync(controller->session, NULL, NULL);
        frida_unref(controller->session);
    }
    
    if (controller->device) {
        frida_unref(controller->device);
    }
    
    if (controller->manager) {
        frida_device_manager_close_sync(controller->manager, NULL, NULL);
        frida_unref(controller->manager);
    }
    
    // Cleanup ring buffers
    ring_buffer_destroy(controller->index_ring);
    ring_buffer_destroy(controller->detail_ring);
    
    // Cleanup shared memory
    shared_memory_destroy(controller->shm_control);
    shared_memory_destroy(controller->shm_index);
    shared_memory_destroy(controller->shm_detail);
    
    // Close output file
    if (controller->output_file) {
        fclose(controller->output_file);
    }
    
    // Cleanup event loop
    if (controller->main_loop) {
        g_main_loop_unref(controller->main_loop);
    }
    
    free(controller);
    
    frida_deinit();
}

int frida_controller_spawn_suspended(FridaController* controller,
                                     const char* path,
                                     char* const argv[],
                                     uint32_t* out_pid) {
    if (!controller || !path) return -1;
    
    controller->state = PROCESS_STATE_SPAWNING;
    controller->control_block->process_state = PROCESS_STATE_SPAWNING;
    
    // For mock tracees, use posix_spawn with suspend flag
    // For system binaries, use Frida spawn
    bool is_mock = (strstr(path, "test") != NULL || strstr(path, "mock") != NULL);
    
    if (is_mock) {
        // Use posix_spawn for test programs
        pid_t pid;
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        
        #ifdef __APPLE__
        // macOS-specific: start suspended
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_START_SUSPENDED);
        #endif
        
        int result = posix_spawn(&pid, path, NULL, &attr, argv, environ);
        posix_spawnattr_destroy(&attr);
        
        if (result == 0) {
            controller->pid = pid;
            *out_pid = pid;
            controller->state = PROCESS_STATE_SUSPENDED;
            controller->control_block->process_state = PROCESS_STATE_SUSPENDED;
            return 0;
        }
        return -1;
    } else {
        // Use Frida for system binaries
        GError* error = NULL;
        FridaSpawnOptions* options = frida_spawn_options_new();
        
        // Build argv array - count the arguments
        gint argv_len = 0;
        if (argv) {
            while (argv[argv_len]) argv_len++;
        }
        frida_spawn_options_set_argv(options, (gchar**)argv, argv_len);
        
        guint pid = frida_device_spawn_sync(controller->device, path, options, 
                                           NULL, &error);
        g_object_unref(options);
        
        if (error) {
            g_printerr("Failed to spawn: %s\n", error->message);
            g_error_free(error);
            return -1;
        }
        
        controller->pid = pid;
        *out_pid = pid;
        controller->state = PROCESS_STATE_SUSPENDED;
        controller->control_block->process_state = PROCESS_STATE_SUSPENDED;
        return 0;
    }
}

int frida_controller_attach(FridaController* controller, uint32_t pid) {
    if (!controller) return -1;
    
    controller->state = PROCESS_STATE_ATTACHING;
    controller->control_block->process_state = PROCESS_STATE_ATTACHING;
    
    GError* error = NULL;
    FridaSessionOptions* options = frida_session_options_new();
    
    controller->session = frida_device_attach_sync(controller->device, pid, 
                                                   options, NULL, &error);
    
    g_object_unref(options);
    
    if (error) {
        g_printerr("Failed to attach: %s\n", error->message);
        g_error_free(error);
        controller->state = PROCESS_STATE_FAILED;
        controller->control_block->process_state = PROCESS_STATE_FAILED;
        return -1;
    }
    
    // Connect detached signal
    g_signal_connect(controller->session, "detached", 
                     G_CALLBACK(on_detached), controller);
    
    controller->pid = pid;
    controller->state = PROCESS_STATE_ATTACHED;
    controller->control_block->process_state = PROCESS_STATE_ATTACHED;
    
    return 0;
}

int frida_controller_install_hooks(FridaController* controller) {
    if (!controller || !controller->session) return -1;
    
    // Create simple hook script that intercepts all exports
    const char* script_source = 
        "const indexBuf = new SharedMemoryBuffer('/ada_index');\n"
        "const detailBuf = new SharedMemoryBuffer('/ada_detail');\n"
        "\n"
        "let functionId = 0;\n"
        "const functions = new Map();\n"
        "\n"
        "Process.enumerateModules().forEach(module => {\n"
        "  module.enumerateExports().forEach(exp => {\n"
        "    if (exp.type === 'function') {\n"
        "      const id = functionId++;\n"
        "      functions.set(exp.address, id);\n"
        "      \n"
        "      Interceptor.attach(exp.address, {\n"
        "        onEnter(args) {\n"
        "          const event = {\n"
        "            timestamp: Date.now(),\n"
        "            functionId: id,\n"
        "            threadId: Process.getCurrentThreadId(),\n"
        "            eventKind: 1, // CALL\n"
        "            callDepth: 0\n"
        "          };\n"
        "          indexBuf.write(event);\n"
        "        },\n"
        "        onLeave(retval) {\n"
        "          const event = {\n"
        "            timestamp: Date.now(),\n"
        "            functionId: id,\n"
        "            threadId: Process.getCurrentThreadId(),\n"
        "            eventKind: 2, // RETURN\n"
        "            callDepth: 0\n"
        "          };\n"
        "          indexBuf.write(event);\n"
        "        }\n"
        "      });\n"
        "    }\n"
        "  });\n"
        "});\n"
        "\n"
        "console.log('Hooks installed on ' + functionId + ' functions');\n";
    
    GError* error = NULL;
    FridaScriptOptions* options = frida_script_options_new();
    frida_script_options_set_name(options, "tracer");
    frida_script_options_set_runtime(options, FRIDA_SCRIPT_RUNTIME_QJS);
    
    controller->script = frida_session_create_script_sync(controller->session,
                                                          script_source,
                                                          options, NULL, &error);
    
    g_object_unref(options);
    
    if (error) {
        g_printerr("Failed to create script: %s\n", error->message);
        g_error_free(error);
        return -1;
    }
    
    // Connect message handler
    g_signal_connect(controller->script, "message", 
                     G_CALLBACK(on_message), controller);
    
    // Load script
    frida_script_load_sync(controller->script, NULL, &error);
    if (error) {
        g_printerr("Failed to load script: %s\n", error->message);
        g_error_free(error);
        return -1;
    }
    
    return 0;
}

int frida_controller_resume(FridaController* controller) {
    if (!controller) return -1;
    
    if (controller->state != PROCESS_STATE_SUSPENDED && 
        controller->state != PROCESS_STATE_ATTACHED) {
        return -1;
    }
    
    #ifdef __APPLE__
    if (controller->pid > 0) {
        // Resume suspended process on macOS
        kill(controller->pid, SIGCONT);
    }
    #endif
    
    // Resume via Frida if using Frida spawn
    if (controller->device && controller->pid > 0) {
        GError* error = NULL;
        frida_device_resume_sync(controller->device, controller->pid, NULL, &error);
        if (error) {
            g_error_free(error);
        }
    }
    
    controller->state = PROCESS_STATE_RUNNING;
    controller->control_block->process_state = PROCESS_STATE_RUNNING;
    
    return 0;
}

int frida_controller_detach(FridaController* controller) {
    if (!controller || !controller->session) return -1;
    
    controller->state = PROCESS_STATE_DETACHING;
    controller->control_block->process_state = PROCESS_STATE_DETACHING;
    
    GError* error = NULL;
    frida_session_detach_sync(controller->session, NULL, &error);
    
    if (error) {
        g_error_free(error);
        return -1;
    }
    
    controller->state = PROCESS_STATE_INITIALIZED;
    controller->control_block->process_state = PROCESS_STATE_INITIALIZED;
    
    return 0;
}

ProcessState frida_controller_get_state(FridaController* controller) {
    return controller ? controller->state : PROCESS_STATE_UNINITIALIZED;
}

TracerStats frida_controller_get_stats(FridaController* controller) {
    if (!controller) {
        TracerStats empty = {0};
        return empty;
    }
    return controller->stats;
}

// Callbacks
static void on_detached(FridaSession* session, FridaSessionDetachReason reason,
                        FridaCrash* crash, gpointer user_data) {
    FridaController* controller = (FridaController*)user_data;
    controller->state = PROCESS_STATE_INITIALIZED;
    controller->control_block->process_state = PROCESS_STATE_INITIALIZED;
}

static void on_message(FridaScript* script, const gchar* message,
                       GBytes* data, gpointer user_data) {
    g_print("Script message: %s\n", message);
}

// Drain thread
static void* drain_thread_func(void* arg) {
    FridaController* controller = (FridaController*)arg;
    IndexEvent index_events[1000];
    DetailEvent detail_events[100];
    
    while (controller->drain_running) {
        // Drain index lane
        size_t index_count = ring_buffer_read_batch(controller->index_ring, 
                                                    index_events, 1000);
        if (index_count > 0) {
            controller->stats.events_captured += index_count;
            controller->stats.bytes_written += index_count * sizeof(IndexEvent);
            
            // Write to file if open
            if (controller->output_file) {
                fwrite(index_events, sizeof(IndexEvent), index_count, 
                      controller->output_file);
            }
        }
        
        // Drain detail lane
        size_t detail_count = ring_buffer_read_batch(controller->detail_ring,
                                                     detail_events, 100);
        if (detail_count > 0) {
            controller->stats.events_captured += detail_count;
            controller->stats.bytes_written += detail_count * sizeof(DetailEvent);
            
            // Write to file if open
            if (controller->output_file) {
                fwrite(detail_events, sizeof(DetailEvent), detail_count,
                      controller->output_file);
            }
        }
        
        controller->stats.drain_cycles++;
        
        // Sleep for 100ms
        usleep(100000);
    }
    
    return NULL;
}