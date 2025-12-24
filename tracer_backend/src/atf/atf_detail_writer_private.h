/**
 * @file atf_detail_writer_private.h
 * @brief Private header for ATF v2 detail file writer
 *
 * The detail writer handles writing variable-length detail events to disk.
 * Detail events are length-prefixed and include rich context like registers and stack.
 */

#ifndef TRACER_BACKEND_ATF_DETAIL_WRITER_PRIVATE_H
#define TRACER_BACKEND_ATF_DETAIL_WRITER_PRIVATE_H

#include <tracer_backend/atf/atf_v2_types.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Detail file writer state
 */
typedef struct {
    FILE* file;                  /* File handle */
    AtfDetailHeader header;      /* Header (updated at finalize) */
    uint32_t event_count;        /* Number of events written */
    uint64_t bytes_written;      /* Total bytes written (events only) */
    uint64_t time_start_ns;      /* First event timestamp */
    uint64_t time_end_ns;        /* Last event timestamp */
    uint32_t index_seq_start;    /* First index sequence covered */
    uint32_t index_seq_end;      /* Last index sequence covered */
    uint32_t thread_id;          /* Thread ID */
    uint8_t clock_type;          /* Clock type */
} AtfDetailWriter;

/**
 * Create a detail writer
 *
 * @param filepath Path to detail file (e.g., "thread_0/detail.atf")
 * @param thread_id Thread ID
 * @param clock_type Clock type
 * @return Pointer to writer, or NULL on error
 */
AtfDetailWriter* atf_detail_writer_create(const char* filepath,
                                          uint32_t thread_id,
                                          uint8_t clock_type);

/**
 * Write a detail event
 *
 * @param writer Pointer to writer
 * @param index_seq Index sequence number (backward link)
 * @param timestamp Timestamp (same as index event)
 * @param event_type Event type (FUNCTION_CALL, FUNCTION_RETURN, etc.)
 * @param payload Pointer to payload data
 * @param payload_size Size of payload in bytes
 * @return 0 on success, negative errno on error
 */
int atf_detail_writer_write_event(AtfDetailWriter* writer,
                                  uint32_t index_seq,
                                  uint64_t timestamp,
                                  uint16_t event_type,
                                  const void* payload,
                                  size_t payload_size);

/**
 * Finalize the detail file
 *
 * Updates header with final counts and timestamps, writes footer.
 *
 * @param writer Pointer to writer
 * @return 0 on success, negative errno on error
 */
int atf_detail_writer_finalize(AtfDetailWriter* writer);

/**
 * Close and free the detail writer
 *
 * @param writer Pointer to writer
 */
void atf_detail_writer_close(AtfDetailWriter* writer);

#ifdef __cplusplus
}
#endif

#endif /* TRACER_BACKEND_ATF_DETAIL_WRITER_PRIVATE_H */
