#ifndef TRACER_BACKEND_SRC_METRICS_THREAD_METRICS_PRIVATE_H
#define TRACER_BACKEND_SRC_METRICS_THREAD_METRICS_PRIVATE_H

#include <cstddef>
#include <cstdint>

#include <tracer_backend/metrics/thread_metrics.h>

namespace ada {
namespace metrics {

struct RateResult {
    double events_per_second{0.0};
    double bytes_per_second{0.0};
    uint64_t window_duration_ns{0};
    uint64_t window_events{0};
    uint64_t window_bytes{0};
};

RateResult rate_calculator_sample(ada_thread_metrics_t* metrics,
                                  uint64_t timestamp_ns,
                                  uint64_t events,
                                  uint64_t bytes);

}
} // namespace ada::metrics

#endif // TRACER_BACKEND_SRC_METRICS_THREAD_METRICS_PRIVATE_H
