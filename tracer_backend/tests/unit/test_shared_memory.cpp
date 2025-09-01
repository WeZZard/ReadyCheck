// Unit tests for Shared Memory using Google Test
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <thread>
#include <chrono>
#include <cstring>
#include <vector>

extern "C" {
    #include <tracer_backend/utils/shared_memory.h>
    #include <tracer_backend/utils/tracer_types.h>
}

using ::testing::_;
using ::testing::Return;
using ::testing::NotNull;

// Test fixture for SharedMemory tests
class SharedMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        printf("[SHM] Setting up test\n");
        // Each test gets a unique session ID to avoid conflicts
        session_id = shared_memory_get_session_id();
        controller_pid = shared_memory_get_pid();
    }
    
    void TearDown() override {
        // Clean up any created shared memory segments
        for (auto& shm : created_segments) {
            if (shm) {
                shared_memory_destroy(shm);
            }
        }
        created_segments.clear();
    }
    
    // Helper to track created segments for cleanup
    SharedMemoryRef CreateAndTrack(const char* role, uint32_t pid, uint32_t session, size_t size) {
        SharedMemoryRef shm = shared_memory_create_unique(role, pid, session, size, nullptr, 0);
        if (shm) {
            created_segments.push_back(shm);
        }
        return shm;
    }
    
    uint32_t session_id;
    uint32_t controller_pid;
    std::vector<SharedMemoryRef> created_segments;
};

// Test: shared_memory__create_unique__then_valid_segment
TEST_F(SharedMemoryTest, shared_memory__create_unique__then_valid_segment) {
    printf("[SHM] create_unique → valid segment\n");
    
    // Act
    SharedMemoryRef shm = CreateAndTrack(ADA_ROLE_CONTROL, controller_pid, session_id, 4096);
    
    // Assert
    ASSERT_NE(shm, nullptr) << "Failed to create shared memory";
    
    void* addr = shared_memory_get_address(shm);
    ASSERT_NE(addr, nullptr) << "Shared memory address is null";
    
    size_t size = shared_memory_get_size(shm);
    EXPECT_EQ(size, 4096) << "Shared memory size mismatch";
}

// Test: shared_memory__write_and_read__then_data_preserved
TEST_F(SharedMemoryTest, shared_memory__write_and_read__then_data_preserved) {
    printf("[SHM] write_and_read → data preserved\n");
    
    // Arrange
    SharedMemoryRef shm = CreateAndTrack(ADA_ROLE_INDEX, controller_pid, session_id, 4096);
    ASSERT_NE(shm, nullptr);
    
    // Act - Write data
    void* addr = shared_memory_get_address(shm);
    const char* test_data = "Test shared memory data preservation";
    strcpy(static_cast<char*>(addr), test_data);
    
    // Assert - Read data back
    EXPECT_STREQ(static_cast<char*>(addr), test_data) 
        << "Data was not preserved in shared memory";
}

// Test: shared_memory__open_existing__then_access_same_memory
TEST_F(SharedMemoryTest, shared_memory__open_existing__then_access_same_memory) {
    printf("[SHM] open_existing → access same memory\n");
    
    // Arrange - Create segment
    SharedMemoryRef shm1 = CreateAndTrack(ADA_ROLE_DETAIL, controller_pid, session_id, 8192);
    ASSERT_NE(shm1, nullptr);
    
    // Write test data
    void* addr1 = shared_memory_get_address(shm1);
    uint64_t test_value = 0xDEADBEEFCAFEBABE;
    *static_cast<uint64_t*>(addr1) = test_value;
    
    // Act - Open same segment
    SharedMemoryRef shm2 = shared_memory_open_unique(ADA_ROLE_DETAIL, controller_pid, session_id, 8192);
    ASSERT_NE(shm2, nullptr) << "Failed to open existing segment";
    
    // Assert - Same data accessible
    void* addr2 = shared_memory_get_address(shm2);
    EXPECT_EQ(*static_cast<uint64_t*>(addr2), test_value) 
        << "Data not accessible from opened segment";
    
    // Clean up second reference
    shared_memory_destroy(shm2);
}

// Test: shared_memory__concurrent_access__then_data_integrity  
TEST_F(SharedMemoryTest, shared_memory__concurrent_access__then_data_integrity) {
    printf("[SHM] concurrent_access → data integrity\n");
    
    // Arrange
    SharedMemoryRef shm = CreateAndTrack(ADA_ROLE_CONTROL, controller_pid, session_id, sizeof(std::atomic<int>));
    ASSERT_NE(shm, nullptr);
    
    std::atomic<int>* counter = static_cast<std::atomic<int>*>(shared_memory_get_address(shm));
    *counter = 0;
    
    const int iterations = 1000;
    const int num_threads = 4;
    
    // Act - Multiple threads increment counter
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([counter, iterations]() {
            for (int j = 0; j < iterations; j++) {
                (*counter)++;
                std::this_thread::yield();
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Assert - All increments accounted for
    EXPECT_EQ(*counter, iterations * num_threads) 
        << "Concurrent increments lost";
}

// Test: shared_memory__different_sessions__then_isolated
TEST_F(SharedMemoryTest, shared_memory__different_sessions__then_isolated) {
    printf("[SHM] different_sessions → isolated\n");
    
    // Arrange - Create with current session
    SharedMemoryRef shm1 = CreateAndTrack(ADA_ROLE_CONTROL, controller_pid, session_id, 4096);
    ASSERT_NE(shm1, nullptr);
    
    // Write identifying data
    void* addr1 = shared_memory_get_address(shm1);
    strcpy(static_cast<char*>(addr1), "Session 1 Data");
    
    // Act - Try to open with different session ID
    uint32_t different_session = session_id ^ 0xFFFFFFFF;  // Flip all bits
    SharedMemoryRef shm2 = shared_memory_open_unique(ADA_ROLE_CONTROL, controller_pid, different_session, 4096);
    
    // Assert - Should not access same memory (nullptr or different content)
    if (shm2) {
        void* addr2 = shared_memory_get_address(shm2);
        EXPECT_STRNE(static_cast<char*>(addr2), "Session 1 Data") 
            << "Different sessions should be isolated";
        shared_memory_destroy(shm2);
    } else {
        SUCCEED() << "Different session correctly could not open segment";
    }
}

// Test: shared_memory__zero_size__then_creation_fails
TEST_F(SharedMemoryTest, shared_memory__zero_size__then_creation_fails) {
    printf("[SHM] zero_size → creation fails\n");
    
    // Act
    SharedMemoryRef shm = shared_memory_create_unique(ADA_ROLE_CONTROL, controller_pid, session_id, 0, nullptr, 0);
    
    // Assert
    EXPECT_EQ(shm, nullptr) << "Should not create zero-size segment";
}

// Test: shared_memory__destroy_then_recreate__then_success
TEST_F(SharedMemoryTest, shared_memory__destroy_then_recreate__then_success) {
    printf("[SHM] destroy_then_recreate → success\n");
    
    // Arrange - Create and destroy
    SharedMemoryRef shm1 = shared_memory_create_unique(ADA_ROLE_INDEX, controller_pid, session_id, 4096, nullptr, 0);
    ASSERT_NE(shm1, nullptr);
    shared_memory_destroy(shm1);
    
    // Act - Recreate with same parameters
    SharedMemoryRef shm2 = CreateAndTrack(ADA_ROLE_INDEX, controller_pid, session_id, 4096);
    
    // Assert
    ASSERT_NE(shm2, nullptr) << "Failed to recreate after destroy";
    EXPECT_NE(shared_memory_get_address(shm2), nullptr);
}

// Test: shared_memory__multiple_roles__then_independent_segments
TEST_F(SharedMemoryTest, shared_memory__multiple_roles__then_independent_segments) {
    printf("[SHM] multiple_roles → independent segments\n");
    
    // Act - Create segments for different roles
    SharedMemoryRef shm_control = CreateAndTrack(ADA_ROLE_CONTROL, controller_pid, session_id, 4096);
    SharedMemoryRef shm_index = CreateAndTrack(ADA_ROLE_INDEX, controller_pid, session_id, 8192);
    SharedMemoryRef shm_detail = CreateAndTrack(ADA_ROLE_DETAIL, controller_pid, session_id, 16384);
    
    // Assert - All created successfully
    ASSERT_NE(shm_control, nullptr);
    ASSERT_NE(shm_index, nullptr);
    ASSERT_NE(shm_detail, nullptr);
    
    // Assert - Different sizes preserved
    EXPECT_EQ(shared_memory_get_size(shm_control), 4096);
    EXPECT_EQ(shared_memory_get_size(shm_index), 8192);
    EXPECT_EQ(shared_memory_get_size(shm_detail), 16384);
    
    // Assert - Different addresses
    void* addr_control = shared_memory_get_address(shm_control);
    void* addr_index = shared_memory_get_address(shm_index);
    void* addr_detail = shared_memory_get_address(shm_detail);
    
    EXPECT_NE(addr_control, addr_index);
    EXPECT_NE(addr_control, addr_detail);
    EXPECT_NE(addr_index, addr_detail);
}

// Parameterized test for different sizes
class SharedMemorySizeTest : public ::testing::TestWithParam<size_t> {
protected:
    void SetUp() override {
        size = GetParam();
        session_id = shared_memory_get_session_id();
        pid = shared_memory_get_pid();
    }
    
    void TearDown() override {
        if (shm) {
            shared_memory_destroy(shm);
        }
    }
    
    size_t size;
    uint32_t session_id;
    uint32_t pid;
    SharedMemoryRef shm = nullptr;
};

TEST_P(SharedMemorySizeTest, shared_memory__various_sizes__then_create_success) {
    printf("[SHM] size_%zu → create success\n", size);
    
    // Act
    shm = shared_memory_create_unique(ADA_ROLE_CONTROL, pid, session_id, size, nullptr, 0);
    
    // Assert
    ASSERT_NE(shm, nullptr) << "Failed to create segment of size " << size;
    EXPECT_EQ(shared_memory_get_size(shm), size);
    EXPECT_NE(shared_memory_get_address(shm), nullptr);
}

// Test with various sizes including page boundaries
INSTANTIATE_TEST_SUITE_P(
    SizeTests,
    SharedMemorySizeTest,
    ::testing::Values(
        1024,              // 1KB
        4096,              // 4KB (typical page size)
        65536,             // 64KB
        1048576,           // 1MB
        33554432           // 32MB (typical lane size)
    ),
    [](const testing::TestParamInfo<size_t>& info) {
        // Generate readable test names
        size_t size = info.param;
        if (size < 1024) return "Size" + std::to_string(size) + "B";
        if (size < 1048576) return "Size" + std::to_string(size / 1024) + "KB";
        return "Size" + std::to_string(size / 1048576) + "MB";
    }
);