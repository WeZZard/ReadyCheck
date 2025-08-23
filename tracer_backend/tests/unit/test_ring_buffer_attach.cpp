// Unit tests for Ring Buffer Attach functionality using Google Test
#include <gtest/gtest.h>
#include <memory>
#include <cstring>

extern "C" {
    #include "ring_buffer.h"
    #include "tracer_types.h"
}

class RingBufferAttachTest : public ::testing::Test {
protected:
    void SetUp() override {
        printf("[RING] Setting up attach test\n");
        buffer_size = sizeof(RingBufferHeader) + (capacity * sizeof(TestEvent));
        memory = std::make_unique<uint8_t[]>(buffer_size);
        memset(memory.get(), 0, buffer_size);
    }
    
    struct TestEvent {
        uint64_t timestamp;
        uint64_t function_id;
        uint32_t thread_id;
        uint32_t event_type;
    };
    
    static constexpr size_t capacity = 100;
    size_t buffer_size;
    std::unique_ptr<uint8_t[]> memory;
};

// Test: ring_buffer__attach_to_existing__then_preserve_data
TEST_F(RingBufferAttachTest, ring_buffer__attach_to_existing__then_preserve_data) {
    printf("[RING] attach_to_existing → preserve data\n");
    
    // Arrange - Create and populate buffer
    RingBuffer* rb_original = ring_buffer_create(memory.get(), buffer_size, sizeof(TestEvent));
    ASSERT_NE(rb_original, nullptr);
    
    TestEvent original_events[5];
    for (int i = 0; i < 5; i++) {
        original_events[i].timestamp = 1000 + i;
        original_events[i].function_id = 0xDEAD0000 + i;
        original_events[i].thread_id = 100 + i;
        original_events[i].event_type = i % 2;
        
        ASSERT_TRUE(ring_buffer_write(rb_original, &original_events[i]));
    }
    
    // Verify initial state
    EXPECT_EQ(ring_buffer_available_read(rb_original), 5);
    
    // Destroy original reference (but keep memory)
    ring_buffer_destroy(rb_original);
    
    // Act - Attach to existing buffer
    RingBuffer* rb_attached = ring_buffer_attach(memory.get(), buffer_size, sizeof(TestEvent));
    
    // Assert attachment succeeded
    ASSERT_NE(rb_attached, nullptr) << "Failed to attach to existing buffer";
    EXPECT_FALSE(ring_buffer_is_empty(rb_attached)) << "Attached buffer should not be empty";
    EXPECT_EQ(ring_buffer_available_read(rb_attached), 5);
    
    // Act - Read from attached buffer
    TestEvent read_events[5];
    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(ring_buffer_read(rb_attached, &read_events[i]));
    }
    
    // Assert data preserved
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(read_events[i].timestamp, original_events[i].timestamp);
        EXPECT_EQ(read_events[i].function_id, original_events[i].function_id);
        EXPECT_EQ(read_events[i].thread_id, original_events[i].thread_id);
        EXPECT_EQ(read_events[i].event_type, original_events[i].event_type);
    }
    
    EXPECT_TRUE(ring_buffer_is_empty(rb_attached));
    ring_buffer_destroy(rb_attached);
}

// Test: ring_buffer__concurrent_attach_and_write__then_both_succeed
// Direct translation of test_concurrent_attach_and_write() from C
TEST_F(RingBufferAttachTest, ring_buffer__concurrent_attach_and_write__then_both_succeed) {
    printf("[RING] concurrent_attach_and_write → both succeed\n");
    
    // Controller creates ring buffer
    RingBuffer* controller_rb = ring_buffer_create(memory.get(), buffer_size, sizeof(int));
    ASSERT_NE(controller_rb, nullptr);
    
    // Controller writes some initial data
    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(ring_buffer_write(controller_rb, &i));
    }
    
    // Agent attaches to existing ring buffer
    RingBuffer* agent_rb = ring_buffer_attach(memory.get(), buffer_size, sizeof(int));
    ASSERT_NE(agent_rb, nullptr);
    
    // Both can write concurrently
    int controller_val = 1000;
    int agent_val = 2000;
    
    bool result1 = ring_buffer_write(controller_rb, &controller_val);
    bool result2 = ring_buffer_write(agent_rb, &agent_val);
    
    ASSERT_TRUE(result1);
    ASSERT_TRUE(result2);
    
    // Read all values to verify both writes succeeded
    int values[12];
    int count = 0;
    while (!ring_buffer_is_empty(controller_rb) && count < 12) {
        ASSERT_TRUE(ring_buffer_read(controller_rb, &values[count]));
        count++;
    }
    
    ASSERT_EQ(count, 12);
    
    // Verify we got the expected values
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(values[i], i);
    }
    // The last two can be in either order due to concurrency
    EXPECT_TRUE((values[10] == 1000 && values[11] == 2000) ||
                (values[10] == 2000 && values[11] == 1000));
    
    ring_buffer_destroy(controller_rb);
    ring_buffer_destroy(agent_rb);
}


// Test: ring_buffer__attach_invalid_magic__then_return_null
// Direct translation of test_attach_fails_on_invalid_magic() from C
TEST_F(RingBufferAttachTest, ring_buffer__attach_invalid_magic__then_return_null) {
    printf("[RING] attach_invalid_magic → return null\n");
    
    // Fill memory with garbage
    memset(memory.get(), 0xFF, buffer_size);
    
    // Try to attach - should fail
    RingBuffer* rb = ring_buffer_attach(memory.get(), buffer_size, sizeof(TestEvent));
    ASSERT_EQ(rb, nullptr) << "Should not attach to buffer with invalid magic";
}

