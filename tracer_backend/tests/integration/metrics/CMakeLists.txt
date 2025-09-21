# ===========================================
# Metrics Integration Tests
# ===========================================

add_executable(test_metrics_integration
    test_metrics_integration.cpp
)

target_include_directories(test_metrics_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/utils
        ${CMAKE_SOURCE_DIR}/src/metrics
)

target_link_libraries(test_metrics_integration
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_utils
        Threads::Threads
)

gtest_discover_tests(test_metrics_integration
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "integration"
)

install(TARGETS
    test_metrics_integration
    RUNTIME DESTINATION bin
)
