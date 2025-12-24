/**
 * @file test_atf_v2_writer.cpp
 * @brief Integration tests for ATF v2 writer
 *
 * US-005: As a developer, I want trace files in raw binary format
 * Tech Spec: M1_E5_I1_TECH_DESIGN.md
 *
 * These tests verify:
 * - Index-only mode (no detail file created)
 * - Index + Detail mode (bidirectional linking)
 * - Writer finalization (headers and footers)
 * - File structure and format
 */

#include <gtest/gtest.h>
#include <tracer_backend/atf/atf_thread_writer.h>
#include <tracer_backend/atf/atf_v2_types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

/* Test fixture for ATF v2 writer tests */
class AtfV2WriterTest : public ::testing::Test {
protected:
    const char* test_dir = "/tmp/ada_atf_v2_test";

    void SetUp() override {
        /* Clean up test directory */
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", test_dir);
        system(cmd);

        /* Create test directory */
        mkdir(test_dir, 0755);
    }

    void TearDown() override {
        /* Clean up test directory */
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", test_dir);
        system(cmd);
    }

    bool file_exists(const char* path) {
        struct stat st;
        return (stat(path, &st) == 0);
    }

    size_t file_size(const char* path) {
        struct stat st;
        if (stat(path, &st) != 0) return 0;
        return st.st_size;
    }
};

/* ===== Index-Only Mode Tests ===== */

// US-005: Verify index-only mode does not create detail file
TEST_F(AtfV2WriterTest, index_only__no_detail_file__then_only_index_created) {
    AtfThreadWriter* writer = atf_thread_writer_create(test_dir, 0, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    /* Write 100 events without detail */
    for (int i = 0; i < 100; i++) {
        uint32_t idx_seq = atf_thread_writer_write_event(
            writer,
            i * 100,                    /* timestamp_ns */
            0x100000001,                /* function_id */
            ATF_EVENT_KIND_CALL,        /* event_kind */
            0,                          /* call_depth */
            nullptr,                    /* detail_payload */
            0                           /* detail_payload_size */
        );
        EXPECT_EQ(idx_seq, i);
    }

    /* Finalize */
    EXPECT_EQ(atf_thread_writer_finalize(writer), 0);
    atf_thread_writer_close(writer);

    /* Verify files */
    char index_path[1024];
    char detail_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/thread_0/index.atf", test_dir);
    snprintf(detail_path, sizeof(detail_path), "%s/thread_0/detail.atf", test_dir);

    EXPECT_TRUE(file_exists(index_path));
    EXPECT_FALSE(file_exists(detail_path));

    /* Verify index file size */
    size_t expected_size = sizeof(AtfIndexHeader) + 100 * sizeof(IndexEvent) + sizeof(AtfIndexFooter);
    EXPECT_EQ(file_size(index_path), expected_size);
}

// US-005: Verify index file header is correct
TEST_F(AtfV2WriterTest, index_only__header__then_fields_correct) {
    AtfThreadWriter* writer = atf_thread_writer_create(test_dir, 0, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    /* Write some events */
    for (int i = 0; i < 10; i++) {
        atf_thread_writer_write_event(writer, i * 100, 0x100000001,
                                      ATF_EVENT_KIND_CALL, 0, nullptr, 0);
    }

    atf_thread_writer_finalize(writer);
    atf_thread_writer_close(writer);

    /* Read and verify header */
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/thread_0/index.atf", test_dir);

    FILE* f = fopen(index_path, "rb");
    ASSERT_NE(f, nullptr);

    AtfIndexHeader header;
    EXPECT_EQ(fread(&header, sizeof(header), 1, f), 1);
    fclose(f);

    /* Verify header fields */
    EXPECT_EQ(memcmp(header.magic, "ATI2", 4), 0);
    EXPECT_EQ(header.endian, 0x01);
    EXPECT_EQ(header.version, 1);
    EXPECT_EQ(header.thread_id, 0);
    EXPECT_EQ(header.clock_type, ATF_CLOCK_MACH_CONTINUOUS);
    EXPECT_EQ(header.event_size, 32);
    EXPECT_EQ(header.event_count, 10);
    EXPECT_EQ(header.events_offset, 64);
    EXPECT_EQ(header.time_start_ns, 0);
    EXPECT_EQ(header.time_end_ns, 900);
}

/* ===== Index + Detail Mode Tests ===== */

// US-005: Verify index + detail mode creates both files
TEST_F(AtfV2WriterTest, index_detail__both_files__then_created) {
    AtfThreadWriter* writer = atf_thread_writer_create(test_dir, 0, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    /* Write events with detail */
    uint8_t payload[64] = {0};
    for (int i = 0; i < 50; i++) {
        uint32_t idx_seq = atf_thread_writer_write_event(
            writer,
            i * 100,
            0x100000001,
            ATF_EVENT_KIND_CALL,
            0,
            payload,
            sizeof(payload)
        );
        EXPECT_EQ(idx_seq, i);
    }

    atf_thread_writer_finalize(writer);
    atf_thread_writer_close(writer);

    /* Verify both files exist */
    char index_path[1024];
    char detail_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/thread_0/index.atf", test_dir);
    snprintf(detail_path, sizeof(detail_path), "%s/thread_0/detail.atf", test_dir);

    EXPECT_TRUE(file_exists(index_path));
    EXPECT_TRUE(file_exists(detail_path));
}

// US-005: Verify bidirectional linking in written files
TEST_F(AtfV2WriterTest, bidirectional__links__then_consistent) {
    AtfThreadWriter* writer = atf_thread_writer_create(test_dir, 0, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    /* Write mixed events: detail, no-detail, detail */
    uint8_t payload[32] = {0};

    /* Event 0: with detail */
    atf_thread_writer_write_event(writer, 0, 0x100000001, ATF_EVENT_KIND_CALL, 0, payload, sizeof(payload));

    /* Event 1: no detail */
    atf_thread_writer_write_event(writer, 100, 0x100000002, ATF_EVENT_KIND_CALL, 0, nullptr, 0);

    /* Event 2: with detail */
    atf_thread_writer_write_event(writer, 200, 0x100000003, ATF_EVENT_KIND_CALL, 0, payload, sizeof(payload));

    atf_thread_writer_finalize(writer);
    atf_thread_writer_close(writer);

    /* Read index file */
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/thread_0/index.atf", test_dir);

    FILE* idx_f = fopen(index_path, "rb");
    ASSERT_NE(idx_f, nullptr);

    /* Skip header */
    fseek(idx_f, sizeof(AtfIndexHeader), SEEK_SET);

    /* Read events */
    IndexEvent events[3];
    EXPECT_EQ(fread(events, sizeof(IndexEvent), 3, idx_f), 3);
    fclose(idx_f);

    /* Verify forward links */
    EXPECT_EQ(events[0].detail_seq, 0);  /* Has detail at seq 0 */
    EXPECT_EQ(events[1].detail_seq, ATF_NO_DETAIL_SEQ);  /* No detail */
    EXPECT_EQ(events[2].detail_seq, 1);  /* Has detail at seq 1 */
}

// US-005: Verify detail file header is correct
TEST_F(AtfV2WriterTest, detail__header__then_fields_correct) {
    AtfThreadWriter* writer = atf_thread_writer_create(test_dir, 0, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    /* Write events with detail */
    uint8_t payload[16] = {0};
    for (int i = 0; i < 5; i++) {
        atf_thread_writer_write_event(writer, i * 100, 0x100000001,
                                      ATF_EVENT_KIND_CALL, 0, payload, sizeof(payload));
    }

    atf_thread_writer_finalize(writer);
    atf_thread_writer_close(writer);

    /* Read detail header */
    char detail_path[1024];
    snprintf(detail_path, sizeof(detail_path), "%s/thread_0/detail.atf", test_dir);

    FILE* f = fopen(detail_path, "rb");
    ASSERT_NE(f, nullptr);

    AtfDetailHeader header;
    EXPECT_EQ(fread(&header, sizeof(header), 1, f), 1);
    fclose(f);

    /* Verify header fields */
    EXPECT_EQ(memcmp(header.magic, "ATD2", 4), 0);
    EXPECT_EQ(header.endian, 0x01);
    EXPECT_EQ(header.version, 1);
    EXPECT_EQ(header.thread_id, 0);
    EXPECT_EQ(header.events_offset, 64);
    EXPECT_EQ(header.event_count, 5);
}

/* ===== Finalization Tests ===== */

// US-005: Verify footer is written correctly
TEST_F(AtfV2WriterTest, finalize__footer__then_written) {
    AtfThreadWriter* writer = atf_thread_writer_create(test_dir, 0, ATF_CLOCK_MACH_CONTINUOUS);
    ASSERT_NE(writer, nullptr);

    /* Write events */
    for (int i = 0; i < 10; i++) {
        atf_thread_writer_write_event(writer, i * 100, 0x100000001,
                                      ATF_EVENT_KIND_CALL, 0, nullptr, 0);
    }

    atf_thread_writer_finalize(writer);
    atf_thread_writer_close(writer);

    /* Read footer */
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/thread_0/index.atf", test_dir);

    FILE* f = fopen(index_path, "rb");
    ASSERT_NE(f, nullptr);

    /* Seek to footer */
    size_t footer_offset = sizeof(AtfIndexHeader) + 10 * sizeof(IndexEvent);
    fseek(f, footer_offset, SEEK_SET);

    AtfIndexFooter footer;
    EXPECT_EQ(fread(&footer, sizeof(footer), 1, f), 1);
    fclose(f);

    /* Verify footer */
    EXPECT_EQ(memcmp(footer.magic, "2ITA", 4), 0);
    EXPECT_EQ(footer.event_count, 10);
    EXPECT_EQ(footer.time_start_ns, 0);
    EXPECT_EQ(footer.time_end_ns, 900);
}
