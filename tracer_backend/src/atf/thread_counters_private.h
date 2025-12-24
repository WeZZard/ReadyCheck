/**
 * @file thread_counters_private.h
 * @brief Private header for thread-local sequence counters
 *
 * ThreadCounters maintains per-thread sequence numbers for index and detail events.
 * These counters are used to reserve sequence numbers at hook time for bidirectional linking.
 */

#ifndef TRACER_BACKEND_ATF_THREAD_COUNTERS_PRIVATE_H
#define TRACER_BACKEND_ATF_THREAD_COUNTERS_PRIVATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Per-thread sequence counters
 * Single producer (the thread itself), no atomics needed
 */
typedef struct {
    uint32_t index_count;   /* Next index sequence number */
    uint32_t detail_count;  /* Next detail sequence number */
} ThreadCounters;

/**
 * Initialize thread counters to zero
 * @param tc Pointer to thread counters
 */
void atf_thread_counters_init(ThreadCounters* tc);

/**
 * Reserve sequence numbers for an event
 *
 * Atomically reserves both index and detail sequence numbers.
 * If detail is disabled, det_seq is set to UINT32_MAX.
 *
 * @param tc Pointer to thread counters
 * @param detail_enabled Whether detail recording is active
 * @param idx_seq Output: reserved index sequence number
 * @param det_seq Output: reserved detail sequence number (or UINT32_MAX)
 */
void atf_reserve_sequences(ThreadCounters* tc, int detail_enabled,
                           uint32_t* idx_seq, uint32_t* det_seq);

/**
 * Reset thread counters to zero
 * @param tc Pointer to thread counters
 */
void atf_thread_counters_reset(ThreadCounters* tc);

#ifdef __cplusplus
}
#endif

#endif /* TRACER_BACKEND_ATF_THREAD_COUNTERS_PRIVATE_H */
