#include <cstring>

#include <tracer_backend/metrics/thread_metrics.h>

#include "thread_metrics_private.h"

namespace {

void clear_rate_history(ada_thread_metrics_t* metrics) {
    metrics->rate.sample_head = 0;
    metrics->rate.sample_count = 0;
    metrics->rate.window_duration_ns = 0;
    metrics->rate.window_events = 0;
    metrics->rate.window_bytes = 0;
    metrics->rate.events_per_second = 0.0;
    metrics->rate.bytes_per_second = 0.0;
    std::memset(metrics->rate.samples, 0, sizeof(metrics->rate.samples));
}

} // namespace

using ada::metrics::rate_calculator_sample;

void ada_thread_metrics_init(ada_thread_metrics_t* metrics,
                             uint64_t thread_id,
                             uint32_t slot_index) {
    if (!metrics) return;
    ada_thread_metrics_reset(metrics);
    metrics->thread_id = thread_id;
    metrics->slot_index = slot_index;
}

void ada_thread_metrics_reset(ada_thread_metrics_t* metrics) {
    if (!metrics) return;

    metrics->thread_id = 0;
    metrics->slot_index = 0;
    metrics->_reserved = 0;

    ADA_ATOMIC_STORE(metrics->counters.events_written, 0, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.events_dropped, 0, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.events_filtered, 0, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 0, ADA_MEMORY_ORDER_RELAXED);

    ADA_ATOMIC_STORE(metrics->pressure.pool_exhaustion_count, 0, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->pressure.ring_full_count, 0, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->pressure.allocation_failures, 0, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->pressure.max_queue_depth, 0, ADA_MEMORY_ORDER_RELAXED);

    ADA_ATOMIC_STORE(metrics->swaps.swap_count, 0, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->swaps.last_swap_timestamp_ns, 0, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->swaps.total_swap_duration_ns, 0, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->swaps.rings_in_rotation, 0, ADA_MEMORY_ORDER_RELAXED);
    metrics->swaps._padding = 0;

    clear_rate_history(metrics);
}

void ada_thread_metrics_swap_end(ada_metrics_swap_token_t* token,
                                 uint64_t end_ns,
                                 uint32_t rings_in_rotation) {
    if (!token || !token->metrics) return;
    if (end_ns < token->start_ns) {
        end_ns = token->start_ns;
    }

    uint64_t duration = end_ns - token->start_ns;
    ADA_ATOMIC_FETCH_ADD(token->metrics->swaps.swap_count, 1u,
                         ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(token->metrics->swaps.last_swap_timestamp_ns, end_ns,
                     ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_FETCH_ADD(token->metrics->swaps.total_swap_duration_ns, duration,
                         ADA_MEMORY_ORDER_RELAXED);
    ada_thread_metrics_set_rings_in_rotation(token->metrics, rings_in_rotation);
}

void ada_thread_metrics_update_rate(ada_thread_metrics_t* metrics,
                                    uint64_t timestamp_ns,
                                    uint64_t events,
                                    uint64_t bytes) {
    if (!metrics) return;
    ada::metrics::RateResult result = rate_calculator_sample(metrics, timestamp_ns, events, bytes);
    // Store the calculated rates back in the metrics
    metrics->rate.events_per_second = result.events_per_second;
    metrics->rate.bytes_per_second = result.bytes_per_second;
}
