# ===========================================
# Controller Unit Tests
# ===========================================

# Check if Frida is available
find_library(FRIDA_CORE_TEST
    NAMES frida-core
    PATHS ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
    NO_DEFAULT_PATH
)

if(FRIDA_CORE_TEST)
    # Spawn method test
    add_executable(test_spawn_method
        test_spawn_method.cpp
    )
    target_include_directories(test_spawn_method
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/../third_parties/frida-core
            ${CMAKE_BINARY_DIR}  # For ada_paths.h
    )
    target_link_libraries(test_spawn_method
        PRIVATE
            test_main
            GTest::gtest
            GTest::gmock
            tracer_controller
            tracer_utils
            Threads::Threads
    )
    
    # Test Discovery
    gtest_discover_tests(test_spawn_method
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "unit"
    )
    
    # Install target
    install(TARGETS
        test_spawn_method
        RUNTIME DESTINATION bin
    )
endif()