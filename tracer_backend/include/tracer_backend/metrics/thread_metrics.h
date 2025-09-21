#ifndef TRACER_BACKEND_METRICS_THREAD_METRICS_H
#define TRACER_BACKEND_METRICS_THREAD_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#if defined(__cplusplus)
#include <atomic>
#else
#include <stdatomic.h>
#endif

#include <tracer_backend/utils/tracer_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ADA_METRICS_RATE_HISTORY
#define ADA_METRICS_RATE_HISTORY 8u
#endif

#ifndef ADA_METRICS_WINDOW_NS
#define ADA_METRICS_WINDOW_NS 100000000ull
#endif

#ifndef ADA_METRICS_ALIGN_CACHELINE
#if defined(__cplusplus)
#define ADA_METRICS_ALIGN_CACHELINE alignas(CACHE_LINE_SIZE)
#else
#define ADA_METRICS_ALIGN_CACHELINE _Alignas(CACHE_LINE_SIZE)
#endif
#endif

#if defined(__cplusplus)
#define ADA_ATOMIC_LOAD(atom, order) (atom).load(order)
#define ADA_ATOMIC_STORE(atom, value, order) (atom).store(value, order)
#define ADA_ATOMIC_FETCH_ADD(atom, value, order) (atom).fetch_add(value, order)
#define ADA_ATOMIC_FETCH_SUB(atom, value, order) (atom).fetch_sub(value, order)
#else
#define ADA_ATOMIC_LOAD(atom, order) atomic_load_explicit(&(atom), order)
#define ADA_ATOMIC_STORE(atom, value, order) atomic_store_explicit(&(atom), value, order)
#define ADA_ATOMIC_FETCH_ADD(atom, value, order) atomic_fetch_add_explicit(&(atom), value, order)
#define ADA_ATOMIC_FETCH_SUB(atom, value, order) atomic_fetch_sub_explicit(&(atom), value, order)
#endif

#if defined(__cplusplus)
#define ADA_MEMORY_ORDER_RELAXED std::memory_order_relaxed
#define ADA_MEMORY_ORDER_ACQUIRE std::memory_order_acquire
#define ADA_MEMORY_ORDER_RELEASE std::memory_order_release
#define ADA_MEMORY_ORDER_ACQ_REL std::memory_order_acq_rel
#else
#define ADA_MEMORY_ORDER_RELAXED memory_order_relaxed
#define ADA_MEMORY_ORDER_ACQUIRE memory_order_acquire
#define ADA_MEMORY_ORDER_RELEASE memory_order_release
#define ADA_MEMORY_ORDER_ACQ_REL memory_order_acq_rel
#endif

// Forward declaration for swap guard
typedef struct ada_metrics_swap_token ada_metrics_swap_token_t;

// Sliding window sample used for rate calculations
typedef struct ada_metrics_rate_sample {
    uint64_t timestamp_ns;
    uint64_t events;
    uint64_t bytes;
} ada_metrics_rate_sample_t;

// Per-thread metrics container. Embedded in ThreadLaneSet to keep ownership local.
typedef struct ada_thread_metrics {
    uint64_t thread_id;
    uint32_t slot_index;
    uint32_t _reserved; // keep 64-bit alignment for counters

    ADA_METRICS_ALIGN_CACHELINE struct {
#if defined(__cplusplus)
        std::atomic<uint64_t> events_written;
        std::atomic<uint64_t> events_dropped;
        std::atomic<uint64_t> events_filtered;
        std::atomic<uint64_t> bytes_written;
#else
        _Atomic(uint64_t) events_written;
        _Atomic(uint64_t) events_dropped;
        _Atomic(uint64_t) events_filtered;
        _Atomic(uint64_t) bytes_written;
#endif
    } counters;

    ADA_METRICS_ALIGN_CACHELINE struct {
#if defined(__cplusplus)
        std::atomic<uint64_t> pool_exhaustion_count;
        std::atomic<uint64_t> ring_full_count;
        std::atomic<uint64_t> allocation_failures;
        std::atomic<uint64_t> max_queue_depth;
#else
        _Atomic(uint64_t) pool_exhaustion_count;
        _Atomic(uint64_t) ring_full_count;
        _Atomic(uint64_t) allocation_failures;
        _Atomic(uint64_t) max_queue_depth;
#endif
    } pressure;

    ADA_METRICS_ALIGN_CACHELINE struct {
#if defined(__cplusplus)
        std::atomic<uint64_t> swap_count;
        std::atomic<uint64_t> last_swap_timestamp_ns;
        std::atomic<uint64_t> total_swap_duration_ns;
        std::atomic<uint32_t> rings_in_rotation;
#else
        _Atomic(uint64_t) swap_count;
        _Atomic(uint64_t) last_swap_timestamp_ns;
        _Atomic(uint64_t) total_swap_duration_ns;
        _Atomic(uint32_t) rings_in_rotation;
#endif
        uint32_t _padding;
    } swaps;

    ADA_METRICS_ALIGN_CACHELINE struct {
        uint32_t sample_head;
        uint32_t sample_count;
        uint64_t window_duration_ns;
        uint64_t window_events;
        uint64_t window_bytes;
        double events_per_second;
        double bytes_per_second;
        ada_metrics_rate_sample_t samples[ADA_METRICS_RATE_HISTORY];
    } rate;
} ada_thread_metrics_t;

// Snapshot structure capturing a stable view of per-thread metrics.
typedef struct ada_thread_metrics_snapshot {
    uint64_t thread_id;
    uint32_t slot_index;
    uint32_t reserved;
    uint64_t timestamp_ns;

    uint64_t events_written;
    uint64_t events_dropped;
    uint64_t events_filtered;
    uint64_t bytes_written;

    double events_per_second;
    double bytes_per_second;
    double drop_rate_percent;

    uint64_t pool_exhaustion_count;
    uint64_t ring_full_count;
    uint64_t allocation_failures;
    uint64_t max_queue_depth;

    uint64_t swap_count;
    double swaps_per_second;
    uint64_t avg_swap_duration_ns;
    uint64_t last_swap_timestamp_ns;
    uint32_t rings_in_rotation;
    uint32_t _pad2;
} ada_thread_metrics_snapshot_t;

// Guard used to measure swap durations without heap allocations.
struct ada_metrics_swap_token {
    ada_thread_metrics_t* metrics;
    uint64_t start_ns;
};

// ---------------------------------------------------------------------------
// Initialization and lifecycle
// ---------------------------------------------------------------------------

void ada_thread_metrics_init(ada_thread_metrics_t* metrics,
                             uint64_t thread_id,
                             uint32_t slot_index);

void ada_thread_metrics_reset(ada_thread_metrics_t* metrics);

// ---------------------------------------------------------------------------
// High-frequency counters (hot path). Implemented inline to stay <5ns.
// ---------------------------------------------------------------------------

static inline void ada_thread_metrics_record_event_written(ada_thread_metrics_t* metrics,
                                                           uint64_t bytes) {
    if (!metrics) return;
    ADA_ATOMIC_FETCH_ADD(metrics->counters.events_written, 1u,
                         ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_FETCH_ADD(metrics->counters.bytes_written, bytes,
                         ADA_MEMORY_ORDER_RELAXED);
}

static inline void ada_thread_metrics_record_events_written_bulk(ada_thread_metrics_t* metrics,
                                                                 uint64_t events,
                                                                 uint64_t bytes) {
    if (!metrics) return;
    if (events) {
        ADA_ATOMIC_FETCH_ADD(metrics->counters.events_written, events,
                             ADA_MEMORY_ORDER_RELAXED);
    }
    if (bytes) {
        ADA_ATOMIC_FETCH_ADD(metrics->counters.bytes_written, bytes,
                             ADA_MEMORY_ORDER_RELAXED);
    }
}

static inline void ada_thread_metrics_record_event_dropped(ada_thread_metrics_t* metrics) {
    if (!metrics) return;
    ADA_ATOMIC_FETCH_ADD(metrics->counters.events_dropped, 1u,
                         ADA_MEMORY_ORDER_RELAXED);
}

static inline void ada_thread_metrics_record_event_filtered(ada_thread_metrics_t* metrics) {
    if (!metrics) return;
    ADA_ATOMIC_FETCH_ADD(metrics->counters.events_filtered, 1u,
                         ADA_MEMORY_ORDER_RELAXED);
}

static inline void ada_thread_metrics_record_ring_full(ada_thread_metrics_t* metrics) {
    if (!metrics) return;
    ADA_ATOMIC_FETCH_ADD(metrics->pressure.ring_full_count, 1u,
                         ADA_MEMORY_ORDER_RELAXED);
}

static inline void ada_thread_metrics_record_pool_exhaustion(ada_thread_metrics_t* metrics) {
    if (!metrics) return;
    ADA_ATOMIC_FETCH_ADD(metrics->pressure.pool_exhaustion_count, 1u,
                         ADA_MEMORY_ORDER_RELAXED);
}

static inline void ada_thread_metrics_record_allocation_failure(ada_thread_metrics_t* metrics) {
    if (!metrics) return;
    ADA_ATOMIC_FETCH_ADD(metrics->pressure.allocation_failures, 1u,
                         ADA_MEMORY_ORDER_RELAXED);
}

static inline void ada_thread_metrics_observe_queue_depth(ada_thread_metrics_t* metrics,
                                                          uint32_t depth) {
    if (!metrics) return;
    uint64_t depth64 = depth;
    uint64_t current = ADA_ATOMIC_LOAD(metrics->pressure.max_queue_depth,
                                       ADA_MEMORY_ORDER_RELAXED);
#if defined(__cplusplus)
    while (depth64 > current) {
        if (metrics->pressure.max_queue_depth.compare_exchange_weak(
                current,
                depth64,
                ADA_MEMORY_ORDER_RELAXED,
                ADA_MEMORY_ORDER_RELAXED)) {
            break;
        }
    }
#else
    while (depth64 > current) {
        if (atomic_compare_exchange_weak_explicit(&metrics->pressure.max_queue_depth,
                                                  &current,
                                                  depth64,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            break;
        }
    }
#endif
}

static inline void ada_thread_metrics_set_rings_in_rotation(ada_thread_metrics_t* metrics,
                                                            uint32_t rings) {
    if (!metrics) return;
    ADA_ATOMIC_STORE(metrics->swaps.rings_in_rotation, rings,
                     ADA_MEMORY_ORDER_RELAXED);
}

// ---------------------------------------------------------------------------
// Swap tracking helpers
// ---------------------------------------------------------------------------

static inline ada_metrics_swap_token_t ada_thread_metrics_swap_begin(ada_thread_metrics_t* metrics,
                                                                     uint64_t start_ns) {
    ada_metrics_swap_token_t token = {metrics, start_ns};
    return token;
}

void ada_thread_metrics_swap_end(ada_metrics_swap_token_t* token,
                                 uint64_t end_ns,
                                 uint32_t rings_in_rotation);

// ---------------------------------------------------------------------------
// Rate calculation helpers (single collector)
// ---------------------------------------------------------------------------

void ada_thread_metrics_update_rate(ada_thread_metrics_t* metrics,
                                    uint64_t timestamp_ns,
                                    uint64_t events,
                                    uint64_t bytes);

// ---------------------------------------------------------------------------
// Snapshot helpers
// ---------------------------------------------------------------------------

void ada_thread_metrics_snapshot_capture(const ada_thread_metrics_t* metrics,
                                         uint64_t timestamp_ns,
                                         ada_thread_metrics_snapshot_t* snapshot);

void ada_thread_metrics_snapshot_apply_rates(ada_thread_metrics_snapshot_t* snapshot,
                                             double events_per_second,
                                             double bytes_per_second);

void ada_thread_metrics_snapshot_set_swap_rate(ada_thread_metrics_snapshot_t* snapshot,
                                               double swaps_per_second);

// ---------------------------------------------------------------------------
// Time helper used by metrics code (monotonic)
// ---------------------------------------------------------------------------

static inline uint64_t ada_metrics_now_ns(void) {
#if defined(__APPLE__)
    // clock_gettime is available on macOS 10.12+
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_METRICS_THREAD_METRICS_H
