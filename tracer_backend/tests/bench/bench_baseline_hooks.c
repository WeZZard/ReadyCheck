#include <benchmark/benchmark.h>
#include <thread>
#include <atomic>
#include <vector>
#include "tracer_controller.h"
#include "shared_memory.h"
#include "ring_buffer.h"
#include "tracer_types.h"

// Benchmark fixture for baseline hooks
class BaselineHooksBenchmark : public benchmark::Fixture {
protected:
    TracerController* controller = nullptr;
    pid_t target_pid = 0;
    uint32_t session_id = 0;
    SharedMemoryRef shm_index = nullptr;
    SharedMemoryRef shm_detail = nullptr;
    SharedMemoryRef shm_control = nullptr;
    RingBuffer* index_ring = nullptr;
    RingBuffer* detail_ring = nullptr;
    ControlBlock* control_block = nullptr;
    
    void SetUp(const ::benchmark::State& state) override {
        controller = tracer_controller_create();
        
        // Spawn test binary
        const char* binary = state.range(0) == 0 ? "test_cli" : "test_runloop";
        char path[256];
        snprintf(path, sizeof(path), "./test_fixtures/%s", binary);
        
        tracer_controller_spawn(controller, path, &target_pid);
        
        // Initialize with unique session ID
        session_id = 0xBEEF0000 | (rand() & 0xFFFF);
        tracer_controller_init(controller, target_pid, session_id);
        
        // Open shared memory
        shm_index = shared_memory_open_unique(SHM_ROLE_HOST, target_pid, session_id, SHM_LANE_INDEX);
        shm_detail = shared_memory_open_unique(SHM_ROLE_HOST, target_pid, session_id, SHM_LANE_DETAIL);
        shm_control = shared_memory_open_unique(SHM_ROLE_HOST, target_pid, session_id, SHM_LANE_CONTROL);
        
        index_ring = ring_buffer_attach(shared_memory_get_address(shm_index), 
                                       32 * 1024 * 1024, sizeof(IndexEvent));
        detail_ring = ring_buffer_attach(shared_memory_get_address(shm_detail),
                                        32 * 1024 * 1024, sizeof(DetailEvent));
        control_block = (ControlBlock*)shared_memory_get_address(shm_control);
        
        // Configure based on benchmark parameters
        control_block->index_lane_enabled = true;
        control_block->detail_lane_enabled = (state.range(1) == 1);
        control_block->capture_stack_snapshot = (state.range(2) == 1);
        control_block->flight_state = FLIGHT_RECORDER_RECORDING;
        
        // Let agent initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    void TearDown(const ::benchmark::State& state) override {
        if (index_ring) ring_buffer_destroy(index_ring);
        if (detail_ring) ring_buffer_destroy(detail_ring);
        if (shm_index) shared_memory_close(shm_index);
        if (shm_detail) shared_memory_close(shm_detail);
        if (shm_control) shared_memory_close(shm_control);
        if (controller) {
            tracer_controller_stop(controller);
            tracer_controller_destroy(controller);
        }
    }
};

// Benchmark: Event emission rate (index only)
BENCHMARK_DEFINE_F(BaselineHooksBenchmark, IndexOnlyEventRate)(benchmark::State& state) {
    uint64_t total_events = 0;
    
    for (auto _ : state) {
        // Drain events for 100ms
        auto start = std::chrono::high_resolution_clock::now();
        auto end = start + std::chrono::milliseconds(100);
        
        IndexEvent event;
        uint64_t count = 0;
        
        while (std::chrono::high_resolution_clock::now() < end) {
            while (ring_buffer_read(index_ring, &event)) {
                count++;
            }
            std::this_thread::yield();
        }
        
        total_events += count;
    }
    
    state.SetItemsProcessed(total_events);
    state.SetLabel("events/sec");
}

// Benchmark: Event emission rate (with detail events)
BENCHMARK_DEFINE_F(BaselineHooksBenchmark, DetailEventRate)(benchmark::State& state) {
    uint64_t total_index_events = 0;
    uint64_t total_detail_events = 0;
    
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        auto end = start + std::chrono::milliseconds(100);
        
        IndexEvent index_event;
        DetailEvent detail_event;
        uint64_t index_count = 0;
        uint64_t detail_count = 0;
        
        while (std::chrono::high_resolution_clock::now() < end) {
            while (ring_buffer_read(index_ring, &index_event)) {
                index_count++;
            }
            while (ring_buffer_read(detail_ring, &detail_event)) {
                detail_count++;
            }
            std::this_thread::yield();
        }
        
        total_index_events += index_count;
        total_detail_events += detail_count;
    }
    
    state.SetItemsProcessed(total_index_events + total_detail_events);
    state.counters["index_events"] = total_index_events;
    state.counters["detail_events"] = total_detail_events;
}

// Benchmark: Stack capture overhead
BENCHMARK_DEFINE_F(BaselineHooksBenchmark, StackCaptureOverhead)(benchmark::State& state) {
    uint64_t events_with_stack = 0;
    uint64_t events_without_stack = 0;
    
    for (auto _ : state) {
        DetailEvent event;
        uint64_t count = 0;
        
        // Measure for 100ms
        auto start = std::chrono::high_resolution_clock::now();
        auto end = start + std::chrono::milliseconds(100);
        
        while (std::chrono::high_resolution_clock::now() < end) {
            while (ring_buffer_read(detail_ring, &event)) {
                count++;
                if (event.stack_size > 0) {
                    events_with_stack++;
                } else {
                    events_without_stack++;
                }
            }
            std::this_thread::yield();
        }
    }
    
    state.counters["with_stack"] = events_with_stack;
    state.counters["without_stack"] = events_without_stack;
}

// Benchmark: Multi-threaded scaling
BENCHMARK_DEFINE_F(BaselineHooksBenchmark, MultiThreadScaling)(benchmark::State& state) {
    // test_runloop spawns multiple threads
    uint64_t total_events = 0;
    std::unordered_map<uint32_t, uint64_t> thread_events;
    
    for (auto _ : state) {
        IndexEvent event;
        
        auto start = std::chrono::high_resolution_clock::now();
        auto end = start + std::chrono::milliseconds(100);
        
        while (std::chrono::high_resolution_clock::now() < end) {
            while (ring_buffer_read(index_ring, &event)) {
                total_events++;
                thread_events[event.thread_id]++;
            }
            std::this_thread::yield();
        }
    }
    
    state.SetItemsProcessed(total_events);
    state.counters["unique_threads"] = thread_events.size();
    
    // Calculate thread balance
    if (!thread_events.empty()) {
        uint64_t min_events = UINT64_MAX;
        uint64_t max_events = 0;
        for (const auto& [tid, count] : thread_events) {
            min_events = std::min(min_events, count);
            max_events = std::max(max_events, count);
        }
        state.counters["thread_imbalance"] = max_events - min_events;
    }
}

// Benchmark: Hook call overhead
static void BM_HookOverhead(benchmark::State& state) {
    // This measures the overhead of a hooked vs unhooked function call
    // We'll use a simple function that we can call repeatedly
    
    typedef int (*fibonacci_func)(int);
    fibonacci_func fib = nullptr;
    
    // Get function pointer
    void* handle = dlopen(nullptr, RTLD_NOW);
    if (handle) {
        fib = (fibonacci_func)dlsym(handle, "fibonacci");
    }
    
    if (!fib) {
        state.SkipWithError("Could not find fibonacci function");
        return;
    }
    
    bool with_hooks = state.range(0) == 1;
    
    TracerController* ctrl = nullptr;
    pid_t pid = 0;
    
    if (with_hooks) {
        // Initialize tracer for current process
        ctrl = tracer_controller_create();
        pid = getpid();
        uint32_t sid = 0xCAFE0000 | (rand() & 0xFFFF);
        tracer_controller_init(ctrl, pid, sid);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    // Benchmark function calls
    for (auto _ : state) {
        benchmark::DoNotOptimize(fib(10));
    }
    
    if (ctrl) {
        tracer_controller_destroy(ctrl);
    }
    
    state.SetLabel(with_hooks ? "with_hooks" : "no_hooks");
}

// Register benchmarks with different configurations
// Args: binary (0=test_cli, 1=test_runloop), detail_enabled, stack_capture
BENCHMARK_REGISTER_F(BaselineHooksBenchmark, IndexOnlyEventRate)
    ->Args({0, 0, 0})  // test_cli, no detail, no stack
    ->Args({1, 0, 0})  // test_runloop, no detail, no stack
    ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(BaselineHooksBenchmark, DetailEventRate)
    ->Args({0, 1, 0})  // test_cli, with detail, no stack
    ->Args({1, 1, 0})  // test_runloop, with detail, no stack
    ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(BaselineHooksBenchmark, StackCaptureOverhead)
    ->Args({0, 1, 1})  // test_cli, with detail and stack
    ->Args({1, 1, 1})  // test_runloop, with detail and stack
    ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(BaselineHooksBenchmark, MultiThreadScaling)
    ->Args({1, 0, 0})  // test_runloop only (multi-threaded)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_HookOverhead)
    ->Args({0})  // without hooks
    ->Args({1})  // with hooks
    ->Unit(benchmark::kNanosecond);

// Memory usage benchmark
static void BM_MemoryUsage(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        
        // Create controller and spawn process
        TracerController* ctrl = tracer_controller_create();
        pid_t pid;
        tracer_controller_spawn(ctrl, "./test_fixtures/test_cli", &pid);
        
        uint32_t sid = 0xFEED0000 | (rand() & 0xFFFF);
        tracer_controller_init(ctrl, pid, sid);
        
        // Measure memory before heavy load
        size_t mem_before = get_process_memory_usage(pid);
        
        state.ResumeTiming();
        
        // Run for fixed duration under load
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        state.PauseTiming();
        
        // Measure memory after
        size_t mem_after = get_process_memory_usage(pid);
        
        state.counters["memory_growth_kb"] = (mem_after - mem_before) / 1024;
        
        tracer_controller_destroy(ctrl);
        
        state.ResumeTiming();
    }
}

BENCHMARK(BM_MemoryUsage)->Unit(benchmark::kSecond);

BENCHMARK_MAIN();