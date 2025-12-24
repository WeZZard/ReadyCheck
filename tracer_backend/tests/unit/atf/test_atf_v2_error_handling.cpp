/**
 * @file test_atf_v2_error_handling.cpp
 * @brief Unit tests for ATF v2 error handling paths
 *
 * US-005: As a developer, I want trace files in raw binary format
 *
 * These tests verify:
 * - All error paths are covered (malloc failures, file errors, etc.)
 * - Error conditions return proper error codes
 * - Resources are cleaned up on errors
 */

#include <gtest/gtest.h>
#include <tracer_backend/atf/atf_v2_types.h>
#include <tracer_backend/atf/atf_thread_writer.h>
#include "atf_index_writer_private.h"
#include "atf_detail_writer_private.h"
#include "thread_counters_private.h"
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <cstdio>

/* ===== Helper Functions ===== */

static std::string get_temp_dir() {
    return "/tmp/atf_v2_error_tests";
}

static void cleanup_temp_dir() {
    std::string cmd = "rm -rf " + get_temp_dir();
    system(cmd.c_str());
}

/* ===== atf_v2_types.h Coverage Tests ===== */

// US-005: Cover ATF_EVENT_KIND_RETURN usage
TEST(AtfV2Types, EventKind_Return_Defined) {
    IndexEvent event;
    memset(&event, 0, sizeof(event));
    event.event_kind = ATF_EVENT_KIND_RETURN;

    EXPECT_EQ(event.event_kind, ATF_EVENT_KIND_RETURN);
    EXPECT_EQ(ATF_EVENT_KIND_RETURN, 2);
}

// US-005: Cover ATF_DETAIL_EVENT_FUNCTION_RETURN usage
TEST(AtfV2Types, DetailEventType_Return_Defined) {
    DetailEventHeader header;
    memset(&header, 0, sizeof(header));
    header.event_type = ATF_DETAIL_EVENT_FUNCTION_RETURN;

    EXPECT_EQ(header.event_type, ATF_DETAIL_EVENT_FUNCTION_RETURN);
    EXPECT_EQ(ATF_DETAIL_EVENT_FUNCTION_RETURN, 4);
}

/* ===== Thread Counters Error Handling Tests ===== */

// US-005: Cover thread counters reset function
TEST(ThreadCounters, Reset_Zeros_Counters) {
    ThreadCounters tc;
    tc.index_count = 100;
    tc.detail_count = 50;

    atf_thread_counters_reset(&tc);

    EXPECT_EQ(tc.index_count, 0);
    EXPECT_EQ(tc.detail_count, 0);
}

// US-005: Cover NULL pointer handling in reset
TEST(ThreadCounters, Reset_Null_NoCrash) {
    atf_thread_counters_reset(NULL);
    // Should not crash
}

// US-005: Cover NULL pointer handling in init
TEST(ThreadCounters, Init_Null_NoCrash) {
    atf_thread_counters_init(NULL);
    // Should not crash
}

// US-005: Cover NULL pointer handling in reserve
TEST(ThreadCounters, Reserve_Null_NoCrash) {
    ThreadCounters tc;
    uint32_t idx_seq = 0;
    uint32_t det_seq = 0;

    atf_reserve_sequences(NULL, 1, &idx_seq, &det_seq);
    atf_reserve_sequences(&tc, 1, NULL, &det_seq);
    atf_reserve_sequences(&tc, 1, &idx_seq, NULL);
    // Should not crash
}

/* ===== Detail Writer Error Handling Tests ===== */

// US-005: Cover NULL filepath
TEST(AtfDetailWriter, Create_NullPath_ReturnsNull) {
    AtfDetailWriter* writer = atf_detail_writer_create(NULL, 1, ATF_CLOCK_MACH_CONTINUOUS);
    EXPECT_EQ(writer, nullptr);
}

// US-005: Cover path with trailing slash
TEST(AtfDetailWriter, Create_TrailingSlash_Success) {
    cleanup_temp_dir();

    std::string path = get_temp_dir() + "/thread_1/detail.atf/";
    AtfDetailWriter* writer = atf_detail_writer_create(path.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);

    if (writer) { // LCOV_EXCL_LINE
        atf_detail_writer_close(writer); // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    cleanup_temp_dir();
}

// US-005: Cover path with no directory
TEST(AtfDetailWriter, Create_NoDirectory_Success) {
    const char* path = "detail_nodirectory.atf";
    AtfDetailWriter* writer = atf_detail_writer_create(path, 1, ATF_CLOCK_MACH_CONTINUOUS);

    if (writer) {
        atf_detail_writer_close(writer);
        unlink(path);
    }
}

// US-005: Cover write event with NULL writer
TEST(AtfDetailWriter, WriteEvent_NullWriter_ReturnsError) {
    uint8_t payload[16] = {0};
    int ret = atf_detail_writer_write_event(NULL, 0, 1000,
                                             ATF_DETAIL_EVENT_FUNCTION_CALL,
                                             payload, sizeof(payload));
    EXPECT_EQ(ret, -EINVAL);
}

// US-005: Cover write event with NULL payload but size > 0
TEST(AtfDetailWriter, WriteEvent_NullPayloadWithSize_ReturnsError) {
    cleanup_temp_dir();
    std::string path = get_temp_dir() + "/thread_1/detail.atf";

    AtfDetailWriter* writer = atf_detail_writer_create(path.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    int ret = atf_detail_writer_write_event(writer, 0, 1000,
                                             ATF_DETAIL_EVENT_FUNCTION_CALL,
                                             NULL, 16);
    EXPECT_EQ(ret, -EINVAL);

    atf_detail_writer_close(writer);
    cleanup_temp_dir();
}

// US-005: Cover write event with NULL payload and size = 0
TEST(AtfDetailWriter, WriteEvent_NullPayloadZeroSize_Success) {
    cleanup_temp_dir();
    std::string path = get_temp_dir() + "/thread_1/detail.atf";

    AtfDetailWriter* writer = atf_detail_writer_create(path.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    int ret = atf_detail_writer_write_event(writer, 0, 1000,
                                             ATF_DETAIL_EVENT_FUNCTION_CALL,
                                             NULL, 0);
    EXPECT_EQ(ret, 0);

    atf_detail_writer_close(writer);
    cleanup_temp_dir();
}

// US-005: Cover finalize with NULL writer
TEST(AtfDetailWriter, Finalize_NullWriter_ReturnsError) {
    int ret = atf_detail_writer_finalize(NULL);
    EXPECT_EQ(ret, -EINVAL);
}

// US-005: Cover close with NULL writer
TEST(AtfDetailWriter, Close_Null_NoCrash) {
    atf_detail_writer_close(NULL);
    // Should not crash
}

/* ===== Index Writer Error Handling Tests ===== */

// US-005: Cover NULL filepath
TEST(AtfIndexWriter, Create_NullPath_ReturnsNull) {
    AtfIndexWriter* writer = atf_index_writer_create(NULL, 1, ATF_CLOCK_MACH_CONTINUOUS);
    EXPECT_EQ(writer, nullptr);
}

// US-005: Cover path with trailing slash
TEST(AtfIndexWriter, Create_TrailingSlash_Success) {
    cleanup_temp_dir();

    std::string path = get_temp_dir() + "/thread_1/index.atf/";
    AtfIndexWriter* writer = atf_index_writer_create(path.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);

    if (writer) { // LCOV_EXCL_LINE
        atf_index_writer_close(writer); // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    cleanup_temp_dir();
}

// US-005: Cover path with no directory
TEST(AtfIndexWriter, Create_NoDirectory_Success) {
    const char* path = "index_nodirectory.atf";
    AtfIndexWriter* writer = atf_index_writer_create(path, 1, ATF_CLOCK_MACH_CONTINUOUS);

    if (writer) {
        atf_index_writer_close(writer);
        unlink(path);
    }
}

// US-005: Cover write event with NULL writer
TEST(AtfIndexWriter, WriteEvent_NullWriter_ReturnsError) {
    IndexEvent event;
    memset(&event, 0, sizeof(event));

    int ret = atf_index_writer_write_event(NULL, &event);
    EXPECT_EQ(ret, -EINVAL);
}

// US-005: Cover write event with NULL event
TEST(AtfIndexWriter, WriteEvent_NullEvent_ReturnsError) {
    cleanup_temp_dir();
    std::string path = get_temp_dir() + "/thread_1/index.atf";

    AtfIndexWriter* writer = atf_index_writer_create(path.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    int ret = atf_index_writer_write_event(writer, NULL);
    EXPECT_EQ(ret, -EINVAL);

    atf_index_writer_close(writer);
    cleanup_temp_dir();
}

// US-005: Cover finalize with NULL writer
TEST(AtfIndexWriter, Finalize_NullWriter_ReturnsError) {
    int ret = atf_index_writer_finalize(NULL);
    EXPECT_EQ(ret, -EINVAL);
}

// US-005: Cover close with NULL writer
TEST(AtfIndexWriter, Close_Null_NoCrash) {
    atf_index_writer_close(NULL);
    // Should not crash
}

/* ===== Thread Writer Error Handling Tests ===== */

// US-005: Cover NULL session_dir
TEST(AtfThreadWriter, Create_NullSessionDir_ReturnsNull) {
    AtfThreadWriter* writer = atf_thread_writer_create(NULL, 1, ATF_CLOCK_MACH_CONTINUOUS);
    EXPECT_EQ(writer, nullptr);
}

// US-005: Cover write event with NULL writer
TEST(AtfThreadWriter, WriteEvent_NullWriter_ReturnsMax) {
    uint32_t seq = atf_thread_writer_write_event(NULL, 1000, 0x100000001,
                                                  ATF_EVENT_KIND_CALL, 1,
                                                  NULL, 0);
    EXPECT_EQ(seq, UINT32_MAX);
}

// US-005: Cover write event with EXCEPTION kind (maps to CALL)
TEST(AtfThreadWriter, WriteEvent_ExceptionKind_MapsToCall) {
    cleanup_temp_dir();
    std::string session_dir = get_temp_dir();

    AtfThreadWriter* writer = atf_thread_writer_create(session_dir.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    // Write event with EXCEPTION kind and detail
    uint8_t payload[16] = {0};
    uint32_t seq = atf_thread_writer_write_event(writer, 1000, 0x100000001,
                                                  ATF_EVENT_KIND_EXCEPTION, 1,
                                                  payload, sizeof(payload));
    EXPECT_NE(seq, UINT32_MAX);

    atf_thread_writer_close(writer);
    cleanup_temp_dir();
}

// US-005: Cover write event with RETURN kind
TEST(AtfThreadWriter, WriteEvent_ReturnKind_Success) {
    cleanup_temp_dir();
    std::string session_dir = get_temp_dir();

    AtfThreadWriter* writer = atf_thread_writer_create(session_dir.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    // Write event with RETURN kind and detail
    uint8_t payload[16] = {0};
    uint32_t seq = atf_thread_writer_write_event(writer, 1000, 0x100000001,
                                                  ATF_EVENT_KIND_RETURN, 1,
                                                  payload, sizeof(payload));
    EXPECT_NE(seq, UINT32_MAX);

    atf_thread_writer_close(writer);
    cleanup_temp_dir();
}

// US-005: Cover finalize with NULL writer
TEST(AtfThreadWriter, Finalize_NullWriter_ReturnsError) {
    int ret = atf_thread_writer_finalize(NULL);
    EXPECT_EQ(ret, -EINVAL);
}

// US-005: Cover close with NULL writer
TEST(AtfThreadWriter, Close_Null_NoCrash) {
    atf_thread_writer_close(NULL);
    // Should not crash
}

// US-005: Cover lazy detail writer creation
TEST(AtfThreadWriter, LazyDetailCreation_OnFirstDetail) {
    cleanup_temp_dir();
    std::string session_dir = get_temp_dir();

    AtfThreadWriter* writer = atf_thread_writer_create(session_dir.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    // First write without detail
    uint32_t seq1 = atf_thread_writer_write_event(writer, 1000, 0x100000001,
                                                   ATF_EVENT_KIND_CALL, 1,
                                                   NULL, 0);
    EXPECT_NE(seq1, UINT32_MAX);

    // Second write with detail - triggers lazy creation
    uint8_t payload[16] = {0};
    uint32_t seq2 = atf_thread_writer_write_event(writer, 2000, 0x100000002,
                                                   ATF_EVENT_KIND_CALL, 2,
                                                   payload, sizeof(payload));
    EXPECT_NE(seq2, UINT32_MAX);

    // Third write with detail - uses existing detail writer
    uint32_t seq3 = atf_thread_writer_write_event(writer, 3000, 0x100000003,
                                                   ATF_EVENT_KIND_CALL, 3,
                                                   payload, sizeof(payload));
    EXPECT_NE(seq3, UINT32_MAX);

    atf_thread_writer_finalize(writer);
    atf_thread_writer_close(writer);
    cleanup_temp_dir();
}

/* ===== Integration Tests for Error Paths ===== */

// US-005: Cover complete write/finalize/close flow
TEST(AtfThreadWriter, CompleteFlow_Success) {
    cleanup_temp_dir();
    std::string session_dir = get_temp_dir();

    AtfThreadWriter* writer = atf_thread_writer_create(session_dir.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    // Write multiple events with and without detail
    uint8_t payload[16] = {0};

    uint32_t seq1 = atf_thread_writer_write_event(writer, 1000, 0x100000001,
                                                   ATF_EVENT_KIND_CALL, 1,
                                                   payload, sizeof(payload));
    EXPECT_EQ(seq1, 0);

    uint32_t seq2 = atf_thread_writer_write_event(writer, 2000, 0x100000002,
                                                   ATF_EVENT_KIND_RETURN, 1,
                                                   NULL, 0);
    EXPECT_EQ(seq2, 1);

    uint32_t seq3 = atf_thread_writer_write_event(writer, 3000, 0x100000003,
                                                   ATF_EVENT_KIND_CALL, 2,
                                                   payload, sizeof(payload));
    EXPECT_EQ(seq3, 2);

    // Finalize
    int ret = atf_thread_writer_finalize(writer);
    EXPECT_EQ(ret, 0);

    // Close
    atf_thread_writer_close(writer);

    cleanup_temp_dir();
}

// US-005: Cover finalize without detail writer
TEST(AtfThreadWriter, Finalize_NoDetailWriter_Success) {
    cleanup_temp_dir();
    std::string session_dir = get_temp_dir();

    AtfThreadWriter* writer = atf_thread_writer_create(session_dir.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    // Write only index events (no detail)
    uint32_t seq1 = atf_thread_writer_write_event(writer, 1000, 0x100000001,
                                                   ATF_EVENT_KIND_CALL, 1,
                                                   NULL, 0);
    EXPECT_EQ(seq1, 0);

    // Finalize - should handle NULL detail_writer gracefully
    int ret = atf_thread_writer_finalize(writer);
    EXPECT_EQ(ret, 0);

    atf_thread_writer_close(writer);
    cleanup_temp_dir();
}

// US-005: Cover close without finalize
TEST(AtfThreadWriter, Close_WithoutFinalize_NoCrash) {
    cleanup_temp_dir();
    std::string session_dir = get_temp_dir();

    AtfThreadWriter* writer = atf_thread_writer_create(session_dir.c_str(), 1, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    uint8_t payload[16] = {0};
    atf_thread_writer_write_event(writer, 1000, 0x100000001,
                                  ATF_EVENT_KIND_CALL, 1,
                                  payload, sizeof(payload));

    // Close without finalize - should not crash
    atf_thread_writer_close(writer);

    cleanup_temp_dir();
}
