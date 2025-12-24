/**
 * @file atf_index_writer_private.h
 * @brief Private header for ATF v2 index file writer
 *
 * The index writer handles writing fixed 32-byte index events to disk.
 * It maintains buffered I/O for performance and tracks file metadata.
 */

#ifndef TRACER_BACKEND_ATF_INDEX_WRITER_PRIVATE_H
#define TRACER_BACKEND_ATF_INDEX_WRITER_PRIVATE_H

#include <tracer_backend/atf/atf_v2_types.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Index file writer state
 */
typedef struct {
    FILE* file;                  /* File handle */
    AtfIndexHeader header;       /* Header (updated at finalize) */
    uint32_t event_count;        /* Number of events written */
    uint64_t time_start_ns;      /* First event timestamp */
    uint64_t time_end_ns;        /* Last event timestamp */
    uint32_t thread_id;          /* Thread ID */
    uint8_t clock_type;          /* Clock type */
} AtfIndexWriter;

/**
 * Create an index writer
 *
 * @param filepath Path to index file (e.g., "thread_0/index.atf")
 * @param thread_id Thread ID
 * @param clock_type Clock type (1=mach_continuous, 2=qpc, 3=boottime)
 * @return Pointer to writer, or NULL on error
 */
AtfIndexWriter* atf_index_writer_create(const char* filepath,
                                        uint32_t thread_id,
                                        uint8_t clock_type);

/**
 * Write an index event
 *
 * @param writer Pointer to writer
 * @param event Pointer to event to write
 * @return 0 on success, negative errno on error
 */
int atf_index_writer_write_event(AtfIndexWriter* writer, const IndexEvent* event);

/**
 * Finalize the index file
 *
 * Updates header with final counts and timestamps, writes footer.
 *
 * @param writer Pointer to writer
 * @return 0 on success, negative errno on error
 */
int atf_index_writer_finalize(AtfIndexWriter* writer);

/**
 * Close and free the index writer
 *
 * @param writer Pointer to writer
 */
void atf_index_writer_close(AtfIndexWriter* writer);

#ifdef __cplusplus
}
#endif

#endif /* TRACER_BACKEND_ATF_INDEX_WRITER_PRIVATE_H */
