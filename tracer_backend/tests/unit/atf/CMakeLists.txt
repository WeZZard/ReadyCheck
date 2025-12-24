# ATF v2 Unit Tests

# Test: ATF v2 Layout Validation
add_executable(test_atf_v2_layout
    test_atf_v2_layout.cpp
)

target_link_libraries(test_atf_v2_layout
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
)

target_include_directories(test_atf_v2_layout
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/atf
)

gtest_discover_tests(test_atf_v2_layout
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

# Test: ATF v2 Bidirectional Linking
add_executable(test_atf_v2_linking
    test_atf_v2_linking.cpp
)

target_link_libraries(test_atf_v2_linking
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_atf_writer
)

target_include_directories(test_atf_v2_linking
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/atf
)

gtest_discover_tests(test_atf_v2_linking
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)

# Test: ATF v2 Error Handling
add_executable(test_atf_v2_error_handling
    test_atf_v2_error_handling.cpp
)

target_link_libraries(test_atf_v2_error_handling
    PRIVATE
        test_main
        GTest::gtest
        GTest::gmock
        tracer_atf_writer
)

target_include_directories(test_atf_v2_error_handling
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/atf
)

gtest_discover_tests(test_atf_v2_error_handling
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit"
)
