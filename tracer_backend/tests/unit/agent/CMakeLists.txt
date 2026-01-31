# ===========================================
# Agent Unit Tests
# ===========================================

# Agent mode state machine tests (no Frida dependency)

add_executable(test_agent_mode
    test_agent_mode.cpp
)
target_include_directories(test_agent_mode
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_agent_mode
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_agent_mode
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_agent_mode
    RUNTIME DESTINATION bin
)

# Exclude list unit tests (no frida dependency)
add_executable(test_exclude_list
    test_exclude_list.cpp
)
target_include_directories(test_exclude_list
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_exclude_list
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        agent_utils
        tracer_utils
)

gtest_discover_tests(test_exclude_list
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_exclude_list
    RUNTIME DESTINATION bin
)

# Additional agent state machine tests
add_executable(test_agent_state_machine
    test_agent_state_machine.cpp
)
target_include_directories(test_agent_state_machine
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_agent_state_machine
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_agent_state_machine
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_agent_state_machine
    RUNTIME DESTINATION bin
)

# Agent coverage tests - exercises agent code paths for coverage
add_executable(test_agent_coverage
    test_agent_coverage.cpp
)
target_include_directories(test_agent_coverage
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_agent_coverage
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_agent_coverage
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_agent_coverage
    RUNTIME DESTINATION bin
)

# Agent mode tick unit test
add_executable(test_agent_mode_tick
    test_agent_mode_tick_before_captures.cpp
)
target_include_directories(test_agent_mode_tick
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_agent_mode_tick
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_agent_mode_tick
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

install(TARGETS
    test_agent_mode_tick
    RUNTIME DESTINATION bin
)

# DSO management unit tests
add_executable(test_dso_management
    test_dso_management.cpp
)
target_include_directories(test_dso_management
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}  # For ada_paths.h
)
target_link_libraries(test_dso_management
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        agent_utils
        tracer_utils
)
gtest_discover_tests(test_dso_management
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
install(TARGETS
    test_dso_management
    RUNTIME DESTINATION bin
)

# Hook registry unit tests
add_executable(test_hook_registry
    test_hook_registry.cpp
)
target_include_directories(test_hook_registry
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
)
target_link_libraries(test_hook_registry
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        agent_utils
        tracer_utils
)
gtest_discover_tests(test_hook_registry
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
install(TARGETS
    test_hook_registry
    RUNTIME DESTINATION bin
)

# Comprehensive hooks unit tests
add_executable(test_comprehensive_hooks
    test_comprehensive_hooks.cpp
)
target_include_directories(test_comprehensive_hooks
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
)
target_link_libraries(test_comprehensive_hooks
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        agent_utils
        tracer_utils
)
gtest_discover_tests(test_comprehensive_hooks
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
install(TARGETS
    test_comprehensive_hooks
    RUNTIME DESTINATION bin
)

# M1_E1_I10: IndexEvent layout unit test
add_executable(test_index_event_layout
    test_index_event_layout.cpp
)
target_include_directories(test_index_event_layout
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
)
target_link_libraries(test_index_event_layout
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
)
gtest_discover_tests(test_index_event_layout
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
install(TARGETS
    test_index_event_layout
    RUNTIME DESTINATION bin
)

# Swift detection unit tests
add_executable(test_swift_detection
    test_swift_detection.cpp
)
target_include_directories(test_swift_detection
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
)
target_link_libraries(test_swift_detection
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        agent_utils
        tracer_utils
)
gtest_discover_tests(test_swift_detection
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
install(TARGETS
    test_swift_detection
    RUNTIME DESTINATION bin
)

# Debug dylib detection unit tests
add_executable(test_debug_dylib_detection
    test_debug_dylib_detection.cpp
)
target_include_directories(test_debug_dylib_detection
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
)
target_link_libraries(test_debug_dylib_detection
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        agent_utils
        tracer_utils
)
gtest_discover_tests(test_debug_dylib_detection
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
install(TARGETS
    test_debug_dylib_detection
    RUNTIME DESTINATION bin
)
