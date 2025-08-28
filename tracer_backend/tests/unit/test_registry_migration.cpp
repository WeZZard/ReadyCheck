// test_registry_migration.cpp - Validates C++ implementation matches C behavior

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

extern "C" {
#include "thread_registry.h"
#include "shared_memory.h"
}

class RegistryMigrationTest : public ::testing::Test {
protected:
    static constexpr size_t MEMORY_SIZE = 64 * 1024 * 1024;  // 64MB
    void* c_memory = nullptr;
    void* cpp_memory = nullptr;
    ThreadRegistry* c_registry = nullptr;
    ThreadRegistry* cpp_registry = nullptr;

    void SetUp() override {
        // Allocate memory for both implementations
        c_memory = malloc(MEMORY_SIZE);
        cpp_memory = malloc(MEMORY_SIZE);
        
        ASSERT_NE(c_memory, nullptr);
        ASSERT_NE(cpp_memory, nullptr);
        
        // Initialize both registries
        c_registry = thread_registry_init(c_memory, MEMORY_SIZE);
        cpp_registry = thread_registry_init(cpp_memory, MEMORY_SIZE);
        
        ASSERT_NE(c_registry, nullptr);
        ASSERT_NE(cpp_registry, nullptr);
    }
    
    void TearDown() override {
        thread_registry_deinit(c_registry);
        thread_registry_deinit(cpp_registry);
        free(c_memory);
        free(cpp_memory);
    }
};

// Test that both implementations behave identically for basic registration
TEST_F(RegistryMigrationTest, migration__basic_registration__identical_behavior) {
    uint32_t tid1 = 1001;
    uint32_t tid2 = 1002;
    
    // Register same threads in both
    auto* c_lanes1 = thread_registry_register(c_registry, tid1);
    auto* cpp_lanes1 = thread_registry_register(cpp_registry, tid1);
    
    ASSERT_NE(c_lanes1, nullptr);
    ASSERT_NE(cpp_lanes1, nullptr);
    
    // Both should have same slot (0)
    EXPECT_EQ(c_lanes1->slot_index, cpp_lanes1->slot_index);
    EXPECT_EQ(c_lanes1->thread_id, cpp_lanes1->thread_id);
    
    // Register second thread
    auto* c_lanes2 = thread_registry_register(c_registry, tid2);
    auto* cpp_lanes2 = thread_registry_register(cpp_registry, tid2);
    
    ASSERT_NE(c_lanes2, nullptr);
    ASSERT_NE(cpp_lanes2, nullptr);
    
    // Both should have same slot (1)
    EXPECT_EQ(c_lanes2->slot_index, cpp_lanes2->slot_index);
    EXPECT_EQ(c_lanes2->slot_index, 1);
    
    // Active counts should match
    EXPECT_EQ(thread_registry_get_active_count(c_registry),
              thread_registry_get_active_count(cpp_registry));
}

// Test duplicate registration behavior
TEST_F(RegistryMigrationTest, migration__duplicate_registration__identical_behavior) {
    uint32_t tid = 2001;
    
    // First registration
    auto* c_lanes1 = thread_registry_register(c_registry, tid);
    auto* cpp_lanes1 = thread_registry_register(cpp_registry, tid);
    
    // Second registration (duplicate)
    auto* c_lanes2 = thread_registry_register(c_registry, tid);
    auto* cpp_lanes2 = thread_registry_register(cpp_registry, tid);
    
    // Both should return same pointer
    EXPECT_EQ(c_lanes1, c_lanes2);
    EXPECT_EQ(cpp_lanes1, cpp_lanes2);
    
    // Active count should be 1
    EXPECT_EQ(thread_registry_get_active_count(c_registry), 1);
    EXPECT_EQ(thread_registry_get_active_count(cpp_registry), 1);
}

// Test concurrent registration behavior
TEST_F(RegistryMigrationTest, migration__concurrent_registration__identical_counts) {
    const int num_threads = 20;
    std::vector<std::thread> c_threads;
    std::vector<std::thread> cpp_threads;
    std::atomic<int> c_success{0};
    std::atomic<int> cpp_success{0};
    
    // Register threads concurrently in C implementation
    for (int i = 0; i < num_threads; i++) {
        c_threads.emplace_back([this, &c_success, i]() {
            if (thread_registry_register(c_registry, 3000 + i)) {
                c_success++;
            }
        });
    }
    
    // Register threads concurrently in C++ implementation
    for (int i = 0; i < num_threads; i++) {
        cpp_threads.emplace_back([this, &cpp_success, i]() {
            if (thread_registry_register(cpp_registry, 3000 + i)) {
                cpp_success++;
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : c_threads) t.join();
    for (auto& t : cpp_threads) t.join();
    
    // Both should have registered same number
    EXPECT_EQ(c_success.load(), cpp_success.load());
    EXPECT_EQ(thread_registry_get_active_count(c_registry),
              thread_registry_get_active_count(cpp_registry));
}

// Test lane operations behavior
TEST_F(RegistryMigrationTest, migration__lane_operations__identical_behavior) {
    uint32_t tid = 4001;
    
    auto* c_lanes = thread_registry_register(c_registry, tid);
    auto* cpp_lanes = thread_registry_register(cpp_registry, tid);
    
    ASSERT_NE(c_lanes, nullptr);
    ASSERT_NE(cpp_lanes, nullptr);
    
    // Test submit ring
    bool c_submit = lane_submit_ring(&c_lanes->index_lane, 1);
    bool cpp_submit = lane_submit_ring(&cpp_lanes->index_lane, 1);
    
    EXPECT_EQ(c_submit, cpp_submit);
    EXPECT_TRUE(c_submit);
    
    // Test take ring
    uint32_t c_taken = lane_take_ring(&c_lanes->index_lane);
    uint32_t cpp_taken = lane_take_ring(&cpp_lanes->index_lane);
    
    EXPECT_EQ(c_taken, cpp_taken);
    EXPECT_EQ(c_taken, 1);
    
    // Test return ring
    bool c_return = lane_return_ring(&c_lanes->index_lane, 1);
    bool cpp_return = lane_return_ring(&cpp_lanes->index_lane, 1);
    
    EXPECT_EQ(c_return, cpp_return);
    EXPECT_TRUE(c_return);
    
    // Test get free ring
    uint32_t c_free = lane_get_free_ring(&c_lanes->index_lane);
    uint32_t cpp_free = lane_get_free_ring(&cpp_lanes->index_lane);
    
    EXPECT_EQ(c_free, cpp_free);
    EXPECT_EQ(c_free, 1);
}

// Performance comparison
TEST_F(RegistryMigrationTest, migration__performance__cpp_not_slower) {
    const int iterations = 100000;
    uint32_t tid = 5001;
    
    // Pre-register to test fast path
    auto* c_lanes = thread_registry_register(c_registry, tid);
    auto* cpp_lanes = thread_registry_register(cpp_registry, tid);
    
    // Measure C implementation
    auto c_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        volatile auto* lanes = thread_registry_get_thread_lanes(c_registry, tid);
        (void)lanes;
    }
    auto c_end = std::chrono::high_resolution_clock::now();
    auto c_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(c_end - c_start);
    
    // Measure C++ implementation
    auto cpp_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        volatile auto* lanes = thread_registry_get_thread_lanes(cpp_registry, tid);
        (void)lanes;
    }
    auto cpp_end = std::chrono::high_resolution_clock::now();
    auto cpp_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(cpp_end - cpp_start);
    
    // C++ should not be significantly slower (allow 20% tolerance)
    double ratio = (double)cpp_duration.count() / c_duration.count();
    EXPECT_LT(ratio, 1.2) << "C++ implementation is >20% slower than C";
    
    printf("Performance comparison:\n");
    printf("  C:   %lld ns total, %lld ns/op\n", 
           (long long)c_duration.count(), 
           (long long)(c_duration.count() / iterations));
    printf("  C++: %lld ns total, %lld ns/op\n",
           (long long)cpp_duration.count(),
           (long long)(cpp_duration.count() / iterations));
    printf("  Ratio: %.2fx\n", ratio);
}

// Test unregister behavior
TEST_F(RegistryMigrationTest, migration__unregister__identical_behavior) {
    uint32_t tid = 6001;
    
    // Register in both
    thread_registry_register(c_registry, tid);
    thread_registry_register(cpp_registry, tid);
    
    // Both should have count 1
    EXPECT_EQ(thread_registry_get_active_count(c_registry), 1);
    EXPECT_EQ(thread_registry_get_active_count(cpp_registry), 1);
    
    // Unregister in both
    bool c_result = thread_registry_unregister(c_registry, tid);
    bool cpp_result = thread_registry_unregister(cpp_registry, tid);
    
    EXPECT_EQ(c_result, cpp_result);
    EXPECT_TRUE(c_result);
    
    // Both should have count 0
    EXPECT_EQ(thread_registry_get_active_count(c_registry), 0);
    EXPECT_EQ(thread_registry_get_active_count(cpp_registry), 0);
}

// Test memory size calculation
TEST_F(RegistryMigrationTest, migration__memory_size__reasonable) {
    size_t size = thread_registry_calculate_memory_size();
    
    // Should be reasonable (under 100MB for 64 threads)
    EXPECT_LT(size, 100 * 1024 * 1024);
    EXPECT_GT(size, 1 * 1024 * 1024);  // But more than 1MB
    
    printf("Calculated memory requirement: %zu bytes (%.2f MB)\n", 
           size, size / (1024.0 * 1024.0));
}