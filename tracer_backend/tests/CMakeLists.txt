# ===========================================
# Tests Module
# ===========================================

# Only build tests if BUILD_TESTING is enabled
if(NOT BUILD_TESTING)
    return()
endif()

# Fetch GoogleTest (includes GMock)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()
include(GoogleTest)

# Common test main
add_library(test_main STATIC test_main.cpp)
target_link_libraries(test_main PUBLIC GTest::gtest GTest::gmock)
target_include_directories(test_main 
    PRIVATE 
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)

# ===========================================
# Test Fixtures - Helper programs for integration tests
# ===========================================

# test_cli - Simple CLI program for tracing tests
add_executable(test_cli
    fixtures/test_cli.c
    fixtures/test_cli_modes.c
)
target_link_libraries(test_cli PRIVATE m)
# Place in bin/ directory as build.rs expects
set_target_properties(test_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(APPLE)
    # Check for APPLE_DEVELOPER_ID environment variable
    if(DEFINED ENV{APPLE_DEVELOPER_ID})
        set(SIGNING_IDENTITY "$ENV{APPLE_DEVELOPER_ID}")
        message(STATUS "Signing with identity: ${SIGNING_IDENTITY}")
    else()
        set(SIGNING_IDENTITY "-")
        message(WARNING "APPLE_DEVELOPER_ID not set. Using ad-hoc signing. 'debugger' entitlement will be ignored.")
    endif()

    add_custom_command(TARGET test_cli POST_BUILD
        COMMAND codesign -s "${SIGNING_IDENTITY}" --timestamp=none --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force $<TARGET_FILE:test_cli>
        COMMENT "Signing test_cli with entitlements"
        VERBATIM
    )
endif()

# test_runloop - RunLoop-based program for macOS-specific tests
if(APPLE)
    add_executable(test_runloop fixtures/test_runloop.c)
    target_link_libraries(test_runloop PRIVATE 
        m
        "-framework CoreFoundation"
    )
    # Place in bin/ directory as build.rs expects
    set_target_properties(test_runloop PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_custom_command(TARGET test_runloop POST_BUILD
        COMMAND codesign -s "${SIGNING_IDENTITY}" --timestamp=none --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force $<TARGET_FILE:test_runloop>
        COMMENT "Signing test_runloop with entitlements"
        VERBATIM
    )

    # ===========================================
    # Swift Test Fixtures
    # ===========================================

    # test_swift_simple - Minimal Swift executable for symbol hooking tests
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/bin/test_swift_simple
        COMMAND swiftc -O -parse-as-library
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_swift_simple.swift
            -o ${CMAKE_BINARY_DIR}/bin/test_swift_simple
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_swift_simple.swift
        COMMENT "Building test_swift_simple"
    )
    add_custom_target(test_swift_simple ALL
        DEPENDS ${CMAKE_BINARY_DIR}/bin/test_swift_simple)

    # Sign test_swift_simple
    add_custom_command(
        TARGET test_swift_simple POST_BUILD
        COMMAND codesign -s "${SIGNING_IDENTITY}" --timestamp=none --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force ${CMAKE_BINARY_DIR}/bin/test_swift_simple
        COMMENT "Signing test_swift_simple with entitlements"
        VERBATIM
    )

    # test_swift_runloop - NSRunLoop-based Swift program
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/bin/test_swift_runloop
        COMMAND swiftc -O -parse-as-library
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_swift_runloop.swift
            -o ${CMAKE_BINARY_DIR}/bin/test_swift_runloop
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_swift_runloop.swift
        COMMENT "Building test_swift_runloop"
    )
    add_custom_target(test_swift_runloop ALL
        DEPENDS ${CMAKE_BINARY_DIR}/bin/test_swift_runloop)

    # Sign test_swift_runloop
    add_custom_command(
        TARGET test_swift_runloop POST_BUILD
        COMMAND codesign -s "${SIGNING_IDENTITY}" --timestamp=none --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force ${CMAKE_BINARY_DIR}/bin/test_swift_runloop
        COMMENT "Signing test_swift_runloop with entitlements"
        VERBATIM
    )

    # test_swift_server_mock - Server-like Swift program
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/bin/test_swift_server_mock
        COMMAND swiftc -O -parse-as-library
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_swift_server_mock.swift
            -o ${CMAKE_BINARY_DIR}/bin/test_swift_server_mock
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_swift_server_mock.swift
        COMMENT "Building test_swift_server_mock"
    )
    add_custom_target(test_swift_server_mock ALL
        DEPENDS ${CMAKE_BINARY_DIR}/bin/test_swift_server_mock)

    # Sign test_swift_server_mock
    add_custom_command(
        TARGET test_swift_server_mock POST_BUILD
        COMMAND codesign -s "${SIGNING_IDENTITY}" --timestamp=none --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force ${CMAKE_BINARY_DIR}/bin/test_swift_server_mock
        COMMENT "Signing test_swift_server_mock with entitlements"
        VERBATIM
    )

    # test_swiftui_app - SwiftUI GUI application built with Xcode toolchain
    # This replicates real-world Xcode-built apps where most Swift symbols are
    # local (non-exported) and only main/_mh_execute_header are exported.
    # Used for end-to-end testing to validate hypothesis about resolve_export_address().
    set(XCODE_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/TestSwiftUIApp")
    set(XCODE_DERIVED_DATA "${CMAKE_BINARY_DIR}/xcode_derived")

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/bin/test_swiftui_app
        COMMAND xcodebuild
            -project "${XCODE_PROJECT_DIR}/TestSwiftUIApp.xcodeproj"
            -scheme TestSwiftUIApp
            -configuration Release
            -derivedDataPath "${XCODE_DERIVED_DATA}"
            -quiet
            build
        COMMAND ${CMAKE_COMMAND} -E copy
            "${XCODE_DERIVED_DATA}/Build/Products/Release/TestSwiftUIApp.app/Contents/MacOS/TestSwiftUIApp"
            "${CMAKE_BINARY_DIR}/bin/test_swiftui_app"
        DEPENDS "${XCODE_PROJECT_DIR}/TestSwiftUIApp/TestSwiftUIAppApp.swift"
                "${XCODE_PROJECT_DIR}/TestSwiftUIApp/Info.plist"
        COMMENT "Building test_swiftui_app with Xcode toolchain (end-to-end fixture)"
    )
    add_custom_target(test_swiftui_app ALL
        DEPENDS ${CMAKE_BINARY_DIR}/bin/test_swiftui_app)

    # Sign test_swiftui_app
    add_custom_command(
        TARGET test_swiftui_app POST_BUILD
        COMMAND codesign -s "${SIGNING_IDENTITY}" --timestamp=none --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force ${CMAKE_BINARY_DIR}/bin/test_swiftui_app
        COMMENT "Signing test_swiftui_app with entitlements"
        VERBATIM
    )
endif()

# ===========================================
# Add Test Subdirectories
# ===========================================

# Unit tests
add_subdirectory(unit/utils)
add_subdirectory(unit/backpressure)
add_subdirectory(unit/drain_thread)
add_subdirectory(unit/timer)
add_subdirectory(unit/controller)
add_subdirectory(unit/agent)
add_subdirectory(unit/atf)
add_subdirectory(unit/cli_parser)
add_subdirectory(unit/selective_persistence)
add_subdirectory(unit/metrics)
add_subdirectory(unit/docs)

# Integration tests
add_subdirectory(integration/utils)
add_subdirectory(integration/drain_thread)
add_subdirectory(integration/controller)
add_subdirectory(integration/timer)
add_subdirectory(integration/agent)
add_subdirectory(integration/atf)
add_subdirectory(integration/selective_persistence)
add_subdirectory(integration/metrics)
add_subdirectory(integration/system)
add_subdirectory(integration/docs)
add_subdirectory(integration/examples)

# Benchmark tests (when available)
add_subdirectory(bench/utils)
add_subdirectory(bench/controller)
add_subdirectory(bench/agent)
add_subdirectory(bench/registry)
add_subdirectory(bench/backpressure)
add_subdirectory(bench/e2e)

# ===========================================
# Custom Targets for Running Tests
# ===========================================

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests"
)

# Custom target to run only unit tests
add_custom_target(run_unit_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -L unit --output-on-failure
    COMMENT "Running unit tests"
)

# Custom target to run only integration tests
add_custom_target(run_integration_tests
    COMMAND ${CMAKE_CTEST_COMMAND} -L integration --output-on-failure
    COMMENT "Running integration tests"
)
