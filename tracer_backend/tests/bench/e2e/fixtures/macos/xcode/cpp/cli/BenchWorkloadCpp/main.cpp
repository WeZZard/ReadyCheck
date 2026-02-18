// main.cpp - C++ E2E benchmark workload (Xcode fixture) for ADA tracing overhead
//
// Exercises six C++ invocation patterns that a tracer must handle:
//   1. Direct function call (free function)  - mangled C++ symbol
//   2. Virtual method dispatch               - vtable interception
//   3. Non-virtual method call               - direct mangled method
//   4. Template instantiation                - template symbol resolution
//   5. std::function / lambda                - closure call
//   6. Threaded burst                        - std::thread with compute calls
//
// Build: Xcode command-line tool (see BenchWorkloadCpp.xcodeproj)
// Usage: BenchWorkloadCpp [--outer N] [--inner N] [--depth N]
//                         [--threads N] [--thread-iterations N]

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Default parameters
// ---------------------------------------------------------------------------
static int g_outer = 1000;
static int g_inner = 1000;
static int g_depth = 15;
static int g_threads = 4;
static int g_thread_iterations = 10000;

// ---------------------------------------------------------------------------
// Pattern 1: Direct function call (free function) - mangled C++ symbol
// ---------------------------------------------------------------------------

__attribute__((noinline))
uint64_t work_leaf(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

// ---------------------------------------------------------------------------
// Pattern 2: Virtual method dispatch - vtable interception
// ---------------------------------------------------------------------------

class WorkBase {
public:
    virtual ~WorkBase() = default;
    virtual uint64_t compute(uint64_t x) = 0;
    virtual uint64_t computeLoop(int count) = 0;
};

class WorkLeaf : public WorkBase {
public:
    __attribute__((noinline))
    uint64_t compute(uint64_t x) override {
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return x;
    }

    __attribute__((noinline))
    uint64_t computeLoop(int count) override {
        uint64_t acc = 0;
        for (int i = 0; i < count; i++) {
            acc += compute(acc ^ static_cast<uint64_t>(i));
        }
        return acc;
    }
};

class WorkMiddle : public WorkBase {
    WorkBase& leaf_;

public:
    explicit WorkMiddle(WorkBase& leaf) : leaf_(leaf) {}

    __attribute__((noinline))
    uint64_t compute(uint64_t x) override {
        return leaf_.compute(x);
    }

    __attribute__((noinline))
    uint64_t computeLoop(int count) override {
        uint64_t acc = 0;
        for (int i = 0; i < count; i++) {
            acc += leaf_.compute(acc ^ static_cast<uint64_t>(i));
        }
        return acc;
    }
};

// ---------------------------------------------------------------------------
// Pattern 3: Non-virtual method call - direct mangled method
// ---------------------------------------------------------------------------

class Worker {
public:
    __attribute__((noinline))
    uint64_t computeDirect(uint64_t x) {
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return x;
    }
};

// ---------------------------------------------------------------------------
// Pattern 4: Template instantiation - template symbol resolution
// ---------------------------------------------------------------------------

template <typename T>
__attribute__((noinline))
T compute_tmpl(T x) {
    x ^= x >> 33;
    x *= static_cast<T>(0xff51afd7ed558ccdULL);
    x ^= x >> 33;
    x *= static_cast<T>(0xc4ceb9fe1a85ec53ULL);
    x ^= x >> 33;
    return x;
}

// Explicit instantiation to ensure the symbol is emitted
template uint64_t compute_tmpl<uint64_t>(uint64_t);

// ---------------------------------------------------------------------------
// Pattern 5: std::function / lambda - closure call
// ---------------------------------------------------------------------------

__attribute__((noinline))
uint64_t invoke_callback(std::function<uint64_t(uint64_t)> const& fn, uint64_t x) {
    return fn(x);
}

// ---------------------------------------------------------------------------
// Recursive with virtual dispatch (reuses virtual hierarchy)
// ---------------------------------------------------------------------------

__attribute__((noinline))
uint64_t tree_recurse(int depth, WorkBase& worker, uint64_t acc) {
    if (depth <= 0) {
        return worker.compute(acc);
    }
    return tree_recurse(depth - 1, worker, acc + 1)
         + tree_recurse(depth - 1, worker, acc + 2);
}

// ---------------------------------------------------------------------------
// Pattern 6: Threaded burst
// ---------------------------------------------------------------------------

__attribute__((noinline))
void thread_worker(WorkBase* worker, int iterations, std::atomic<uint64_t>& result) {
    uint64_t acc = 0;
    for (int i = 0; i < iterations; i++) {
        acc += worker->compute(acc ^ static_cast<uint64_t>(i));
    }
    result.fetch_add(acc, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

static void parse_args(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--outer") == 0 && i + 1 < argc) {
            g_outer = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--inner") == 0 && i + 1 < argc) {
            g_inner = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
            g_depth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            g_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--thread-iterations") == 0 && i + 1 < argc) {
            g_thread_iterations = atoi(argv[++i]);
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    parse_args(argc, argv);

    uint64_t total_result = 0;
    uint64_t total_calls = 0;

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    WorkLeaf leaf;
    WorkMiddle middle(leaf);
    Worker direct_worker;

    // --- Pattern 2: Virtual dispatch flat loop ---
    // middle.computeLoop is called outer times (each call is 1 virtual call).
    // Inside each computeLoop, leaf.compute is called inner times (each is 1
    // virtual call).
    // Total virtual calls: outer (computeLoop) + outer*inner (compute).
    {
        int inner_arg = g_inner;
        for (int i = 0; i < g_outer; i++) {
            total_result += middle.computeLoop(inner_arg);
            // Compiler barrier: clobber both result AND argument so the
            // compiler cannot prove computeLoop(inner_arg) is loop-invariant.
            asm volatile("" : "+r"(total_result), "+r"(inner_arg));
        }
    }
    total_calls += static_cast<uint64_t>(g_outer)
                 + static_cast<uint64_t>(g_outer) * g_inner;

    // --- Pattern 3: Non-virtual direct method ---
    for (int i = 0; i < g_outer; i++) {
        total_result += direct_worker.computeDirect(
            total_result ^ static_cast<uint64_t>(i));
    }
    total_calls += static_cast<uint64_t>(g_outer);

    // --- Pattern 4: Template instantiation ---
    for (int i = 0; i < g_outer; i++) {
        total_result += compute_tmpl<uint64_t>(
            total_result ^ static_cast<uint64_t>(i));
    }
    total_calls += static_cast<uint64_t>(g_outer);

    // --- Pattern 5: std::function / lambda ---
    std::function<uint64_t(uint64_t)> lambda_fn = [](uint64_t x) -> uint64_t {
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return x;
    };
    for (int i = 0; i < g_outer; i++) {
        total_result += invoke_callback(
            lambda_fn, total_result ^ static_cast<uint64_t>(i));
    }
    total_calls += static_cast<uint64_t>(g_outer);

    // --- Recursive tree with virtual dispatch ---
    // Total tree_recurse invocations: 2^(depth+1) - 1
    total_result += tree_recurse(g_depth, leaf, 0);
    total_calls += (1ULL << (g_depth + 1)) - 1;

    // --- Pattern 6: Threaded burst ---
    std::vector<std::thread> threads;
    std::atomic<uint64_t> thread_result{0};

    for (int t = 0; t < g_threads; t++) {
        threads.emplace_back(thread_worker, &leaf, g_thread_iterations,
                             std::ref(thread_result));
    }

    for (auto& t : threads) {
        t.join();
    }
    total_result += thread_result.load();
    total_calls += static_cast<uint64_t>(g_threads) * g_thread_iterations;

    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double hotpath_ms = (ts_end.tv_sec - ts_start.tv_sec) * 1000.0
                      + (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000.0;

    fprintf(stderr, "BENCH_RESULT lang=cpp total_calls=%llu checksum=%llu hotpath_ms=%.3f\n",
            static_cast<unsigned long long>(total_calls),
            static_cast<unsigned long long>(total_result),
            hotpath_ms);

    return 0;
}
