/**
 * @file atf_thread_writer.c
 * @brief Implementation of unified ATF v2 thread writer
 *
 * Coordinates index and detail writers, handles bidirectional linking.
 */

#include <tracer_backend/atf/atf_thread_writer.h>
#include "atf_index_writer_private.h"
#include "atf_detail_writer_private.h"
#include "thread_counters_private.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/**
 * Thread writer implementation
 */
struct AtfThreadWriter {
    AtfIndexWriter* index_writer;
    AtfDetailWriter* detail_writer;  /* NULL if no detail recorded */
    ThreadCounters counters;
    char* session_dir;               /* Stored for detail writer creation */
    uint32_t thread_id;
    uint8_t clock_type;
    int detail_file_created;
};

AtfThreadWriter* atf_thread_writer_create(const char* session_dir,
                                          uint32_t thread_id,
                                          uint8_t clock_type) {
    if (!session_dir) return NULL;

    /* Allocate writer */
    AtfThreadWriter* writer = (AtfThreadWriter*)calloc(1, sizeof(AtfThreadWriter));
    if (!writer) return NULL; // LCOV_EXCL_LINE

    /* Store session_dir for later use */
    writer->session_dir = strdup(session_dir);
    if (!writer->session_dir) { // LCOV_EXCL_START
        free(writer);
        return NULL;
    } // LCOV_EXCL_STOP

    /* Initialize state */
    writer->thread_id = thread_id;
    writer->clock_type = clock_type;
    writer->detail_file_created = 0;
    atf_thread_counters_init(&writer->counters);

    /* Build paths */
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/thread_%u/index.atf",
             session_dir, thread_id);

    /* Create index writer */
    writer->index_writer = atf_index_writer_create(index_path, thread_id, clock_type);
    if (!writer->index_writer) { // LCOV_EXCL_START
        free(writer->session_dir);
        free(writer);
        return NULL;
    } // LCOV_EXCL_STOP

    /* Detail writer created lazily on first detail event */
    writer->detail_writer = NULL;

    return writer;
}

uint32_t atf_thread_writer_write_event(AtfThreadWriter* writer,
                                       uint64_t timestamp_ns,
                                       uint64_t function_id,
                                       uint32_t event_kind,
                                       uint32_t call_depth,
                                       const void* detail_payload,
                                       size_t detail_payload_size) {
    if (!writer || !writer->index_writer) return UINT32_MAX;

    /* Reserve sequence numbers */
    uint32_t idx_seq, det_seq;
    int has_detail = (detail_payload != NULL && detail_payload_size > 0) ? 1 : 0;
    atf_reserve_sequences(&writer->counters, has_detail, &idx_seq, &det_seq);

    /* Build index event */
    IndexEvent idx_event;
    idx_event.timestamp_ns = timestamp_ns;
    idx_event.function_id = function_id;
    idx_event.thread_id = writer->thread_id;
    idx_event.event_kind = event_kind;
    idx_event.call_depth = call_depth;
    idx_event.detail_seq = det_seq;

    /* Write index event */
    if (atf_index_writer_write_event(writer->index_writer, &idx_event) != 0) { // LCOV_EXCL_LINE
        return UINT32_MAX; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    /* Write detail event if present */
    if (has_detail) {
        /* Create detail writer if not exists */
        if (!writer->detail_writer) {
            char detail_path[1024];

            /* Use stored session_dir to construct detail path */
            snprintf(detail_path, sizeof(detail_path),
                     "%s/thread_%u/detail.atf", writer->session_dir, writer->thread_id);

            writer->detail_writer = atf_detail_writer_create(detail_path,
                                                             writer->thread_id,
                                                             writer->clock_type);
            if (!writer->detail_writer) { // LCOV_EXCL_LINE
                return UINT32_MAX; // LCOV_EXCL_LINE
            } // LCOV_EXCL_LINE
            writer->detail_file_created = 1;

            /* Update index header to indicate detail file exists */
            writer->index_writer->header.flags |= ATF_INDEX_FLAG_HAS_DETAIL_FILE;
        }

        /* Determine event type from event_kind */
        uint16_t detail_event_type;
        if (event_kind == ATF_EVENT_KIND_CALL) {
            detail_event_type = ATF_DETAIL_EVENT_FUNCTION_CALL;
        } else if (event_kind == ATF_EVENT_KIND_RETURN) {
            detail_event_type = ATF_DETAIL_EVENT_FUNCTION_RETURN;
        } else {
            detail_event_type = ATF_DETAIL_EVENT_FUNCTION_CALL;  /* Default */
        }

        /* Write detail event with backward link */
        if (atf_detail_writer_write_event(writer->detail_writer,
                                          idx_seq,  /* Backward link */
                                          timestamp_ns,
                                          detail_event_type,
                                          detail_payload,
                                          detail_payload_size) != 0) { // LCOV_EXCL_LINE
            return UINT32_MAX; // LCOV_EXCL_LINE
        } // LCOV_EXCL_LINE
    }

    return idx_seq;
}

int atf_thread_writer_finalize(AtfThreadWriter* writer) {
    if (!writer) return -EINVAL;

    int ret = 0;

    /* Finalize index writer */
    if (writer->index_writer) {
        if (atf_index_writer_finalize(writer->index_writer) != 0) { // LCOV_EXCL_LINE
            ret = -EIO; // LCOV_EXCL_LINE
        } // LCOV_EXCL_LINE
    }

    /* Finalize detail writer if it exists */
    if (writer->detail_writer) {
        if (atf_detail_writer_finalize(writer->detail_writer) != 0) { // LCOV_EXCL_LINE
            ret = -EIO; // LCOV_EXCL_LINE
        } // LCOV_EXCL_LINE
    }

    return ret;
}

void atf_thread_writer_close(AtfThreadWriter* writer) {
    if (!writer) return;

    if (writer->index_writer) {
        atf_index_writer_close(writer->index_writer);
    }

    if (writer->detail_writer) {
        atf_detail_writer_close(writer->detail_writer);
    }

    free(writer->session_dir);
    free(writer);
}
