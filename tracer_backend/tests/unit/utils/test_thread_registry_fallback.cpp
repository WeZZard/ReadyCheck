#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/utils/shared_memory.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/utils/shm_directory.h>
}

#include <cstdint>

// Validate that lane operations fall back to local base when SHM directory
// is not mapped (shm_dir_get_base(0) == NULL). This exercises the branch
// in thread_registry.cpp computing pool_base from the registry pointer.
TEST(thread_registry__fallback_without_shm_dir__then_uses_local_base, unit) {
    // Ensure no local bases are mapped so shm_dir_get_base(0) returns NULL
    shm_dir_clear_local_bases();

    uint32_t pid = shared_memory_get_pid();
    uint32_t sid = shared_memory_get_session_id();
    size_t reg_size = thread_registry_calculate_memory_size_with_capacity(MAX_THREADS);

    SharedMemoryRef reg = shared_memory_create_unique(ADA_ROLE_REGISTRY, pid, sid, reg_size, nullptr, 0);
    ASSERT_NE(reg, nullptr);

    void* reg_addr = shared_memory_get_address(reg);
    ASSERT_NE(reg_addr, nullptr);

    ThreadRegistry* registry = thread_registry_init(reg_addr, reg_size);
    ASSERT_NE(registry, nullptr);

    // Register a thread and get its index lane
    ThreadLaneSet* lanes = thread_registry_register(registry, (uintptr_t)pthread_self());
    ASSERT_NE(lanes, nullptr);
    Lane* idx_lane = thread_lanes_get_index_lane(lanes);
    ASSERT_NE(idx_lane, nullptr);

    // Get a free ring via the C API. This should use fallback base computation.
    uint32_t ring = lane_get_free_ring(idx_lane);
    EXPECT_NE(ring, UINT32_MAX);
    EXPECT_EQ(ring, 1u);

    // Return the ring; also uses fallback base computation.
    EXPECT_TRUE(lane_return_ring(idx_lane, ring));

    // No explicit deinit; memory is in SHM for the test process lifetime.
    shared_memory_destroy(reg);
}

