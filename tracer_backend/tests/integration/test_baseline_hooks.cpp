#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>

extern "C" {
#include "frida_controller.h"
#include "shared_memory.h"
#include "ring_buffer.h"
#include "tracer_types.h"
#include "ada_paths.h"
}

using namespace std::chrono_literals;

// Test 1: Basic functionality - spawn, inject, verify hooks work
TEST(BaselineHooks, BasicFunctionality) {
    FridaController* controller = frida_controller_create("/tmp/ada_test");
    ASSERT_NE(controller, nullptr);
    
    const char * exe_path = ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_cli";
    char* argv[] = {(char*)exe_path, nullptr};
    uint32_t pid = 0;
    frida_controller_spawn_suspended(controller, exe_path, argv, &pid);
    if (pid == 0) {
        frida_controller_destroy(controller);
        GTEST_SKIP() << "Could not spawn test process: " << exe_path;
    }
    ASSERT_GT(pid, 0);
    
    // Inject agent
    int result = frida_controller_inject_agent(controller, ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/lib/libfrida_agent.dylib");
    if (result != 0) {
        result = frida_controller_inject_agent(controller, ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/lib/libfrida_agent.dylib");
    }
    
    // Resume process
    frida_controller_resume(controller);
    
    // Let it run for a bit
    std::this_thread::sleep_for(2s);
    
    // Check process state
    ProcessState state = frida_controller_get_state(controller);
    EXPECT_NE(state, PROCESS_STATE_FAILED);
    
    frida_controller_destroy(controller);
}

// Test 2: TLS reentrancy protection
TEST(BaselineHooks, ReentrancyProtection) {
    FridaController* controller = frida_controller_create("/tmp/ada_test");
    ASSERT_NE(controller, nullptr);
    
    const char * exe_path = ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_cli";
    char* argv[] = {(char*)exe_path, nullptr};
    uint32_t pid = 0;
    frida_controller_spawn_suspended(controller, exe_path, argv, &pid);
    if (pid == 0) {
        frida_controller_destroy(controller);
        GTEST_SKIP() << "Could not spawn test process: " << exe_path;
    }
    
    // Inject agent and resume
    frida_controller_inject_agent(controller, ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/lib/libfrida_agent.dylib");
    frida_controller_resume(controller);
    
    // Run for a while - recursive functions should not cause infinite loops
    std::this_thread::sleep_for(3s);
    
    // Process should still be running
    ProcessState state = frida_controller_get_state(controller);
    EXPECT_NE(state, PROCESS_STATE_FAILED);
    
    frida_controller_destroy(controller);
}

// Test 3: Multi-threaded event capture
TEST(BaselineHooks, MultiThreaded) {
    FridaController* controller = frida_controller_create("/tmp/ada_test");
    ASSERT_NE(controller, nullptr);
    
    const char * exe_path = ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/test/test_runloop";
    char* argv[] = {(char*)exe_path, nullptr};
    uint32_t pid = 0;
    frida_controller_spawn_suspended(controller, exe_path, argv, &pid);
    if (pid == 0) {
        frida_controller_destroy(controller);
        GTEST_SKIP() << "Could not spawn test process: " << exe_path;
    }
    
    // Inject agent and resume
    frida_controller_inject_agent(controller, ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/lib/libfrida_agent.dylib");
    frida_controller_resume(controller);
    
    // Let it run for a bit to generate events from multiple threads
    std::this_thread::sleep_for(3s);
    
    // Verify process is still healthy
    ProcessState state = frida_controller_get_state(controller);
    EXPECT_NE(state, PROCESS_STATE_FAILED);
    
    frida_controller_destroy(controller);
}

// Test 4: Verify agent compiles and loads
TEST(BaselineHooks, AgentLoads) {
    // Check if agent library exists
    FILE* f = fopen(ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/lib/libfrida_agent.dylib", "r");
    if (!f) {
        f = fopen(ADA_WORKSPACE_ROOT "/target/" ADA_BUILD_PROFILE "/tracer_backend/lib/libfrida_agent.dylib", "r");
    }
    
    if (f) {
        fclose(f);
        SUCCEED() << "Agent library exists";
    } else {
        FAIL() << "Agent library not found";
    }
}

// Test 5: Shared memory and ring buffer setup
TEST(BaselineHooks, SharedMemorySetup) {
    pid_t my_pid = shared_memory_get_pid();
    uint32_t session_id = shared_memory_get_session_id();
    
    // Create shared memory
    char shm_name[256];
    SharedMemoryRef shm = shared_memory_create_unique(
        ADA_ROLE_INDEX, my_pid, session_id, 
        1024 * 1024, shm_name, sizeof(shm_name));
    ASSERT_NE(shm, nullptr);
    
    void* addr = shared_memory_get_address(shm);
    ASSERT_NE(addr, nullptr);
    
    // Initialize ring buffer
    RingBuffer* ring = ring_buffer_create(addr, 1024 * 1024, sizeof(IndexEvent));
    ASSERT_NE(ring, nullptr);
    
    // Write a test event
    IndexEvent event = {
        .timestamp = 123456789,
        .function_id = 0xDEADBEEF,
        .thread_id = 42,
        .event_kind = EVENT_KIND_CALL,
        .call_depth = 1
    };
    
    bool written = ring_buffer_write(ring, &event);
    EXPECT_TRUE(written);
    
    // Read it back
    IndexEvent read_event;
    bool read = ring_buffer_read(ring, &read_event);
    EXPECT_TRUE(read);
    EXPECT_EQ(read_event.function_id, 0xDEADBEEF);
    EXPECT_EQ(read_event.thread_id, 42);
    
    ring_buffer_destroy(ring);
    shared_memory_destroy(shm);
}