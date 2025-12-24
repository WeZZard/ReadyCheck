/**
 * @file thread_counters.c
 * @brief Implementation of thread-local sequence counters
 *
 * Provides atomic reservation of sequence numbers for bidirectional linking
 * between index and detail events.
 */

#include "thread_counters_private.h"
#include <tracer_backend/atf/atf_v2_types.h>
#include <string.h>

void atf_thread_counters_init(ThreadCounters* tc) {
    if (!tc) return;
    tc->index_count = 0;
    tc->detail_count = 0;
}

void atf_reserve_sequences(ThreadCounters* tc, int detail_enabled,
                           uint32_t* idx_seq, uint32_t* det_seq) {
    if (!tc || !idx_seq || !det_seq) return;

    /* Reserve index sequence (always increments) */
    *idx_seq = tc->index_count++;

    /* Reserve detail sequence (only if enabled) */
    if (detail_enabled) {
        *det_seq = tc->detail_count++;
    } else {
        *det_seq = ATF_NO_DETAIL_SEQ;
    }
}

void atf_thread_counters_reset(ThreadCounters* tc) {
    if (!tc) return;
    tc->index_count = 0;
    tc->detail_count = 0;
}
