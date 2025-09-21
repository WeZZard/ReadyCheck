#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/metrics/thread_metrics.h>
#include <tracer_backend/metrics/global_metrics.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/tracer_types.h>
}

#include <array>
#include <vector>

#include "thread_metrics_private.h"

namespace {

TEST(ThreadMetrics, InitZeroesCounters) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 42, 3);

    EXPECT_EQ(metrics.thread_id, 42u);
    EXPECT_EQ(metrics.slot_index, 3u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_written, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_dropped, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.bytes_written, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(metrics.rate.sample_count, 0u);
}

TEST(ThreadMetrics, RecordEventWrittenUpdatesCounters) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 1, 0);

    ada_thread_metrics_record_event_written(&metrics, 128);
    ada_thread_metrics_record_event_written(&metrics, 256);

    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_written, ADA_MEMORY_ORDER_RELAXED), 2u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.bytes_written, ADA_MEMORY_ORDER_RELAXED), 384u);
}

TEST(ThreadMetrics, UpdateRateMaintainsSlidingWindow) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 1, 0);

    ada_thread_metrics_update_rate(&metrics, 0, 0, 0);
    ADA_ATOMIC_STORE(metrics.counters.events_written, 100, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.counters.bytes_written, 1000, ADA_MEMORY_ORDER_RELAXED);
    ada_thread_metrics_update_rate(&metrics, ADA_METRICS_WINDOW_NS, 100, 1000);

    EXPECT_NEAR(metrics.rate.events_per_second, 1000.0, 1e-6);
    EXPECT_NEAR(metrics.rate.bytes_per_second, 10000.0, 1e-6);

    // Advance beyond window and ensure old sample is dropped
    ADA_ATOMIC_STORE(metrics.counters.events_written, 150, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.counters.bytes_written, 1500, ADA_MEMORY_ORDER_RELAXED);
    ada_thread_metrics_update_rate(&metrics, ADA_METRICS_WINDOW_NS * 2, 150, 1500);

    EXPECT_NEAR(metrics.rate.events_per_second, 500.0, 1e-6);
    EXPECT_NEAR(metrics.rate.bytes_per_second, 5000.0, 1e-6);
}

TEST(ThreadMetrics, SwapEndUpdatesDuration) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 11, 2);

    ada_metrics_swap_token_t token = ada_thread_metrics_swap_begin(&metrics, 100);
    ada_thread_metrics_swap_end(&token, 250, 4);

    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.swaps.swap_count, ADA_MEMORY_ORDER_RELAXED), 1u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.swaps.total_swap_duration_ns, ADA_MEMORY_ORDER_RELAXED), 150u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.swaps.rings_in_rotation, ADA_MEMORY_ORDER_RELAXED), 4u);
}

TEST(GlobalMetrics, CollectAggregatesCounters) {
    constexpr uint32_t kCapacity = 2;
    size_t mem_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> storage(mem_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(storage.data(), storage.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ThreadLaneSet* lanes1 = thread_registry_register(registry, 101);
    ThreadLaneSet* lanes2 = thread_registry_register(registry, 202);
    ASSERT_NE(lanes1, nullptr);
    ASSERT_NE(lanes2, nullptr);

    ada_thread_metrics_t* metrics1 = thread_lanes_get_metrics(lanes1);
    ada_thread_metrics_t* metrics2 = thread_lanes_get_metrics(lanes2);
    ASSERT_NE(metrics1, nullptr);
    ASSERT_NE(metrics2, nullptr);

    ADA_ATOMIC_STORE(metrics1->counters.events_written, 50, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics1->counters.bytes_written, 5000, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics2->counters.events_written, 30, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics2->counters.bytes_written, 3000, ADA_MEMORY_ORDER_RELAXED);

    std::array<ada_thread_metrics_snapshot_t, MAX_THREADS> buffer{};
    ada_global_metrics_t global;
    ASSERT_TRUE(ada_global_metrics_init(&global, buffer.data(), buffer.size()));

    EXPECT_TRUE(ada_global_metrics_collect(&global, registry, ADA_METRICS_WINDOW_NS));

    // Update counters to simulate activity and collect again to produce rates
    ADA_ATOMIC_STORE(metrics1->counters.events_written, 100, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics1->counters.bytes_written, 10000, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics2->counters.events_written, 80, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics2->counters.bytes_written, 8000, ADA_MEMORY_ORDER_RELAXED);

    EXPECT_TRUE(ada_global_metrics_collect(&global, registry, ADA_METRICS_WINDOW_NS * 2));

    size_t snapshot_count = ada_global_metrics_snapshot_count(&global);
    EXPECT_EQ(snapshot_count, 2u);

    const ada_thread_metrics_snapshot_t* snaps = ada_global_metrics_snapshot_data(&global);
    ASSERT_NE(snaps, nullptr);

    ada_global_metrics_totals_t totals = ada_global_metrics_get_totals(&global);
    EXPECT_EQ(totals.total_events_written, 180u);
    EXPECT_EQ(totals.total_bytes_written, 18000u);
    EXPECT_EQ(totals.active_thread_count, 2u);

    // Rates should now be non-zero after second sample
    bool any_rate_positive = false;
    for (size_t i = 0; i < snapshot_count; ++i) {
        any_rate_positive |= snaps[i].events_per_second > 0.0;
    }
    EXPECT_TRUE(any_rate_positive);

    thread_registry_deinit(registry);
}

TEST(MetricsTaskThreadMetrics, Reset__thenZeroesAllState) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 123, 7);

    metrics.thread_id = 555u;
    metrics.slot_index = 42u;
    ADA_ATOMIC_STORE(metrics.counters.events_written, 99, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.counters.events_dropped, 12, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.counters.events_filtered, 3, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.counters.bytes_written, 1024, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.pressure.ring_full_count, 8, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.swaps.swap_count, 4, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.swaps.rings_in_rotation, 9, ADA_MEMORY_ORDER_RELAXED);
    metrics.rate.events_per_second = 123.0;
    metrics.rate.bytes_per_second = 456.0;

    ada_thread_metrics_reset(&metrics);

    EXPECT_EQ(metrics.thread_id, 0u);
    EXPECT_EQ(metrics.slot_index, 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_written, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_dropped, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_filtered, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.bytes_written, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.pressure.ring_full_count, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.swaps.swap_count, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.swaps.rings_in_rotation, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(metrics.rate.events_per_second, 0.0);
    EXPECT_EQ(metrics.rate.bytes_per_second, 0.0);
}

TEST(MetricsTaskThreadMetrics, RecordEventsWrittenBulk__thenSelectiveIncrements) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 1, 0);

    ada_thread_metrics_record_events_written_bulk(&metrics, 0, 64);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_written, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.bytes_written, ADA_MEMORY_ORDER_RELAXED), 64u);

    ada_thread_metrics_record_events_written_bulk(&metrics, 3, 0);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_written, ADA_MEMORY_ORDER_RELAXED), 3u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.bytes_written, ADA_MEMORY_ORDER_RELAXED), 64u);

    ada_thread_metrics_record_events_written_bulk(&metrics, 2, 128);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_written, ADA_MEMORY_ORDER_RELAXED), 5u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.bytes_written, ADA_MEMORY_ORDER_RELAXED), 192u);
}

TEST(MetricsTaskThreadMetrics, RecordPressureSignals__thenCountersIncrement) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 1, 0);

    ada_thread_metrics_record_event_dropped(&metrics);
    ada_thread_metrics_record_event_filtered(&metrics);
    ada_thread_metrics_record_ring_full(&metrics);
    ada_thread_metrics_record_pool_exhaustion(&metrics);
    ada_thread_metrics_record_allocation_failure(&metrics);

    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_dropped, ADA_MEMORY_ORDER_RELAXED), 1u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.counters.events_filtered, ADA_MEMORY_ORDER_RELAXED), 1u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.pressure.ring_full_count, ADA_MEMORY_ORDER_RELAXED), 1u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.pressure.pool_exhaustion_count, ADA_MEMORY_ORDER_RELAXED), 1u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.pressure.allocation_failures, ADA_MEMORY_ORDER_RELAXED), 1u);
}

TEST(MetricsTaskThreadMetrics, ObserveQueueDepth__thenTracksMaximum) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 1, 0);

    ADA_ATOMIC_STORE(metrics.pressure.max_queue_depth, 8, ADA_MEMORY_ORDER_RELAXED);
    ada_thread_metrics_observe_queue_depth(&metrics, 3);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.pressure.max_queue_depth, ADA_MEMORY_ORDER_RELAXED), 8u);

    ada_thread_metrics_observe_queue_depth(&metrics, 42);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.pressure.max_queue_depth, ADA_MEMORY_ORDER_RELAXED), 42u);
}

TEST(MetricsTaskThreadMetrics, SwapEnd__withOutOfOrderTimestamp__thenClampsDuration) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 10, 2);

    ada_metrics_swap_token_t token = ada_thread_metrics_swap_begin(&metrics, 500);
    ada_thread_metrics_swap_end(&token, 400, 6);

    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.swaps.swap_count, ADA_MEMORY_ORDER_RELAXED), 1u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.swaps.total_swap_duration_ns, ADA_MEMORY_ORDER_RELAXED), 0u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.swaps.last_swap_timestamp_ns, ADA_MEMORY_ORDER_RELAXED), 500u);
    EXPECT_EQ(ADA_ATOMIC_LOAD(metrics.swaps.rings_in_rotation, ADA_MEMORY_ORDER_RELAXED), 6u);
}

TEST(MetricsTaskThreadMetrics, UpdateRate__nullMetrics__thenNoCrash) {
    ada_thread_metrics_update_rate(nullptr, 0, 0, 0);
    SUCCEED();
}

TEST(MetricsTaskRateCalculator, NonMonotonicTimestamp__thenProducesZeroRates) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 1, 0);

    ada::metrics::RateResult first = ada::metrics::rate_calculator_sample(&metrics, 100, 10, 100);
    EXPECT_DOUBLE_EQ(first.events_per_second, 0.0);
    EXPECT_DOUBLE_EQ(first.bytes_per_second, 0.0);

    ada::metrics::RateResult second = ada::metrics::rate_calculator_sample(&metrics, 50, 5, 50);
    EXPECT_DOUBLE_EQ(second.events_per_second, 0.0);
    EXPECT_DOUBLE_EQ(second.bytes_per_second, 0.0);
    EXPECT_EQ(metrics.rate.window_duration_ns, 0u);
    EXPECT_EQ(metrics.rate.window_events, 0u);
    EXPECT_EQ(metrics.rate.window_bytes, 0u);
}

TEST(MetricsTaskRateCalculator, EvictsSamplesOutsideWindow__thenMaintainsBoundedHistory) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 1, 0);

    ada::metrics::rate_calculator_sample(&metrics, 0, 0, 0);
    ada::metrics::rate_calculator_sample(&metrics, ADA_METRICS_WINDOW_NS / 2, 50, 500);
    ada::metrics::RateResult result =
        ada::metrics::rate_calculator_sample(&metrics, ADA_METRICS_WINDOW_NS + 10, 150, 1500);

    EXPECT_EQ(metrics.rate.sample_count, 2u);
    EXPECT_GT(metrics.rate.window_duration_ns, ADA_METRICS_WINDOW_NS / 2);
    EXPECT_GT(result.events_per_second, 0.0);
    EXPECT_GT(result.bytes_per_second, 0.0);
}

TEST(MetricsTaskRateCalculator, NullMetrics__thenReturnsDefaultResult) {
    ada::metrics::RateResult result = ada::metrics::rate_calculator_sample(nullptr, 1, 1, 1);
    EXPECT_DOUBLE_EQ(result.events_per_second, 0.0);
    EXPECT_DOUBLE_EQ(result.bytes_per_second, 0.0);
    EXPECT_EQ(result.window_duration_ns, 0u);
    EXPECT_EQ(result.window_events, 0u);
    EXPECT_EQ(result.window_bytes, 0u);
}

TEST(MetricsTaskThreadMetricsSnapshot, Capture__thenPopulatesDerivedFields) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 77, 5);

    ADA_ATOMIC_STORE(metrics.counters.events_written, 90, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.counters.events_dropped, 10, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.counters.bytes_written, 2048, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics.pressure.max_queue_depth, 16, ADA_MEMORY_ORDER_RELAXED);
    ada_metrics_swap_token_t token = ada_thread_metrics_swap_begin(&metrics, 1000);
    ada_thread_metrics_swap_end(&token, 1100, 3);
    metrics.rate.events_per_second = 123.0;
    metrics.rate.bytes_per_second = 456.0;

    ada_thread_metrics_snapshot_t snapshot{};
    ada_thread_metrics_snapshot_capture(&metrics, 5000, &snapshot);
    EXPECT_EQ(snapshot.thread_id, 77u);
    EXPECT_EQ(snapshot.slot_index, 5u);
    EXPECT_EQ(snapshot.timestamp_ns, 5000u);
    EXPECT_EQ(snapshot.events_written, 90u);
    EXPECT_NEAR(snapshot.drop_rate_percent, (10.0 * 100.0) / 100.0, 1e-9);
    EXPECT_EQ(snapshot.avg_swap_duration_ns, 100u);

    ada_thread_metrics_snapshot_apply_rates(&snapshot, 10.0, 20.0);
    EXPECT_DOUBLE_EQ(snapshot.events_per_second, 10.0);
    EXPECT_DOUBLE_EQ(snapshot.bytes_per_second, 20.0);

    ada_thread_metrics_snapshot_set_swap_rate(&snapshot, 3.5);
    EXPECT_DOUBLE_EQ(snapshot.swaps_per_second, 3.5);
}

TEST(MetricsTaskThreadMetricsSnapshot, CaptureZeroTotals__thenDropRateZero) {
    ada_thread_metrics_t metrics;
    ada_thread_metrics_init(&metrics, 1, 0);

    ada_thread_metrics_snapshot_t snapshot{};
    ada_thread_metrics_snapshot_capture(&metrics, 123, &snapshot);
    EXPECT_DOUBLE_EQ(snapshot.drop_rate_percent, 0.0);
}

TEST(MetricsTaskThreadMetricsSnapshot, NullInputs__thenNoCrash) {
    ada_thread_metrics_snapshot_capture(nullptr, 0, nullptr);
    ada_thread_metrics_snapshot_apply_rates(nullptr, 1.0, 2.0);
    ada_thread_metrics_snapshot_set_swap_rate(nullptr, 1.0);
    SUCCEED();
}

TEST(MetricsTaskGlobalMetrics, InitValidation__thenRejectsBadArgs) {
    ada_thread_metrics_snapshot_t buffer[1]{};
    ada_global_metrics_t global;
    EXPECT_FALSE(ada_global_metrics_init(nullptr, buffer, 1));
    EXPECT_FALSE(ada_global_metrics_init(&global, nullptr, 1));
    EXPECT_FALSE(ada_global_metrics_init(&global, buffer, 0));
}

TEST(MetricsTaskGlobalMetrics, Reset__thenRestoresDefaults) {
    std::array<ada_thread_metrics_snapshot_t, MAX_THREADS> buffer{};
    ada_global_metrics_t global{};
    ASSERT_TRUE(ada_global_metrics_init(&global, buffer.data(), buffer.size()));

    global.totals.total_events_written = 100u;
    global.rates.system_events_per_second = 5.0;
    global.control.collection_interval_ns.store(1, ADA_MEMORY_ORDER_RELAXED);
    global.control.collection_enabled.store(false, ADA_MEMORY_ORDER_RELAXED);
    global.snapshot_count.store(3u, ADA_MEMORY_ORDER_RELAXED);

    ada_global_metrics_reset(&global);

    EXPECT_EQ(global.snapshots, buffer.data());
    EXPECT_EQ(global.snapshot_capacity, buffer.size());
    EXPECT_EQ(global.totals.total_events_written, 0u);
    EXPECT_EQ(global.rates.system_events_per_second, 0.0);
    EXPECT_EQ(global.control.collection_interval_ns.load(ADA_MEMORY_ORDER_RELAXED),
              static_cast<uint64_t>(ADA_METRICS_WINDOW_NS));
    EXPECT_TRUE(global.control.collection_enabled.load(ADA_MEMORY_ORDER_RELAXED));
    EXPECT_EQ(global.snapshot_count.load(ADA_MEMORY_ORDER_RELAXED), 0u);
}

TEST(MetricsTaskGlobalMetrics, SetInterval__thenIgnoresZero) {
    std::array<ada_thread_metrics_snapshot_t, MAX_THREADS> buffer{};
    ada_global_metrics_t global{};
    ASSERT_TRUE(ada_global_metrics_init(&global, buffer.data(), buffer.size()));

    uint64_t original = global.control.collection_interval_ns.load(ADA_MEMORY_ORDER_RELAXED);
    ada_global_metrics_set_interval(&global, 0);
    EXPECT_EQ(global.control.collection_interval_ns.load(ADA_MEMORY_ORDER_RELAXED), original);

    ada_global_metrics_set_interval(&global, 123456u);
    EXPECT_EQ(global.control.collection_interval_ns.load(ADA_MEMORY_ORDER_RELAXED), 123456u);
}

TEST(MetricsTaskGlobalMetrics, Collect__thenHonorsDisableAndTiming) {
    constexpr uint32_t kCapacity = 1;
    size_t mem_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> storage(mem_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(storage.data(), storage.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ThreadLaneSet* lanes = thread_registry_register(registry, 303u);
    ASSERT_NE(lanes, nullptr);

    std::array<ada_thread_metrics_snapshot_t, MAX_THREADS> buffer{};
    ada_global_metrics_t global{};
    ASSERT_TRUE(ada_global_metrics_init(&global, buffer.data(), buffer.size()));

    global.control.collection_enabled.store(false, ADA_MEMORY_ORDER_RELAXED);
    EXPECT_FALSE(ada_global_metrics_collect(&global, registry, 10));

    global.control.collection_enabled.store(true, ADA_MEMORY_ORDER_RELAXED);
    EXPECT_TRUE(ada_global_metrics_collect(&global, registry, ADA_METRICS_WINDOW_NS));
    EXPECT_FALSE(ada_global_metrics_collect(&global, registry, ADA_METRICS_WINDOW_NS + 1));

    thread_registry_deinit(registry);
}

TEST(MetricsTaskGlobalMetrics, Collect__nullInputs__thenReturnsFalse) {
    std::array<ada_thread_metrics_snapshot_t, MAX_THREADS> buffer{};
    ada_global_metrics_t global{};
    ASSERT_TRUE(ada_global_metrics_init(&global, buffer.data(), buffer.size()));

    EXPECT_FALSE(ada_global_metrics_collect(nullptr, reinterpret_cast<ThreadRegistry*>(0x1), 0));
    EXPECT_FALSE(ada_global_metrics_collect(&global, nullptr, 0));
}

TEST(MetricsTaskGlobalMetrics, Collect__noThreads__thenResetsRates) {
    constexpr uint32_t kCapacity = 1;
    size_t mem_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> storage(mem_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(storage.data(), storage.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    std::array<ada_thread_metrics_snapshot_t, MAX_THREADS> buffer{};
    ada_global_metrics_t global{};
    ASSERT_TRUE(ada_global_metrics_init(&global, buffer.data(), buffer.size()));

    EXPECT_TRUE(ada_global_metrics_collect(&global, registry, ADA_METRICS_WINDOW_NS));
    EXPECT_EQ(ada_global_metrics_snapshot_count(&global), 0u);
    ada_global_metrics_rates_t rates = ada_global_metrics_get_rates(&global);
    EXPECT_EQ(rates.system_events_per_second, 0.0);
    EXPECT_EQ(rates.system_bytes_per_second, 0.0);

    thread_registry_deinit(registry);
}

TEST(MetricsTaskGlobalMetrics, SnapshotHelpers__nullInputs__thenSafeDefaults) {
    EXPECT_EQ(ada_global_metrics_snapshot_count(nullptr), 0u);
    EXPECT_EQ(ada_global_metrics_snapshot_data(nullptr), nullptr);

    ada_global_metrics_totals_t totals = ada_global_metrics_get_totals(nullptr);
    EXPECT_EQ(totals.total_events_written, 0u);

    ada_global_metrics_rates_t rates = ada_global_metrics_get_rates(nullptr);
    EXPECT_EQ(rates.system_events_per_second, 0.0);
}

TEST(MetricsTaskGlobalMetrics, Collect__limitedCapacity__thenTruncatesSnapshots) {
    constexpr uint32_t kCapacity = 2;
    size_t mem_size = thread_registry_calculate_memory_size_with_capacity(kCapacity);
    std::vector<uint8_t> storage(mem_size, 0);
    ThreadRegistry* registry = thread_registry_init_with_capacity(storage.data(), storage.size(), kCapacity);
    ASSERT_NE(registry, nullptr);

    ThreadLaneSet* lanes1 = thread_registry_register(registry, 111u);
    ThreadLaneSet* lanes2 = thread_registry_register(registry, 222u);
    ASSERT_NE(lanes1, nullptr);
    ASSERT_NE(lanes2, nullptr);

    ada_thread_metrics_t* metrics1 = thread_lanes_get_metrics(lanes1);
    ada_thread_metrics_t* metrics2 = thread_lanes_get_metrics(lanes2);
    ADA_ATOMIC_STORE(metrics1->counters.events_written, 10, ADA_MEMORY_ORDER_RELAXED);
    ADA_ATOMIC_STORE(metrics2->counters.events_written, 20, ADA_MEMORY_ORDER_RELAXED);

    std::array<ada_thread_metrics_snapshot_t, 1> buffer{};
    ada_global_metrics_t global{};
    ASSERT_TRUE(ada_global_metrics_init(&global, buffer.data(), buffer.size()));

    EXPECT_TRUE(ada_global_metrics_collect(&global, registry, ADA_METRICS_WINDOW_NS));
    EXPECT_EQ(ada_global_metrics_snapshot_count(&global), 1u);

    ada_global_metrics_totals_t totals = ada_global_metrics_get_totals(&global);
    EXPECT_LE(totals.total_events_written, 30u);
    EXPECT_EQ(totals.active_thread_count, 1u);

    thread_registry_deinit(registry);
}

} // namespace
