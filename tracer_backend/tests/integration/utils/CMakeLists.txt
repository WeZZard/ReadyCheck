# ===========================================
# Utils Integration Tests
# ===========================================

# Thread Registry Integration test
add_executable(test_thread_registry_integration
    test_thread_registry_integration.cpp
)
target_include_directories(test_thread_registry_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/utils  # For private headers
)
target_link_libraries(test_thread_registry_integration
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

# Test Discovery
gtest_discover_tests(test_thread_registry_integration
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "integration"
)

# Install target
install(TARGETS
    test_thread_registry_integration
    RUNTIME DESTINATION bin
)