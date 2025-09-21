#ifndef TRACER_BACKEND_METRICS_GLOBAL_METRICS_H
#define TRACER_BACKEND_METRICS_GLOBAL_METRICS_H

#include <stddef.h>
#include <stdint.h>

#include <tracer_backend/metrics/thread_metrics.h>
#include <tracer_backend/utils/tracer_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ADA_METRICS_ALIGN_CACHELINE
#if defined(__cplusplus)
#define ADA_METRICS_ALIGN_CACHELINE alignas(CACHE_LINE_SIZE)
#else
#define ADA_METRICS_ALIGN_CACHELINE _Alignas(CACHE_LINE_SIZE)
#endif
#endif

#if defined(__cplusplus)
#define ADA_GLOBAL_ATOMIC_SIZE std::atomic<size_t>
#define ADA_GLOBAL_ATOMIC_U64 std::atomic<uint64_t>
#define ADA_GLOBAL_ATOMIC_BOOL std::atomic<bool>
#else
#define ADA_GLOBAL_ATOMIC_SIZE _Atomic(size_t)
#define ADA_GLOBAL_ATOMIC_U64 _Atomic(uint64_t)
#define ADA_GLOBAL_ATOMIC_BOOL _Atomic(bool)
#endif

struct ThreadRegistry; // forward decl

typedef struct ada_global_metrics_totals {
    uint64_t total_events_written;
    uint64_t total_events_dropped;
    uint64_t total_events_filtered;
    uint64_t total_bytes_written;
    uint64_t active_thread_count;
} ada_global_metrics_totals_t;

typedef struct ada_global_metrics_rates {
    double system_events_per_second;
    double system_bytes_per_second;
    uint64_t last_window_ns;
} ada_global_metrics_rates_t;

typedef struct ada_global_metrics_control {
    ADA_GLOBAL_ATOMIC_U64 collection_interval_ns;
    ADA_GLOBAL_ATOMIC_U64 last_collection_ns;
    ADA_GLOBAL_ATOMIC_BOOL collection_enabled;
} ada_global_metrics_control_t;

typedef struct ada_global_metrics_state {
    ADA_METRICS_ALIGN_CACHELINE ada_global_metrics_totals_t totals;
    ADA_METRICS_ALIGN_CACHELINE ada_global_metrics_rates_t rates;

    ada_thread_metrics_snapshot_t* snapshots;
    size_t snapshot_capacity;
    ADA_GLOBAL_ATOMIC_SIZE snapshot_count;

    ADA_METRICS_ALIGN_CACHELINE ada_global_metrics_control_t control;

    // Per-slot bookkeeping for swap rate calculations
    uint64_t previous_swap_count[MAX_THREADS];
    uint64_t previous_swap_timestamp[MAX_THREADS];
    uint64_t previous_thread_id[MAX_THREADS];
} ada_global_metrics_t;

// Initialize global metrics state with caller-provided snapshot buffer.
// Returns false if arguments are invalid.
bool ada_global_metrics_init(ada_global_metrics_t* global,
                             ada_thread_metrics_snapshot_t* snapshot_buffer,
                             size_t capacity);

// Disable copy helpers
void ada_global_metrics_reset(ada_global_metrics_t* global);

void ada_global_metrics_set_interval(ada_global_metrics_t* global,
                                     uint64_t interval_ns);

bool ada_global_metrics_collect(ada_global_metrics_t* global,
                                struct ThreadRegistry* registry,
                                uint64_t now_ns);

size_t ada_global_metrics_snapshot_count(const ada_global_metrics_t* global);

const ada_thread_metrics_snapshot_t* ada_global_metrics_snapshot_data(const ada_global_metrics_t* global);

ada_global_metrics_totals_t ada_global_metrics_get_totals(const ada_global_metrics_t* global);

ada_global_metrics_rates_t ada_global_metrics_get_rates(const ada_global_metrics_t* global);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_METRICS_GLOBAL_METRICS_H
