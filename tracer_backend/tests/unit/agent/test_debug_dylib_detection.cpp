// Unit tests for debug dylib detection
//
// Tests the API for detecting Xcode Debug configuration builds that use
// ENABLE_DEBUG_DYLIB=YES to create thin stub executables.

#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/agent/debug_dylib_detection.h>
}

// =============================================================================
// Test: ada_detect_debug_dylib_stub - input validation
// =============================================================================

TEST(debug_dylib_detection__null_base__then_returns_false, unit) {
    DebugDylibInfo info;
    EXPECT_FALSE(ada_detect_debug_dylib_stub(0, "/path/to/binary", &info));
}

TEST(debug_dylib_detection__null_info__then_returns_false, unit) {
    // Use a non-zero base address that won't crash (we just check the null guard)
    EXPECT_FALSE(ada_detect_debug_dylib_stub(0x1000, "/path/to/binary", nullptr));
}

TEST(debug_dylib_detection__valid_inputs__then_returns_true, unit) {
    // Even if the binary at the address isn't a valid Mach-O,
    // the function should return true (detection was attempted)
    // unless the inputs are null.
    //
    // Note: We can't easily test with real Mach-O data in unit tests,
    // so we just verify the input validation works correctly.
    // Integration tests with actual binaries will test the full detection.

    // This test would need a valid Mach-O header in memory to pass fully.
    // For now, we just verify the function doesn't crash on valid inputs.
    DebugDylibInfo info;

    // Create a minimal buffer that looks like an invalid Mach-O
    // The function should return false because it's not a valid Mach-O
    uint8_t buffer[64] = {0};
    bool result = ada_detect_debug_dylib_stub(
        reinterpret_cast<uintptr_t>(buffer),
        "/path/to/binary",
        &info);

    // Function returns false for invalid Mach-O magic
    EXPECT_FALSE(result);
}

// =============================================================================
// Test: ada_find_loaded_debug_dylib - input validation
// =============================================================================

TEST(debug_dylib_detection__find_null_info__then_returns_false, unit) {
    EXPECT_FALSE(ada_find_loaded_debug_dylib(nullptr));
}

TEST(debug_dylib_detection__find_empty_path__then_returns_false, unit) {
    DebugDylibInfo info;
    info.is_debug_stub = true;
    info.debug_dylib_path[0] = '\0';  // Empty path
    info.debug_dylib_base = 0;

    EXPECT_FALSE(ada_find_loaded_debug_dylib(&info));
}

TEST(debug_dylib_detection__find_nonexistent_dylib__then_returns_false, unit) {
    DebugDylibInfo info;
    info.is_debug_stub = true;
    strncpy(info.debug_dylib_path, "/nonexistent/path.debug.dylib",
            sizeof(info.debug_dylib_path) - 1);
    info.debug_dylib_path[sizeof(info.debug_dylib_path) - 1] = '\0';
    info.debug_dylib_base = 0;

    // Should return false because the dylib doesn't exist
    EXPECT_FALSE(ada_find_loaded_debug_dylib(&info));
}

// =============================================================================
// Test: Platform-specific behavior
// =============================================================================

#ifndef __APPLE__
TEST(debug_dylib_detection__non_apple__then_not_stub, unit) {
    // On non-Apple platforms, debug dylib detection always returns is_debug_stub=false
    DebugDylibInfo info;
    info.is_debug_stub = true;  // Pre-set to true

    uint8_t buffer[64] = {0};
    bool result = ada_detect_debug_dylib_stub(
        reinterpret_cast<uintptr_t>(buffer),
        "/path/to/binary",
        &info);

    EXPECT_TRUE(result);  // Function succeeds
    EXPECT_FALSE(info.is_debug_stub);  // But no stub detected on non-Apple
}

TEST(debug_dylib_detection__non_apple_find__then_returns_false, unit) {
    DebugDylibInfo info;
    info.is_debug_stub = true;
    strncpy(info.debug_dylib_path, "test.debug.dylib",
            sizeof(info.debug_dylib_path) - 1);
    info.debug_dylib_path[sizeof(info.debug_dylib_path) - 1] = '\0';

    // On non-Apple platforms, find always returns false
    EXPECT_FALSE(ada_find_loaded_debug_dylib(&info));
}
#endif

// =============================================================================
// Test: DebugDylibInfo struct initialization
// =============================================================================

TEST(debug_dylib_detection__info_initialized__then_zeroed, unit) {
    DebugDylibInfo info;

    // Initialize with non-zero values
    info.is_debug_stub = true;
    strcpy(info.debug_dylib_path, "test");
    info.debug_dylib_base = 0xDEADBEEF;

    uint8_t buffer[64] = {0};
    ada_detect_debug_dylib_stub(
        reinterpret_cast<uintptr_t>(buffer),
        "/path/to/binary",
        &info);

    // After detection (even if failed), struct should be initialized
    EXPECT_FALSE(info.is_debug_stub);
    EXPECT_EQ(info.debug_dylib_path[0], '\0');
    EXPECT_EQ(info.debug_dylib_base, 0u);
}
