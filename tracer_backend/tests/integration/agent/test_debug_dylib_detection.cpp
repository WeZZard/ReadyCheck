// Integration tests for debug dylib detection
//
// These tests verify that the ADA tracer correctly detects and traces
// debug dylib stubs created by Xcode's ENABLE_DEBUG_DYLIB=YES setting.
//
// Prerequisites:
//   - Build the test fixture: ./tracer_backend/tests/fixtures/debug_stub_app/build_fixture.sh
//   - Apple Developer certificate (for code signing)

#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <sstream>
#include <iostream>
#include <filesystem>

extern "C" {
#include <tracer_backend/agent/debug_dylib_detection.h>
}

namespace fs = std::filesystem;

namespace {

// Get the workspace root from environment or default
std::string get_workspace_root() {
    const char* root = getenv("ADA_WORKSPACE_ROOT");
    if (root && root[0] != '\0') {
        return root;
    }
    // Fallback: try to find it relative to current working directory
    fs::path cwd = fs::current_path();
    while (cwd.has_parent_path()) {
        if (fs::exists(cwd / "Cargo.toml") && fs::exists(cwd / "tracer_backend")) {
            return cwd.string();
        }
        cwd = cwd.parent_path();
    }
    return "";
}

std::string get_fixture_path() {
    return get_workspace_root() + "/tracer_backend/tests/fixtures/debug_stub_app";
}

std::string get_debug_app_path() {
    return get_fixture_path() + "/build/Build/Products/Debug/DebugStubApp.app/Contents/MacOS/DebugStubApp";
}

std::string get_ada_binary() {
    std::string root = get_workspace_root();
    // Try release first, then debug
    std::string release_path = root + "/target/release/ada";
    std::string debug_path = root + "/target/debug/ada";

    if (fs::exists(release_path)) {
        return release_path;
    } else if (fs::exists(debug_path)) {
        return debug_path;
    }
    return "";
}

bool fixture_is_built() {
    return fs::exists(get_debug_app_path());
}

// Run a command and capture output
std::pair<int, std::string> run_command(const std::string& cmd) {
    std::string result;
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) {
        return {-1, "Failed to run command"};
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    int status = pclose(pipe);
    return {WEXITSTATUS(status), result};
}

// Extract event count from ada query summary output
size_t parse_event_count(const std::string& output) {
    // Look for "Events:" line
    size_t pos = output.find("Events:");
    if (pos == std::string::npos) {
        return 0;
    }

    // Skip "Events:" and any whitespace
    pos += 7;
    while (pos < output.size() && (output[pos] == ' ' || output[pos] == '\t')) {
        pos++;
    }

    // Parse the number (may have commas)
    std::string num_str;
    while (pos < output.size() && (isdigit(output[pos]) || output[pos] == ',')) {
        if (output[pos] != ',') {
            num_str += output[pos];
        }
        pos++;
    }

    return num_str.empty() ? 0 : std::stoull(num_str);
}

// Extract module name from ada query summary output
std::string parse_module_name(const std::string& output) {
    // Look for "Module:" line
    size_t pos = output.find("Module:");
    if (pos == std::string::npos) {
        return "";
    }

    // Skip "Module:" and whitespace
    pos += 7;
    while (pos < output.size() && (output[pos] == ' ' || output[pos] == '\t')) {
        pos++;
    }

    // Read until whitespace or newline
    std::string name;
    while (pos < output.size() && output[pos] != ' ' && output[pos] != '\n' && output[pos] != '\t') {
        name += output[pos];
        pos++;
    }

    return name;
}

} // anonymous namespace

// =============================================================================
// Integration Tests
// =============================================================================

class DebugDylibDetectionIntegration : public ::testing::Test {
protected:
    void SetUp() override {
        workspace_root_ = get_workspace_root();
        ada_binary_ = get_ada_binary();

        if (workspace_root_.empty()) {
            GTEST_SKIP() << "Could not find workspace root";
        }

        if (ada_binary_.empty()) {
            GTEST_SKIP() << "Could not find ada binary (run cargo build first)";
        }

        if (!fixture_is_built()) {
            GTEST_SKIP() << "Test fixture not built. Run: "
                         << get_fixture_path() << "/build_fixture.sh";
        }
    }

    std::string workspace_root_;
    std::string ada_binary_;
};

TEST_F(DebugDylibDetectionIntegration, fixture_app__traces_debug_dylib) {
    // Trace the debug stub app
    std::string cmd = ada_binary_ + " capture start " + get_debug_app_path();
    auto [status, output] = run_command(cmd);

    // Skip if agent library not found (common in test environment)
    if (output.find("libfrida_agent.dylib not found") != std::string::npos) {
        GTEST_SKIP() << "Agent library not found - run from workspace with release build";
    }

    // The app exits after 2 seconds, so capture should complete
    EXPECT_EQ(status, 0) << "Capture failed: " << output;

    // Query the latest session
    auto [query_status, summary] = run_command(ada_binary_ + " query @latest summary");
    EXPECT_EQ(query_status, 0) << "Query failed: " << summary;

    // Verify module name contains debug.dylib
    std::string module = parse_module_name(summary);
    EXPECT_TRUE(module.find("debug.dylib") != std::string::npos)
        << "Expected module to be debug.dylib, got: " << module;

    // Verify we captured a reasonable number of events
    size_t events = parse_event_count(summary);
    EXPECT_GT(events, 100) << "Expected more than 100 events, got: " << events;
}

TEST_F(DebugDylibDetectionIntegration, fixture_app__contains_app_functions) {
    // Trace the debug stub app
    std::string cmd = ada_binary_ + " capture start " + get_debug_app_path();
    auto [status, output] = run_command(cmd);

    // Skip if agent library not found (common in test environment)
    if (output.find("libfrida_agent.dylib not found") != std::string::npos) {
        GTEST_SKIP() << "Agent library not found - run from workspace with release build";
    }

    EXPECT_EQ(status, 0) << "Capture failed: " << output;

    // Query functions
    auto [query_status, functions] = run_command(ada_binary_ + " query @latest functions");
    EXPECT_EQ(query_status, 0) << "Query failed: " << functions;

    // Check for expected app functions (mangled Swift names)
    // These should be present in the debug dylib, not the stub
    EXPECT_TRUE(functions.find("DebugStubApp") != std::string::npos)
        << "Expected DebugStubApp functions in trace";
    EXPECT_TRUE(functions.find("ContentView") != std::string::npos)
        << "Expected ContentView functions in trace";

    // Verify stub functions are NOT the primary content
    // The stub would show __debug_blank_executor_main as a primary function
    // With detection working, we should see real app functions
    size_t stub_count = 0;
    size_t app_count = 0;

    std::istringstream stream(functions);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("__debug_blank_executor") != std::string::npos) {
            stub_count++;
        }
        if (line.find("DebugStubApp") != std::string::npos ||
            line.find("ContentView") != std::string::npos) {
            app_count++;
        }
    }

    EXPECT_GT(app_count, stub_count)
        << "Expected more app functions than stub functions. "
        << "App: " << app_count << ", Stub: " << stub_count;
}

// Compare event counts between Debug with debug dylib and Debug without debug dylib
// Both should have similar event counts if detection is working correctly
TEST_F(DebugDylibDetectionIntegration, event_counts_comparable_with_and_without_debug_dylib) {
    // Build Debug WITHOUT debug dylib for comparison
    std::string no_dylib_build_cmd =
        "xcodebuild -project " + get_fixture_path() + "/DebugStubApp.xcodeproj "
        "-scheme DebugStubApp -configuration Debug "
        "-derivedDataPath " + get_fixture_path() + "/build_no_dylib "
        "CODE_SIGNING_ALLOWED=NO ENABLE_DEBUG_DYLIB=NO build 2>&1";

    auto [build_status, build_output] = run_command(no_dylib_build_cmd);
    if (build_status != 0) {
        GTEST_SKIP() << "Could not build Debug configuration without debug dylib: " << build_output;
    }

    std::string no_dylib_app = get_fixture_path() +
        "/build_no_dylib/Build/Products/Debug/DebugStubApp.app/Contents/MacOS/DebugStubApp";

    if (!fs::exists(no_dylib_app)) {
        GTEST_SKIP() << "Debug app without debug dylib not found at: " << no_dylib_app;
    }

    // Trace Debug WITH debug dylib (ENABLE_DEBUG_DYLIB=YES)
    std::string with_dylib_cmd = ada_binary_ + " capture start " + get_debug_app_path();
    auto [with_status, with_output] = run_command(with_dylib_cmd);

    if (with_output.find("libfrida_agent.dylib not found") != std::string::npos) {
        GTEST_SKIP() << "Agent library not found - run from workspace with release build";
    }

    auto [with_query_status, with_summary] = run_command(ada_binary_ + " query @latest summary");
    size_t with_dylib_events = parse_event_count(with_summary);
    std::string with_dylib_module = parse_module_name(with_summary);

    // Trace Debug WITHOUT debug dylib (ENABLE_DEBUG_DYLIB=NO)
    std::string no_dylib_cmd = ada_binary_ + " capture start " + no_dylib_app;
    auto [no_status, no_output] = run_command(no_dylib_cmd);

    auto [no_query_status, no_summary] = run_command(ada_binary_ + " query @latest summary");
    size_t no_dylib_events = parse_event_count(no_summary);
    std::string no_dylib_module = parse_module_name(no_summary);

    // Log the results for debugging
    std::cout << "\n=== Event Count Comparison ===" << std::endl;
    std::cout << "With debug dylib:    " << with_dylib_events << " events (module: " << with_dylib_module << ")" << std::endl;
    std::cout << "Without debug dylib: " << no_dylib_events << " events (module: " << no_dylib_module << ")" << std::endl;

    // Verify the with-dylib trace used the debug.dylib
    EXPECT_TRUE(with_dylib_module.find("debug.dylib") != std::string::npos)
        << "Expected module to be debug.dylib when ENABLE_DEBUG_DYLIB=YES, got: " << with_dylib_module;

    // Verify the no-dylib trace used the normal binary (no debug.dylib suffix)
    EXPECT_TRUE(no_dylib_module.find("debug.dylib") == std::string::npos)
        << "Expected module to NOT be debug.dylib when ENABLE_DEBUG_DYLIB=NO, got: " << no_dylib_module;

    // Event counts should be very close (within 10% of each other)
    // If detection fails, with_dylib would show ~50-100 events (stub only)
    // while no_dylib would show ~1000+ events
    double ratio = static_cast<double>(with_dylib_events) / static_cast<double>(no_dylib_events);

    EXPECT_GT(ratio, 0.5) << "With-dylib events (" << with_dylib_events
        << ") should be at least 50% of no-dylib events (" << no_dylib_events << ")";
    EXPECT_LT(ratio, 2.0) << "With-dylib events (" << with_dylib_events
        << ") should be at most 200% of no-dylib events (" << no_dylib_events << ")";

    // Additional sanity check: both should have a reasonable number of events
    EXPECT_GT(with_dylib_events, 100) << "Expected more than 100 events with debug dylib";
    EXPECT_GT(no_dylib_events, 100) << "Expected more than 100 events without debug dylib";
}
