# ===========================================
# Agent Integration Tests
# ===========================================

# Check if Frida is available
find_library(FRIDA_CORE_TEST
    NAMES frida-core
    PATHS ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
    NO_DEFAULT_PATH
)

if(FRIDA_CORE_TEST)
    # Agent loader test
    add_executable(test_agent_loader
        test_agent_loader.cpp
    )
    target_include_directories(test_agent_loader
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_agent_loader
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )

    # Baseline hooks test
    add_executable(test_baseline_hooks
        test_baseline_hooks.cpp
    )
    target_include_directories(test_baseline_hooks
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-gum
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_baseline_hooks
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            frida_agent
            tracer_utils
            Threads::Threads
    )

    # Registry mode handling tests
    add_executable(test_agent_registry_modes
        test_agent_registry_modes.cpp
    )
    target_include_directories(test_agent_registry_modes
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-gum
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_agent_registry_modes
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            frida_agent
            tracer_utils
            Threads::Threads
    )
    
    # Test Discovery
    gtest_discover_tests(test_agent_loader
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    gtest_discover_tests(test_baseline_hooks
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    gtest_discover_tests(test_agent_registry_modes
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )
    
    # Swift hooks integration test
    add_executable(test_swift_hooks
        test_swift_hooks.cpp
    )
    target_include_directories(test_swift_hooks
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-gum
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_swift_hooks
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            frida_agent
            tracer_utils
            Threads::Threads
    )
    gtest_discover_tests(test_swift_hooks
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )

    # Sign test binaries with debugger entitlements (required for Frida spawn/attach)
    if(APPLE)
        # Get signing identity from parent scope
        if(DEFINED ENV{APPLE_DEVELOPER_ID})
            set(TEST_SIGNING_IDENTITY "$ENV{APPLE_DEVELOPER_ID}")
        else()
            set(TEST_SIGNING_IDENTITY "-")
        endif()

        add_custom_command(TARGET test_swift_hooks POST_BUILD
            COMMAND codesign -s "${TEST_SIGNING_IDENTITY}" --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force $<TARGET_FILE:test_swift_hooks>
            COMMENT "Signing test_swift_hooks with debugger entitlements"
            VERBATIM
        )
        add_custom_command(TARGET test_agent_loader POST_BUILD
            COMMAND codesign -s "${TEST_SIGNING_IDENTITY}" --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force $<TARGET_FILE:test_agent_loader>
            COMMENT "Signing test_agent_loader with debugger entitlements"
            VERBATIM
        )
        add_custom_command(TARGET test_baseline_hooks POST_BUILD
            COMMAND codesign -s "${TEST_SIGNING_IDENTITY}" --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force $<TARGET_FILE:test_baseline_hooks>
            COMMENT "Signing test_baseline_hooks with debugger entitlements"
            VERBATIM
        )
        add_custom_command(TARGET test_agent_registry_modes POST_BUILD
            COMMAND codesign -s "${TEST_SIGNING_IDENTITY}" --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force $<TARGET_FILE:test_agent_registry_modes>
            COMMENT "Signing test_agent_registry_modes with debugger entitlements"
            VERBATIM
        )
    endif()

    # Debug dylib detection integration test
    add_executable(test_debug_dylib_integration
        test_debug_dylib_detection.cpp
    )
    target_include_directories(test_debug_dylib_integration
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-gum
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_debug_dylib_integration
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            agent_utils
            tracer_utils
            Threads::Threads
    )
    gtest_discover_tests(test_debug_dylib_integration
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "integration"
    )

    # Install targets
    install(TARGETS
        test_agent_loader
        test_baseline_hooks
        test_agent_registry_modes
        test_swift_hooks
        test_debug_dylib_integration
        RUNTIME DESTINATION bin
    )
endif()
