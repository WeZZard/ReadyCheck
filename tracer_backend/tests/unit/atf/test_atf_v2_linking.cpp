/**
 * @file test_atf_v2_linking.cpp
 * @brief Unit tests for ATF v2 bidirectional linking
 *
 * US-005: As a developer, I want trace files in raw binary format
 * Tech Spec: M1_E5_I1_TECH_DESIGN.md - Bidirectional Counter Design
 *
 * These tests verify:
 * - ThreadCounters reserves sequences atomically
 * - Forward linking (index -> detail) works correctly
 * - Backward linking (detail -> index) works correctly
 * - Sequences increment properly
 */

#include <gtest/gtest.h>
#include <tracer_backend/atf/atf_v2_types.h>
#include "thread_counters_private.h"

/* ===== Thread Counter Tests ===== */

// US-005: Verify counter initialization to zero
TEST(ThreadCounters, Init_Counters_Zero) {
    ThreadCounters tc = {0};

    EXPECT_EQ(tc.index_count, 0);
    EXPECT_EQ(tc.detail_count, 0);
}

// US-005: Verify reserve both sequences increments both counters
TEST(ThreadCounters, ReserveBoth_Links_Correctly) {
    ThreadCounters tc = {0};
    uint32_t idx_seq = 0;
    uint32_t det_seq = 0;

    atf_reserve_sequences(&tc, 1, &idx_seq, &det_seq);

    EXPECT_EQ(idx_seq, 0);
    EXPECT_EQ(det_seq, 0);
    EXPECT_EQ(tc.index_count, 1);
    EXPECT_EQ(tc.detail_count, 1);
}

// US-005: Verify no detail sets det_seq to MAX
TEST(ThreadCounters, NoDetail_Sets_Max) {
    ThreadCounters tc = {0};
    uint32_t idx_seq = 0;
    uint32_t det_seq = 0;

    atf_reserve_sequences(&tc, 0, &idx_seq, &det_seq);

    EXPECT_EQ(idx_seq, 0);
    EXPECT_EQ(det_seq, ATF_NO_DETAIL_SEQ);
    EXPECT_EQ(tc.index_count, 1);
    EXPECT_EQ(tc.detail_count, 0);
}

// US-005: Verify multiple reservations increment correctly
TEST(ThreadCounters, MultipleReservations_Increments) {
    ThreadCounters tc = {0};
    uint32_t idx_seq = 0;
    uint32_t det_seq = 0;

    // First reservation with detail
    atf_reserve_sequences(&tc, 1, &idx_seq, &det_seq);
    EXPECT_EQ(idx_seq, 0);
    EXPECT_EQ(det_seq, 0);

    // Second reservation without detail
    atf_reserve_sequences(&tc, 0, &idx_seq, &det_seq);
    EXPECT_EQ(idx_seq, 1);
    EXPECT_EQ(det_seq, ATF_NO_DETAIL_SEQ);

    // Third reservation with detail
    atf_reserve_sequences(&tc, 1, &idx_seq, &det_seq);
    EXPECT_EQ(idx_seq, 2);
    EXPECT_EQ(det_seq, 1);

    EXPECT_EQ(tc.index_count, 3);
    EXPECT_EQ(tc.detail_count, 2);
}

/* ===== Forward Linking Tests (Index -> Detail) ===== */

// US-005: Verify forward lookup returns detail_seq
TEST(IndexEventForward, ForwardLookup_Returns_DetailSeq) {
    IndexEvent event;
    memset(&event, 0, sizeof(event));
    event.timestamp_ns = 1000;
    event.function_id = 0x100000001;
    event.thread_id = 1;
    event.event_kind = ATF_EVENT_KIND_CALL;
    event.call_depth = 1;
    event.detail_seq = 42;

    EXPECT_EQ(index_event_get_detail_seq(&event), 42);
    EXPECT_TRUE(index_event_has_detail(&event));
}

// US-005: Verify forward lookup with no detail returns MAX
TEST(IndexEventForward, NoDetail_Returns_Max) {
    IndexEvent event;
    memset(&event, 0, sizeof(event));
    event.detail_seq = ATF_NO_DETAIL_SEQ;

    EXPECT_EQ(index_event_get_detail_seq(&event), ATF_NO_DETAIL_SEQ);
    EXPECT_FALSE(index_event_has_detail(&event));
}

/* ===== Backward Linking Tests (Detail -> Index) ===== */

// US-005: Verify backward lookup returns index_seq
TEST(DetailEventBackward, BackwardLookup_Returns_IndexSeq) {
    DetailEventHeader header;
    memset(&header, 0, sizeof(header));
    header.total_length = sizeof(DetailEventHeader);
    header.event_type = ATF_DETAIL_EVENT_FUNCTION_CALL;
    header.flags = 0;
    header.index_seq = 17;
    header.thread_id = 1;
    header.timestamp = 1000;

    EXPECT_EQ(detail_event_get_index_seq(&header), 17);
}

// US-005: Verify backward lookup preserves index_seq value
TEST(DetailEventBackward, IndexSeq_Preserves_Value) {
    DetailEventHeader header;
    memset(&header, 0, sizeof(header));
    header.index_seq = 999;

    EXPECT_EQ(detail_event_get_index_seq(&header), 999);
}

/* ===== Bidirectional Link Consistency Tests ===== */

// US-005: Verify paired events maintain consistency
TEST(BidirectionalLink, PairedEvents_Consistent) {
    ThreadCounters tc = {0};
    uint32_t idx_seq = 0;
    uint32_t det_seq = 0;

    // Reserve sequences
    atf_reserve_sequences(&tc, 1, &idx_seq, &det_seq);

    // Create index event with forward link
    IndexEvent idx_event;
    memset(&idx_event, 0, sizeof(idx_event));
    idx_event.detail_seq = det_seq;

    // Create detail event with backward link
    DetailEventHeader det_header;
    memset(&det_header, 0, sizeof(det_header));
    det_header.index_seq = idx_seq;

    // Verify bidirectional consistency
    EXPECT_EQ(index_event_get_detail_seq(&idx_event), det_seq);
    EXPECT_EQ(detail_event_get_index_seq(&det_header), idx_seq);
}

// US-005: Verify mixed reservations maintain correct links
TEST(BidirectionalLink, MixedReservations_LinksMatch) {
    ThreadCounters tc = {0};

    struct Event {
        uint32_t idx;
        uint32_t det;
    };

    Event events[5];

    // Reservation pattern: detail, no-detail, detail, no-detail, detail
    for (int i = 0; i < 5; i++) {
        int has_detail = (i % 2 == 0) ? 1 : 0;
        atf_reserve_sequences(&tc, has_detail, &events[i].idx, &events[i].det);
    }

    // Verify sequence numbers
    EXPECT_EQ(events[0].idx, 0); EXPECT_EQ(events[0].det, 0);
    EXPECT_EQ(events[1].idx, 1); EXPECT_EQ(events[1].det, ATF_NO_DETAIL_SEQ);
    EXPECT_EQ(events[2].idx, 2); EXPECT_EQ(events[2].det, 1);
    EXPECT_EQ(events[3].idx, 3); EXPECT_EQ(events[3].det, ATF_NO_DETAIL_SEQ);
    EXPECT_EQ(events[4].idx, 4); EXPECT_EQ(events[4].det, 2);

    // Verify final counts
    EXPECT_EQ(tc.index_count, 5);
    EXPECT_EQ(tc.detail_count, 3);
}
