#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/utils/shared_memory.h>
#include <tracer_backend/utils/tracer_types.h>
#include <tracer_backend/utils/shm_directory.h>
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/ada/thread.h>
#include <tracer_backend/utils/ring_buffer.h>
}

#include <cstring>

// Helper: make a minimal directory for the registry arena
static void make_registry_directory(ShmDirectory* dir, SharedMemoryRef shm_reg, size_t size) {
    dir->schema_version = 1;
    dir->count = 1;
    auto* e0 = &dir->entries[0];
    std::memset(e0->name, 0, sizeof(e0->name));
    const char* n = shared_memory_get_name(shm_reg);
    if (n && n[0] != '\0') {
        // shared_memory provides the correct POSIX name already
        std::strncpy(e0->name, n, sizeof(e0->name) - 1);
    }
    e0->size = (uint64_t)size;
}

// 1) directory__published__then_indices_stable
TEST(shm_directory__published__then_indices_stable, unit) {
    uint32_t pid = shared_memory_get_pid();
    uint32_t sid = shared_memory_get_session_id();
    size_t reg_size = thread_registry_calculate_memory_size_with_capacity(MAX_THREADS);

    SharedMemoryRef reg = shared_memory_create_unique(ADA_ROLE_REGISTRY, pid, sid, reg_size, nullptr, 0);
    ASSERT_NE(reg, nullptr);

    ControlBlock cb = {};
    make_registry_directory(&cb.shm_directory, reg, reg_size);

    EXPECT_EQ(cb.shm_directory.schema_version, 1u);
    EXPECT_EQ(cb.shm_directory.count, 1u);
    EXPECT_GT(std::strlen(cb.shm_directory.entries[0].name), 0u);
    EXPECT_EQ(cb.shm_directory.entries[0].size, (uint64_t)reg_size);

    shared_memory_destroy(reg);
}

// 2) attach__map_entries__then_build_local_bases
TEST(shm_directory__attach_map__then_local_bases_built, unit) {
    uint32_t pid = shared_memory_get_pid();
    uint32_t sid = shared_memory_get_session_id();
    size_t reg_size = thread_registry_calculate_memory_size_with_capacity(MAX_THREADS);

    SharedMemoryRef reg = shared_memory_create_unique(ADA_ROLE_REGISTRY, pid, sid, reg_size, nullptr, 0);
    ASSERT_NE(reg, nullptr);

    ControlBlock cb = {};
    make_registry_directory(&cb.shm_directory, reg, reg_size);
    ASSERT_TRUE(shm_dir_map_local_bases(&cb.shm_directory));

    void* base0 = shm_dir_get_base(0);
    size_t size0 = shm_dir_get_size(0);
    EXPECT_NE(base0, nullptr);
    EXPECT_EQ(size0, reg_size);

    shm_dir_clear_local_bases();
    shared_memory_destroy(reg);
}

// 3) accessor__index_offset_materialization__then_handle_valid
TEST(shm_directory__materialize_index__then_write_read_raw, unit) {
    uint32_t pid = shared_memory_get_pid();
    uint32_t sid = shared_memory_get_session_id();
    size_t reg_size = thread_registry_calculate_memory_size_with_capacity(MAX_THREADS);

    SharedMemoryRef reg = shared_memory_create_unique(ADA_ROLE_REGISTRY, pid, sid, reg_size, nullptr, 0);
    ASSERT_NE(reg, nullptr);

    void* reg_addr = shared_memory_get_address(reg);
    ThreadRegistry* registry = thread_registry_init(reg_addr, reg_size);
    ASSERT_NE(registry, nullptr);

    // Publish and map directory
    ControlBlock cb = {};
    make_registry_directory(&cb.shm_directory, reg, reg_size);
    ASSERT_TRUE(shm_dir_map_local_bases(&cb.shm_directory));

    // Register a thread and obtain active ring header
    ThreadLaneSet* lanes = thread_registry_register(registry, (uintptr_t)pthread_self());
    ASSERT_NE(lanes, nullptr);
    Lane* idx_lane = thread_lanes_get_index_lane(lanes);
    ASSERT_NE(idx_lane, nullptr);

    RingBufferHeader* hdr = thread_registry_get_active_ring_header(registry, idx_lane);
    ASSERT_NE(hdr, nullptr);

    // Write and read back a simple IndexEvent using raw helpers
    IndexEvent ev = {};
    ev.timestamp = 1234;
    ev.function_id = 0xABCD;
    ev.thread_id = 42;
    ev.event_kind = EVENT_KIND_CALL;
    ev.call_depth = 7;

    bool wrote = ring_buffer_write_raw(hdr, sizeof(IndexEvent), &ev);
    EXPECT_TRUE(wrote);
    EXPECT_GT(ring_buffer_available_read_raw(hdr), 0u);

    IndexEvent out = {};
    bool read = ring_buffer_read_raw(hdr, sizeof(IndexEvent), &out);
    EXPECT_TRUE(read);
    EXPECT_EQ(out.timestamp, ev.timestamp);
    EXPECT_EQ(out.function_id, ev.function_id);
    EXPECT_EQ(out.thread_id, ev.thread_id);
    EXPECT_EQ(out.event_kind, ev.event_kind);
    EXPECT_EQ(out.call_depth, ev.call_depth);

    shm_dir_clear_local_bases();
    // Registry is in shm; no explicit destroy needed here in test harness
    shared_memory_destroy(reg);
}
