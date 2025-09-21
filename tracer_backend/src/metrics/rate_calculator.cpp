#include "thread_metrics_private.h"

#include <algorithm>

namespace ada {
namespace metrics {
namespace {

static constexpr uint64_t kWindowNs = ADA_METRICS_WINDOW_NS;

static inline uint32_t oldest_index(const ada_thread_metrics_t* metrics) {
    if (metrics->rate.sample_count == 0) {
        return 0;
    }
    uint32_t capacity = ADA_METRICS_RATE_HISTORY;
    uint32_t head = metrics->rate.sample_head;
    uint32_t count = metrics->rate.sample_count;
    return (head + capacity - count) % capacity;
}

static inline ada_metrics_rate_sample_t& sample_at(ada_thread_metrics_t* metrics,
                                                   uint32_t index) {
    return metrics->rate.samples[index % ADA_METRICS_RATE_HISTORY];
}

static inline const ada_metrics_rate_sample_t& sample_at_const(const ada_thread_metrics_t* metrics,
                                                              uint32_t index) {
    return metrics->rate.samples[index % ADA_METRICS_RATE_HISTORY];
}

} // namespace

RateResult rate_calculator_sample(ada_thread_metrics_t* metrics,
                                  uint64_t timestamp_ns,
                                  uint64_t events,
                                  uint64_t bytes) {
    RateResult result;
    if (!metrics) {
        return result;
    }

    uint32_t head = metrics->rate.sample_head;
    uint32_t count = metrics->rate.sample_count;

    // Insert new sample at head
    sample_at(metrics, head) = ada_metrics_rate_sample_t{timestamp_ns, events, bytes};
    head = (head + 1u) % ADA_METRICS_RATE_HISTORY;
    if (count < ADA_METRICS_RATE_HISTORY) {
        ++count;
    }

    metrics->rate.sample_head = head;
    metrics->rate.sample_count = count;

    uint64_t window_floor = (timestamp_ns > kWindowNs) ? (timestamp_ns - kWindowNs) : 0ull;

    // Drop samples older than window
    while (count > 1u) {
        uint32_t idx = oldest_index(metrics);
        const auto& candidate = sample_at_const(metrics, idx);
        if (candidate.timestamp_ns >= window_floor) {
            break;
        }
        // Drop oldest by decrementing count
        --count;
        metrics->rate.sample_count = count;
    }

    metrics->rate.window_duration_ns = 0;
    metrics->rate.window_events = 0;
    metrics->rate.window_bytes = 0;
    metrics->rate.events_per_second = 0.0;
    metrics->rate.bytes_per_second = 0.0;

    if (count == 0u) {
        return result;
    }

    const uint32_t newest_index = (head + ADA_METRICS_RATE_HISTORY - 1u) % ADA_METRICS_RATE_HISTORY;
    const auto& newest = sample_at_const(metrics, newest_index);
    const auto& oldest = sample_at_const(metrics, oldest_index(metrics));

    if (newest.timestamp_ns <= oldest.timestamp_ns) {
        return result;
    }

    uint64_t delta_ns = newest.timestamp_ns - oldest.timestamp_ns;
    uint64_t delta_events = newest.events - oldest.events;
    uint64_t delta_bytes = newest.bytes - oldest.bytes;

    metrics->rate.window_duration_ns = delta_ns;
    metrics->rate.window_events = delta_events;
    metrics->rate.window_bytes = delta_bytes;

    double denom = static_cast<double>(delta_ns);
    if (denom <= 0.0) {
        return result;
    }

    const double scale = 1000000000.0; // ns -> seconds
    metrics->rate.events_per_second = (delta_events > 0)
        ? (static_cast<double>(delta_events) * scale / denom)
        : 0.0;
    metrics->rate.bytes_per_second = (delta_bytes > 0)
        ? (static_cast<double>(delta_bytes) * scale / denom)
        : 0.0;

    result.events_per_second = metrics->rate.events_per_second;
    result.bytes_per_second = metrics->rate.bytes_per_second;
    result.window_duration_ns = delta_ns;
    result.window_events = delta_events;
    result.window_bytes = delta_bytes;
    return result;
}

} // namespace metrics
} // namespace ada
