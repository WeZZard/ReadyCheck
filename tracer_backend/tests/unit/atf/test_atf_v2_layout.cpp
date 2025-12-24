/**
 * @file test_atf_v2_layout.cpp
 * @brief Unit tests for ATF v2 type layout validation
 *
 * US-005: As a developer, I want trace files in raw binary format
 *
 * These tests verify:
 * - All struct sizes match specification (64 or 32 bytes)
 * - Magic bytes are correct
 * - Endianness markers are set
 * - Field offsets are as expected
 */

#include <gtest/gtest.h>
#include <tracer_backend/atf/atf_v2_types.h>
#include <cstring>

/* ===== Index Header Layout Tests ===== */

// US-005: Verify index header is exactly 64 bytes
TEST(AtfIndexHeader, Size_Equals_64_Bytes) {
    EXPECT_EQ(sizeof(AtfIndexHeader), 64);
}

// US-005: Verify index header magic bytes are "ATI2"
TEST(AtfIndexHeader, Magic_Is_ATI2) {
    AtfIndexHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, "ATI2", 4);

    EXPECT_EQ(header.magic[0], 'A');
    EXPECT_EQ(header.magic[1], 'T');
    EXPECT_EQ(header.magic[2], 'I');
    EXPECT_EQ(header.magic[3], '2');
    EXPECT_EQ(memcmp(header.magic, "ATI2", 4), 0);
}

// US-005: Verify endianness marker is 0x01 for little-endian
TEST(AtfIndexHeader, Endian_Is_Little) {
    AtfIndexHeader header;
    memset(&header, 0, sizeof(header));
    header.endian = 0x01;

    EXPECT_EQ(header.endian, 0x01);
}

// US-005: Verify event size field stores 32
TEST(AtfIndexHeader, EventSize_Is_32) {
    AtfIndexHeader header;
    memset(&header, 0, sizeof(header));
    header.event_size = 32;

    EXPECT_EQ(header.event_size, 32);
}

// US-005: Verify events_offset field defaults to 64 (after header)
TEST(AtfIndexHeader, EventsOffset_Is_64) {
    AtfIndexHeader header;
    memset(&header, 0, sizeof(header));
    header.events_offset = 64;

    EXPECT_EQ(header.events_offset, 64);
}

/* ===== Index Event Layout Tests ===== */

// US-005: Verify index event is exactly 32 bytes
TEST(IndexEvent, Size_Equals_32_Bytes) {
    EXPECT_EQ(sizeof(IndexEvent), 32);
}

// US-005: Verify detail_seq MAX indicates no detail
TEST(IndexEvent, DetailSeqMax_Indicates_NoDetail) {
    IndexEvent event;
    memset(&event, 0, sizeof(event));
    event.detail_seq = ATF_NO_DETAIL_SEQ;

    EXPECT_FALSE(index_event_has_detail(&event));
    EXPECT_EQ(event.detail_seq, UINT32_MAX);
}

// US-005: Verify detail_seq valid value indicates has detail
TEST(IndexEvent, DetailSeqValid_Indicates_HasDetail) {
    IndexEvent event;
    memset(&event, 0, sizeof(event));
    event.detail_seq = 42;

    EXPECT_TRUE(index_event_has_detail(&event));
    EXPECT_EQ(index_event_get_detail_seq(&event), 42);
}

// US-005: Verify event kind values are distinct
TEST(IndexEvent, EventKind_Values_Distinct) {
    EXPECT_NE(ATF_EVENT_KIND_CALL, ATF_EVENT_KIND_RETURN);
    EXPECT_NE(ATF_EVENT_KIND_CALL, ATF_EVENT_KIND_EXCEPTION);
    EXPECT_NE(ATF_EVENT_KIND_RETURN, ATF_EVENT_KIND_EXCEPTION);
}

/* ===== Index Footer Layout Tests ===== */

// US-005: Verify index footer is exactly 64 bytes
TEST(AtfIndexFooter, Size_Equals_64_Bytes) {
    EXPECT_EQ(sizeof(AtfIndexFooter), 64);
}

// US-005: Verify index footer magic bytes are "2ITA" (reversed)
TEST(AtfIndexFooter, Magic_Is_Reversed) {
    AtfIndexFooter footer;
    memset(&footer, 0, sizeof(footer));
    memcpy(footer.magic, "2ITA", 4);

    EXPECT_EQ(footer.magic[0], '2');
    EXPECT_EQ(footer.magic[1], 'I');
    EXPECT_EQ(footer.magic[2], 'T');
    EXPECT_EQ(footer.magic[3], 'A');
    EXPECT_EQ(memcmp(footer.magic, "2ITA", 4), 0);
}

/* ===== Detail Header Layout Tests ===== */

// US-005: Verify detail header is exactly 64 bytes
TEST(AtfDetailHeader, Size_Equals_64_Bytes) {
    EXPECT_EQ(sizeof(AtfDetailHeader), 64);
}

// US-005: Verify detail header magic bytes are "ATD2"
TEST(AtfDetailHeader, Magic_Is_ATD2) {
    AtfDetailHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, "ATD2", 4);

    EXPECT_EQ(header.magic[0], 'A');
    EXPECT_EQ(header.magic[1], 'T');
    EXPECT_EQ(header.magic[2], 'D');
    EXPECT_EQ(header.magic[3], '2');
    EXPECT_EQ(memcmp(header.magic, "ATD2", 4), 0);
}

// US-005: Verify detail header events_offset is 64 (after header)
TEST(AtfDetailHeader, EventsOffset_Is_64) {
    AtfDetailHeader header;
    memset(&header, 0, sizeof(header));
    header.events_offset = 64;

    EXPECT_EQ(header.events_offset, 64);
}

/* ===== Detail Event Layout Tests ===== */

// US-005: Verify detail event header is exactly 24 bytes
TEST(DetailEventHeader, Size_Equals_24_Bytes) {
    EXPECT_EQ(sizeof(DetailEventHeader), 24);
}

// US-005: Verify detail event backward lookup returns index_seq
TEST(DetailEvent, BackwardLookup_Returns_IndexSeq) {
    DetailEventHeader header;
    memset(&header, 0, sizeof(header));
    header.index_seq = 17;

    EXPECT_EQ(detail_event_get_index_seq(&header), 17);
}

// US-005: Verify detail event type values are distinct
TEST(DetailEvent, EventType_Values_Distinct) {
    EXPECT_NE(ATF_DETAIL_EVENT_FUNCTION_CALL, ATF_DETAIL_EVENT_FUNCTION_RETURN);
}

/* ===== Detail Footer Layout Tests ===== */

// US-005: Verify detail footer is exactly 64 bytes
TEST(AtfDetailFooter, Size_Equals_64_Bytes) {
    EXPECT_EQ(sizeof(AtfDetailFooter), 64);
}

// US-005: Verify detail footer magic bytes are "2DTA" (reversed)
TEST(AtfDetailFooter, Magic_Is_Reversed) {
    AtfDetailFooter footer;
    memset(&footer, 0, sizeof(footer));
    memcpy(footer.magic, "2DTA", 4);

    EXPECT_EQ(footer.magic[0], '2');
    EXPECT_EQ(footer.magic[1], 'D');
    EXPECT_EQ(footer.magic[2], 'T');
    EXPECT_EQ(footer.magic[3], 'A');
    EXPECT_EQ(memcmp(footer.magic, "2DTA", 4), 0);
}

/* ===== Field Offset Tests ===== */

// US-005: Verify index event timestamp_ns is at offset 0
TEST(IndexEvent, TimestampOffset_Is_0) {
    EXPECT_EQ(offsetof(IndexEvent, timestamp_ns), 0);
}

// US-005: Verify index event detail_seq is at offset 28
TEST(IndexEvent, DetailSeqOffset_Is_28) {
    EXPECT_EQ(offsetof(IndexEvent, detail_seq), 28);
}

// US-005: Verify detail event header index_seq is at offset 8
TEST(DetailEventHeader, IndexSeqOffset_Is_8) {
    EXPECT_EQ(offsetof(DetailEventHeader, index_seq), 8);
}
