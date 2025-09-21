#include <atomic>
#include <cstring>

#include <tracer_backend/metrics/global_metrics.h>

#include "thread_metrics_private.h"

#include <tracer_backend/utils/thread_registry.h>
#include "../utils/thread_registry_private.h"

namespace {

using ada::metrics::RateResult;
using ada::metrics::rate_calculator_sample;

static uint32_t compute_queue_depth(std::atomic<uint32_t>& head,
                                    std::atomic<uint32_t>& tail,
                                    uint32_t capacity) {
    uint32_t h = head.load(std::memory_order_acquire);
    uint32_t t = tail.load(std::memory_order_acquire);
    if (capacity == 0u) return 0u;
    if (t >= h) {
        return t - h;
    }
    return capacity - (h - t);
}

static double compute_swaps_per_second(ada_global_metrics_t* global,
                                       uint32_t slot,
                                       uint64_t thread_id,
                                       uint64_t swap_count,
                                       uint64_t now_ns) {
    if (slot >= MAX_THREADS) return 0.0;

    if (global->previous_thread_id[slot] != thread_id) {
        global->previous_thread_id[slot] = thread_id;
        global->previous_swap_count[slot] = swap_count;
        global->previous_swap_timestamp[slot] = now_ns;
        return 0.0;
    }

    uint64_t prev_count = global->previous_swap_count[slot];
    uint64_t prev_ts = global->previous_swap_timestamp[slot];

    global->previous_swap_count[slot] = swap_count;
    global->previous_swap_timestamp[slot] = now_ns;

    if (now_ns <= prev_ts || swap_count <= prev_count) {
        return 0.0;
    }

    uint64_t delta_count = swap_count - prev_count;
    uint64_t delta_ns = now_ns - prev_ts;
    if (delta_ns == 0ull) return 0.0;
    return static_cast<double>(delta_count) * 1000000000.0 / static_cast<double>(delta_ns);
}

} // namespace

extern "C" {

bool ada_global_metrics_init(ada_global_metrics_t* global,
                             ada_thread_metrics_snapshot_t* snapshot_buffer,
                             size_t capacity) {
    if (!global || !snapshot_buffer || capacity == 0u) {
        return false;
    }
    std::memset(global, 0, sizeof(*global));
    global->snapshots = snapshot_buffer;
    global->snapshot_capacity = capacity;
    ADA_ATOMIC_STORE(global->snapshot_count, 0, ADA_MEMORY_ORDER_RELAXED);

    global->totals = {};
    global->rates = {};

    ADA_ATOMIC_STORE(global->control.collection_interval_ns,
                     ADA_METRICS_WINDOW_NS,
                     ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(global->control.last_collection_ns, 0, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(global->control.collection_enabled, true, ADA_MEMORY_ORDER_RELAXED);

    std::memset(global->previous_swap_count, 0, sizeof(global->previous_swap_count));
    std::memset(global->previous_swap_timestamp, 0, sizeof(global->previous_swap_timestamp));
    std::memset(global->previous_thread_id, 0, sizeof(global->previous_thread_id));
    return true;
}

void ada_global_metrics_reset(ada_global_metrics_t* global) {
    if (!global) return;
    ada_thread_metrics_snapshot_t* buffer = global->snapshots;
    size_t capacity = global->snapshot_capacity;
    ada_global_metrics_init(global, buffer, capacity);
}

void ada_global_metrics_set_interval(ada_global_metrics_t* global,
                                     uint64_t interval_ns) {
    if (!global || interval_ns == 0ull) return;
    ADA_ATOMIC_STORE(global->control.collection_interval_ns,
                     interval_ns,
                     ADA_MEMORY_ORDER_RELAXED);
}

bool ada_global_metrics_collect(ada_global_metrics_t* global,
                                ThreadRegistry* registry,
                                uint64_t now_ns) {
    if (!global || !registry) return false;
    if (!ADA_ATOMIC_LOAD(global->control.collection_enabled, ADA_MEMORY_ORDER_RELAXED)) {
        return false;
    }

    uint64_t last = ADA_ATOMIC_LOAD(global->control.last_collection_ns,
                                    ADA_MEMORY_ORDER_ACQUIRE);
    uint64_t interval = ADA_ATOMIC_LOAD(global->control.collection_interval_ns,
                                        ADA_MEMORY_ORDER_RELAXED);
    if (interval == 0ull) interval = ADA_METRICS_WINDOW_NS;
    if (last != 0ull && now_ns - last < interval) {
        return false;
    }

    bool claimed = false;
#if defined(__cplusplus)
    claimed = global->control.last_collection_ns.compare_exchange_strong(
        last,
        now_ns,
        ADA_MEMORY_ORDER_ACQ_REL,
        ADA_MEMORY_ORDER_ACQUIRE);
#else
    claimed = atomic_compare_exchange_strong_explicit(&global->control.last_collection_ns,
                                                      &last,
                                                      now_ns,
                                                      memory_order_acq_rel,
                                                      memory_order_acquire);
#endif
    if (!claimed) {
        return false;
    }

    global->totals = {};
    global->rates = {};

    size_t snapshot_index = 0;

    auto* cpp_registry = ada::internal::to_cpp(registry);
    uint32_t capacity = cpp_registry->get_capacity();
    for (uint32_t i = 0; i < capacity; ++i) {
        ThreadLaneSet* lanes = thread_registry_get_thread_at(registry, i);
        if (!lanes) continue;

        if (snapshot_index >= global->snapshot_capacity) {
            break;
        }

        ada_thread_metrics_t* metrics = thread_lanes_get_metrics(lanes);
        if (!metrics) {
            continue;
        }

        auto* cpp_lanes = ada::internal::to_cpp(lanes);
        uint32_t index_depth = compute_queue_depth(cpp_lanes->index_lane.submit_head,
                                                   cpp_lanes->index_lane.submit_tail,
                                                   cpp_lanes->index_lane.submit_capacity);
        uint32_t detail_depth = compute_queue_depth(cpp_lanes->detail_lane.submit_head,
                                                    cpp_lanes->detail_lane.submit_tail,
                                                    cpp_lanes->detail_lane.submit_capacity);
        uint32_t depth = index_depth + detail_depth;
        ada_thread_metrics_observe_queue_depth(metrics, depth);

        uint64_t events = ADA_ATOMIC_LOAD(metrics->counters.events_written,
                                          ADA_MEMORY_ORDER_RELAXED);
        uint64_t bytes = ADA_ATOMIC_LOAD(metrics->counters.bytes_written,
                                         ADA_MEMORY_ORDER_RELAXED);

        RateResult rate = rate_calculator_sample(metrics, now_ns, events, bytes);

        ada_thread_metrics_snapshot_t* snap = &global->snapshots[snapshot_index++];
        ada_thread_metrics_snapshot_capture(metrics, now_ns, snap);
        ada_thread_metrics_snapshot_apply_rates(snap,
                                                rate.events_per_second,
                                                rate.bytes_per_second);

        double swaps_per_second = compute_swaps_per_second(global,
                                                           snap->slot_index,
                                                           snap->thread_id,
                                                           snap->swap_count,
                                                           now_ns);
        ada_thread_metrics_snapshot_set_swap_rate(snap, swaps_per_second);

        global->totals.total_events_written += snap->events_written;
        global->totals.total_events_dropped += snap->events_dropped;
        global->totals.total_events_filtered += snap->events_filtered;
        global->totals.total_bytes_written += snap->bytes_written;
        global->totals.active_thread_count += 1u;

        global->rates.system_events_per_second += snap->events_per_second;
        global->rates.system_bytes_per_second += snap->bytes_per_second;
        global->rates.last_window_ns = rate.window_duration_ns;
    }

    if (snapshot_index == 0u) {
        global->rates.system_events_per_second = 0.0;
        global->rates.system_bytes_per_second = 0.0;
        global->rates.last_window_ns = 0;
    }

    ADA_ATOMIC_STORE(global->snapshot_count, snapshot_index, ADA_MEMORY_ORDER_RELEASE);
    return true;
}

size_t ada_global_metrics_snapshot_count(const ada_global_metrics_t* global) {
    if (!global) return 0;
    return ADA_ATOMIC_LOAD(global->snapshot_count, ADA_MEMORY_ORDER_ACQUIRE);
}

const ada_thread_metrics_snapshot_t* ada_global_metrics_snapshot_data(const ada_global_metrics_t* global) {
    if (!global) return nullptr;
    return global->snapshots;
}

ada_global_metrics_totals_t ada_global_metrics_get_totals(const ada_global_metrics_t* global) {
    if (!global) {
        ada_global_metrics_totals_t empty{};
        return empty;
    }
    return global->totals;
}

ada_global_metrics_rates_t ada_global_metrics_get_rates(const ada_global_metrics_t* global) {
    if (!global) {
        ada_global_metrics_rates_t empty{};
        return empty;
    }
    return global->rates;
}

} // extern "C"
