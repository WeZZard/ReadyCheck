#include "gtest/gtest.h"
#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/utils/shared_memory.h>
#include <tracer_backend/utils/ring_buffer.h>
#include <tracer_backend/utils/thread_registry.h>
}

#include "thread_registry_private.h"

namespace {

class OffsetsMaterializationTest : public ::testing::Test {
protected:
    SharedMemoryRef shm_ = nullptr;
    void* memory_ = nullptr;
    size_t size_ = 0;
    ada::internal::ThreadRegistry* registry_ = nullptr;

    void SetUp() override {
        size_ = 64 * 1024 * 1024; // 64MB
        char name_buf[256];
        shm_ = shared_memory_create_unique("offsets", getpid(), shared_memory_get_session_id(),
                                           size_, name_buf, sizeof(name_buf));
        ASSERT_NE(shm_, nullptr);
        memory_ = shared_memory_get_address(shm_);
        ASSERT_NE(memory_, nullptr);
        registry_ = ada::internal::ThreadRegistry::create(memory_, size_, MAX_THREADS);
        ASSERT_NE(registry_, nullptr);
    }

    void TearDown() override {
        if (memory_) std::memset(memory_, 0, size_);
        if (shm_) { shared_memory_destroy(shm_); shm_ = nullptr; }
        registry_ = nullptr;
        memory_ = nullptr;
        size_ = 0;
    }
};

// Example 1: Layout materialization from offsets is valid and usable
TEST_F(OffsetsMaterializationTest, offsets__layout_materialization__usable) {
    auto* lanes = registry_->register_thread(0xBEEF);
    ASSERT_NE(lanes, nullptr);

    // Compute pool base and materialize layouts via offsets
    uint8_t* reg_base = reinterpret_cast<uint8_t*>(registry_);
    auto& seg = registry_->segments[0];
    uint8_t* pool_base = reg_base + seg.base_offset;

    ada::internal::LaneMemoryLayout* idx_layout_from_off = reinterpret_cast<ada::internal::LaneMemoryLayout*>(
        pool_base + lanes->index_layout_off);
    ada::internal::LaneMemoryLayout* det_layout_from_off = reinterpret_cast<ada::internal::LaneMemoryLayout*>(
        pool_base + lanes->detail_layout_off);
    ASSERT_NE(idx_layout_from_off, nullptr);
    ASSERT_NE(det_layout_from_off, nullptr);
    // Touch queues to ensure memory is valid
    idx_layout_from_off->submit_queue[0] = 7;
    EXPECT_EQ(idx_layout_from_off->submit_queue[0], 7u);
}

// Example 2: Ring pointer materialization from ring descriptors allows attach + roundtrip
TEST_F(OffsetsMaterializationTest, offsets__ring_attach_and_roundtrip__works) {
    auto* lanes = registry_->register_thread(0xCAFE);
    ASSERT_NE(lanes, nullptr);

    uint8_t* reg_base = reinterpret_cast<uint8_t*>(registry_);
    auto& seg = registry_->segments[0];
    uint8_t* seg_base = reg_base + seg.base_offset;

    ada::internal::LaneMemoryLayout* idx_layout = reinterpret_cast<ada::internal::LaneMemoryLayout*>(
        seg_base + lanes->index_layout_off);
    ASSERT_NE(idx_layout, nullptr);

    // Use active ring 0
    uint32_t idx = lanes->index_lane.active_idx.load();
    auto desc = idx_layout->ring_descs[idx];
    uint8_t* ring_ptr = seg_base + desc.offset;

    RingBuffer* rb = ring_buffer_attach(ring_ptr, desc.bytes, sizeof(IndexEvent));
    ASSERT_NE(rb, nullptr);

    IndexEvent ev{};
    ev.timestamp = 1;
    ev.function_id = 0xDEADBEEF;
    ev.thread_id = 123;
    ev.event_kind = EVENT_KIND_CALL;
    ev.call_depth = 7;

    ASSERT_TRUE(ring_buffer_write(rb, &ev));

    IndexEvent out{};
    ASSERT_TRUE(ring_buffer_read(rb, &out));
    EXPECT_EQ(out.timestamp, ev.timestamp);
    EXPECT_EQ(out.function_id, ev.function_id);
    EXPECT_EQ(out.thread_id, ev.thread_id);
    EXPECT_EQ(out.event_kind, ev.event_kind);
    EXPECT_EQ(out.call_depth, ev.call_depth);

    ring_buffer_destroy(rb);
}

// Example 3: Use raw, header-only helpers directly with materialized header
TEST_F(OffsetsMaterializationTest, offsets__raw_helpers__roundtrip_works) {
    auto* lanes = registry_->register_thread(0xABCD);
    ASSERT_NE(lanes, nullptr);

    uint8_t* reg_base = reinterpret_cast<uint8_t*>(registry_);
    auto& seg = registry_->segments[0];
    uint8_t* seg_base = reg_base + seg.base_offset;

    auto* idx_layout = reinterpret_cast<ada::internal::LaneMemoryLayout*>(
        seg_base + lanes->index_layout_off);
    ASSERT_NE(idx_layout, nullptr);
    uint32_t idx = lanes->index_lane.active_idx.load();
    auto desc = idx_layout->ring_descs[idx];
    auto* header = reinterpret_cast<RingBufferHeader*>(seg_base + desc.offset);

    IndexEvent ev{};
    ev.timestamp = 99;
    ASSERT_TRUE(ring_buffer_write_raw(header, sizeof(IndexEvent), &ev));
    IndexEvent out{};
    ASSERT_TRUE(ring_buffer_read_raw(header, sizeof(IndexEvent), &out));
    EXPECT_EQ(out.timestamp, ev.timestamp);
}

// Invariant: no absolute ring pointers are present post-migration; offsets-only access validated by other tests

} // namespace
