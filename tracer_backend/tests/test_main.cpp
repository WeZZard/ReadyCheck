// Main entry point for Google Test runner
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstdio>
#include <cstdlib>
#include "ada_paths.h"

// Custom test environment setup for tracer_backend tests
class TracerBackendTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Global setup for all tests
        printf("===== Tracer Backend Test Suite =====\n");
        printf("Setting up test environment...\n");
        
        // Provide stable defaults via compiled-in macros; allow env override by runner
        const char* ws_env = getenv("ADA_WORKSPACE_ROOT");
        if (!ws_env || ws_env[0] == '\0') {
            setenv("ADA_WORKSPACE_ROOT", ADA_WORKSPACE_ROOT, 1);
        }
        const char* prof_env = getenv("ADA_BUILD_PROFILE");
        if (!prof_env || prof_env[0] == '\0') {
            setenv("ADA_BUILD_PROFILE", ADA_BUILD_PROFILE, 1);
        }
    }
    
    void TearDown() override {
        // Global teardown for all tests
        printf("\nTearing down test environment...\n");
        printf("===== Test Suite Complete =====\n");
    }
};

// Custom test event listener for better output formatting
class BehavioralTestPrinter : public ::testing::EmptyTestEventListener {
    void OnTestStart(const ::testing::TestInfo& test_info) override {
        // Format test names according to behavioral naming convention
        printf("\n");
    }
    
    void OnTestEnd(const ::testing::TestInfo& test_info) override {
        if (test_info.result()->Passed()) {
            printf("  ✓ %s\n", test_info.name());
        } else {
            printf("  ✗ %s\n", test_info.name());
        }
    }
};

int main(int argc, char** argv) {
    // Initialize Google Test
    ::testing::InitGoogleTest(&argc, argv);
    
    // Initialize Google Mock
    ::testing::InitGoogleMock(&argc, argv);
    
    // Add custom environment
    ::testing::AddGlobalTestEnvironment(new TracerBackendTestEnvironment);
    
    // Add custom event listener for behavioral test formatting
    ::testing::TestEventListeners& listeners = 
        ::testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new BehavioralTestPrinter);
    
    // Run all tests
    return RUN_ALL_TESTS();
}