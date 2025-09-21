#include <tracer_backend/metrics/thread_metrics.h>

void ada_thread_metrics_snapshot_capture(const ada_thread_metrics_t* metrics,
                                         uint64_t timestamp_ns,
                                         ada_thread_metrics_snapshot_t* snapshot) {
    if (!metrics || !snapshot) return;

    snapshot->thread_id = metrics->thread_id;
    snapshot->slot_index = metrics->slot_index;
    snapshot->reserved = 0;
    snapshot->timestamp_ns = timestamp_ns;

    snapshot->events_written = ADA_ATOMIC_LOAD(metrics->counters.events_written,
                                               ADA_MEMORY_ORDER_RELAXED);
    snapshot->events_dropped = ADA_ATOMIC_LOAD(metrics->counters.events_dropped,
                                               ADA_MEMORY_ORDER_RELAXED);
    snapshot->events_filtered = ADA_ATOMIC_LOAD(metrics->counters.events_filtered,
                                                ADA_MEMORY_ORDER_RELAXED);
    snapshot->bytes_written = ADA_ATOMIC_LOAD(metrics->counters.bytes_written,
                                              ADA_MEMORY_ORDER_RELAXED);

    snapshot->events_per_second = metrics->rate.events_per_second;
    snapshot->bytes_per_second = metrics->rate.bytes_per_second;

    uint64_t total_events = snapshot->events_written + snapshot->events_dropped;
    if (total_events == 0) {
        snapshot->drop_rate_percent = 0.0;
    } else {
        snapshot->drop_rate_percent =
            (double)snapshot->events_dropped * 100.0 / (double)total_events;
    }

    snapshot->pool_exhaustion_count = ADA_ATOMIC_LOAD(metrics->pressure.pool_exhaustion_count,
                                                      ADA_MEMORY_ORDER_RELAXED);
    snapshot->ring_full_count = ADA_ATOMIC_LOAD(metrics->pressure.ring_full_count,
                                                ADA_MEMORY_ORDER_RELAXED);
    snapshot->allocation_failures = ADA_ATOMIC_LOAD(metrics->pressure.allocation_failures,
                                                    ADA_MEMORY_ORDER_RELAXED);
    snapshot->max_queue_depth = ADA_ATOMIC_LOAD(metrics->pressure.max_queue_depth,
                                                ADA_MEMORY_ORDER_RELAXED);

    snapshot->swap_count = ADA_ATOMIC_LOAD(metrics->swaps.swap_count,
                                           ADA_MEMORY_ORDER_RELAXED);
    snapshot->last_swap_timestamp_ns = ADA_ATOMIC_LOAD(metrics->swaps.last_swap_timestamp_ns,
                                                       ADA_MEMORY_ORDER_RELAXED);
    snapshot->rings_in_rotation = ADA_ATOMIC_LOAD(metrics->swaps.rings_in_rotation,
                                                  ADA_MEMORY_ORDER_RELAXED);

    uint64_t total_swap = ADA_ATOMIC_LOAD(metrics->swaps.total_swap_duration_ns,
                                          ADA_MEMORY_ORDER_RELAXED);
    if (snapshot->swap_count == 0) {
        snapshot->avg_swap_duration_ns = 0;
    } else {
        snapshot->avg_swap_duration_ns = total_swap / snapshot->swap_count;
    }

    snapshot->swaps_per_second = 0.0;
    snapshot->_pad2 = 0;
}

void ada_thread_metrics_snapshot_apply_rates(ada_thread_metrics_snapshot_t* snapshot,
                                             double events_per_second,
                                             double bytes_per_second) {
    if (!snapshot) return;
    snapshot->events_per_second = events_per_second;
    snapshot->bytes_per_second = bytes_per_second;
}

void ada_thread_metrics_snapshot_set_swap_rate(ada_thread_metrics_snapshot_t* snapshot,
                                               double swaps_per_second) {
    if (!snapshot) return;
    snapshot->swaps_per_second = swaps_per_second;
}
