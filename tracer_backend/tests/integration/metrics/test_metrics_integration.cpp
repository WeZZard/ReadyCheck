#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/metrics/global_metrics.h>
#include <tracer_backend/metrics/thread_metrics.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/tracer_types.h>
}

#include <array>
#include <vector>

#include "thread_metrics_private.h"
#include "thread_registry_private.h"

namespace {

TEST(MetricsTaskIntegration, GlobalMetricsCollect__observesQueueDepthAndSwapRates__thenAggregates) {
    constexpr uint32_t kCapacity = 1;
    size_t mem_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> storage(mem_size, 0);
    ThreadRegistry* registry =
        thread_registry_init_with_capacity(storage.data(), storage.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ThreadLaneSet* lanes = thread_registry_register(registry, 0xABCu);
    ASSERT_NE(lanes, nullptr);

    ada_thread_metrics_t* metrics = thread_lanes_get_metrics(lanes);
    ASSERT_NE(metrics, nullptr);

    auto* cpp_lanes = ada::internal::to_cpp(lanes);

    std::array<ada_thread_metrics_snapshot_t, MAX_THREADS> buffer{};
    ada_global_metrics_t global{};
    ASSERT_TRUE(ada_global_metrics_init(&global, buffer.data(), buffer.size()));

    ADA_ATOMIC_STORE(metrics->counters.events_written, 40, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 4000, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.events_dropped, 5, ADA_MEMORY_ORDER_RELAXED);

    auto warm_token = ada_thread_metrics_swap_begin(metrics, 1'000'000u);
    ada_thread_metrics_swap_end(&warm_token, 1'000'200u, 2);

    cpp_lanes->index_lane.submit_head.store(10u, std::memory_order_release);
    cpp_lanes->index_lane.submit_tail.store(30u, std::memory_order_release);
    cpp_lanes->detail_lane.submit_head.store(0u, std::memory_order_release);
    cpp_lanes->detail_lane.submit_tail.store(5u, std::memory_order_release);

    uint64_t now1 = 1'500'000u;
    EXPECT_TRUE(ada_global_metrics_collect(&global, registry, now1));
    EXPECT_EQ(ada_global_metrics_snapshot_count(&global), 1u);

    const ada_thread_metrics_snapshot_t* snaps = ada_global_metrics_snapshot_data(&global);
    ASSERT_NE(snaps, nullptr);
    EXPECT_EQ(snaps[0].max_queue_depth, 25u);
    EXPECT_DOUBLE_EQ(snaps[0].swaps_per_second, 0.0);

    ada_global_metrics_totals_t totals1 = ada_global_metrics_get_totals(&global);
    EXPECT_EQ(totals1.total_events_written, snaps[0].events_written);
    EXPECT_EQ(totals1.total_events_dropped, snaps[0].events_dropped);
    EXPECT_EQ(totals1.active_thread_count, 1u);

    ADA_ATOMIC_STORE(metrics->counters.events_written, 140, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.bytes_written, 9400, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics->counters.events_dropped, 7, ADA_MEMORY_ORDER_RELAXED);

    auto second_token = ada_thread_metrics_swap_begin(metrics, now1 + 1000u);
    ada_thread_metrics_swap_end(&second_token, now1 + 1200u, 3);

    cpp_lanes->index_lane.submit_head.store(900u, std::memory_order_release);
    cpp_lanes->index_lane.submit_tail.store(100u, std::memory_order_release);
    cpp_lanes->detail_lane.submit_head.store(200u, std::memory_order_release);
    cpp_lanes->detail_lane.submit_tail.store(260u, std::memory_order_release);

    uint64_t now2 = now1 + ADA_METRICS_WINDOW_NS;
    EXPECT_TRUE(ada_global_metrics_collect(&global, registry, now2));
    EXPECT_EQ(ada_global_metrics_snapshot_count(&global), 1u);

    snaps = ada_global_metrics_snapshot_data(&global);
    ASSERT_NE(snaps, nullptr);

    EXPECT_GT(snaps[0].events_per_second, 0.0);
    EXPECT_GT(snaps[0].bytes_per_second, 0.0);
    EXPECT_GT(snaps[0].swaps_per_second, 0.0);
    EXPECT_EQ(snaps[0].max_queue_depth, 284u);

    ada_global_metrics_totals_t totals2 = ada_global_metrics_get_totals(&global);
    EXPECT_EQ(totals2.total_events_written, snaps[0].events_written);
    EXPECT_EQ(totals2.total_bytes_written, snaps[0].bytes_written);
    EXPECT_EQ(totals2.total_events_dropped, snaps[0].events_dropped);

    ada_global_metrics_rates_t rates = ada_global_metrics_get_rates(&global);
    EXPECT_EQ(rates.system_events_per_second, snaps[0].events_per_second);
    EXPECT_EQ(rates.system_bytes_per_second, snaps[0].bytes_per_second);
    EXPECT_EQ(rates.last_window_ns, metrics->rate.window_duration_ns);

    thread_registry_deinit(registry);
}

} // namespace
