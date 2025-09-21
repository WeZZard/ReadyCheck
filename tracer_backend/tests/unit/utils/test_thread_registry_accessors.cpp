// Test thread_registry.cpp accessor functions (lines 509-519)
#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/metrics/thread_metrics.h>
}

#include <vector>

class ThreadRegistryAccessors : public ::testing::Test {
protected:
    void SetUp() override {
        mem_size_ = thread_registry_calculate_memory_size_with_capacity(4);
        storage_.resize(mem_size_, 0);
        registry_ = thread_registry_init_with_capacity(storage_.data(), storage_.size(), 4);
        ASSERT_NE(registry_, nullptr);
    }

    size_t mem_size_;
    std::vector<uint8_t> storage_;
    ThreadRegistry* registry_;
};

// Test thread_registry_get_capacity (lines 509-513)
TEST_F(ThreadRegistryAccessors, GetCapacity) {
    // Test with valid registry
    uint32_t capacity = thread_registry_get_capacity(registry_);
    EXPECT_EQ(capacity, 4u);

    // Test with null registry
    capacity = thread_registry_get_capacity(nullptr);
    EXPECT_EQ(capacity, 0u);
}

// Test thread_registry_get_active_count (lines 515-519)
TEST_F(ThreadRegistryAccessors, GetActiveCount) {
    // Test with empty registry
    uint32_t count = thread_registry_get_active_count(registry_);
    EXPECT_EQ(count, 0u);

    // Register one thread
    ThreadLaneSet* lanes1 = thread_registry_register(registry_, 101);
    ASSERT_NE(lanes1, nullptr);
    count = thread_registry_get_active_count(registry_);
    EXPECT_EQ(count, 1u);

    // Register another thread
    ThreadLaneSet* lanes2 = thread_registry_register(registry_, 102);
    ASSERT_NE(lanes2, nullptr);
    count = thread_registry_get_active_count(registry_);
    EXPECT_EQ(count, 2u);

    // Unregister one thread
    thread_registry_unregister(lanes1);
    count = thread_registry_get_active_count(registry_);
    EXPECT_EQ(count, 1u);

    // Test with null registry
    count = thread_registry_get_active_count(nullptr);
    EXPECT_EQ(count, 0u);
}

// Test both accessors together
TEST_F(ThreadRegistryAccessors, CombinedAccessorUsage) {
    // Get initial state
    uint32_t capacity = thread_registry_get_capacity(registry_);
    uint32_t active = thread_registry_get_active_count(registry_);
    EXPECT_EQ(capacity, 4u);
    EXPECT_EQ(active, 0u);

    // Fill up the registry
    std::vector<ThreadLaneSet*> lanes_vec;
    for (uint32_t i = 0; i < capacity; ++i) {
        ThreadLaneSet* lanes = thread_registry_register(registry_, 1000 + i);
        if (lanes) {
            lanes_vec.push_back(lanes);
        }
    }

    // Should have filled capacity
    active = thread_registry_get_active_count(registry_);
    EXPECT_EQ(active, capacity);

    // Try to register one more (should fail)
    ThreadLaneSet* overflow = thread_registry_register(registry_, 9999);
    EXPECT_EQ(overflow, nullptr);

    // Count should remain the same
    active = thread_registry_get_active_count(registry_);
    EXPECT_EQ(active, capacity);

    // Unregister all
    for (auto* lanes : lanes_vec) {
        thread_registry_unregister(lanes);
    }

    // Should be empty again
    active = thread_registry_get_active_count(registry_);
    EXPECT_EQ(active, 0u);
}

// Test thread_lanes accessor functions for coverage
TEST_F(ThreadRegistryAccessors, ThreadLanesAccessors) {
    // Register a thread to get lanes
    ThreadLaneSet* lanes = thread_registry_register(registry_, 5001);
    ASSERT_NE(lanes, nullptr);

    // Test thread_lanes_get_slot_index
    uint32_t slot = thread_lanes_get_slot_index(lanes);
    EXPECT_LT(slot, 64u);  // Should be less than max slots

    // Test thread_lanes_get_thread_id
    uint64_t tid = thread_lanes_get_thread_id(lanes);
    EXPECT_EQ(tid, 5001u);

    // Test with null (for coverage of error paths)
    EXPECT_EQ(thread_lanes_get_slot_index(nullptr), 0u);
    EXPECT_EQ(thread_lanes_get_thread_id(nullptr), 0u);

    // Cleanup
    thread_registry_unregister(lanes);
}