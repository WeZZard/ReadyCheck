# ATF v2 Integration Tests

add_executable(test_atf_v2_writer
    test_atf_v2_writer.cpp
)

target_link_libraries(test_atf_v2_writer
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_atf_writer
        tracer_utils
)

target_include_directories(test_atf_v2_writer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/atf
)

gtest_discover_tests(test_atf_v2_writer
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "integration"
)
