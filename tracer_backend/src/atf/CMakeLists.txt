# ATF v4 (protobuf-based, deprecated but still used)
find_program(PROTOC_C_EXECUTABLE protoc-c)
if(NOT PROTOC_C_EXECUTABLE)
    message(FATAL_ERROR "protoc-c compiler not found. Install protobuf-c (e.g. `brew install protobuf-c`).")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PROTOBUF_C REQUIRED libprotobuf-c)

set(ATF_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(ATF_PROTO_FILES
    ${ATF_PROTO_DIR}/trace_schema.proto
    ${ATF_PROTO_DIR}/google/protobuf/timestamp.proto
)

set(ATF_PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(ATF_GENERATED_SOURCES
    ${ATF_PROTO_OUTPUT_DIR}/trace_schema.pb-c.c
    ${ATF_PROTO_OUTPUT_DIR}/google/protobuf/timestamp.pb-c.c
)
set(ATF_GENERATED_HEADERS
    ${ATF_PROTO_OUTPUT_DIR}/trace_schema.pb-c.h
    ${ATF_PROTO_OUTPUT_DIR}/google/protobuf/timestamp.pb-c.h
)

add_custom_command(
    OUTPUT ${ATF_GENERATED_SOURCES} ${ATF_GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ATF_PROTO_OUTPUT_DIR}
    COMMAND ${PROTOC_C_EXECUTABLE}
        --proto_path=${ATF_PROTO_DIR}
        --c_out=${ATF_PROTO_OUTPUT_DIR}
        ${ATF_PROTO_FILES}
    DEPENDS ${ATF_PROTO_FILES}
    COMMENT "Generating ATF protobuf sources"
    VERBATIM
)

# ATF v2 (raw binary format, current)
add_library(tracer_atf_writer STATIC
    # ATF v4 (deprecated)
    atf_v4_writer.c
    ${ATF_GENERATED_SOURCES}
    # ATF v2 (current)
    thread_counters.c
    atf_index_writer.c
    atf_detail_writer.c
    atf_thread_writer.c
)

target_include_directories(tracer_atf_writer
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${ATF_PROTO_OUTPUT_DIR}
        ${PROTOBUF_C_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tracer_atf_writer
    PUBLIC
        c_std_11
)

set_target_properties(tracer_atf_writer PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(tracer_atf_writer
    PUBLIC
        tracer_utils
        ${PROTOBUF_C_LIBRARIES}
)

if(PROTOBUF_C_CFLAGS_OTHER)
    target_compile_options(tracer_atf_writer PRIVATE ${PROTOBUF_C_CFLAGS_OTHER})
endif()

if(PROTOBUF_C_LIBRARY_DIRS)
    target_link_directories(tracer_atf_writer PRIVATE ${PROTOBUF_C_LIBRARY_DIRS})
endif()
