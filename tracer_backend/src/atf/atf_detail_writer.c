/**
 * @file atf_detail_writer.c
 * @brief Implementation of ATF v2 detail file writer
 *
 * Writes variable-length detail events to disk with length prefixes.
 */

#include "atf_detail_writer_private.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

/* Platform detection */
#if defined(__APPLE__)
    #define CURRENT_OS ATF_OS_MACOS
#elif defined(__linux__)
    #define CURRENT_OS ATF_OS_LINUX
#elif defined(_WIN32)
    #define CURRENT_OS ATF_OS_WINDOWS
#else
    #define CURRENT_OS ATF_OS_LINUX
#endif

/* Architecture detection */
#if defined(__x86_64__) || defined(_M_X64)
    #define CURRENT_ARCH ATF_ARCH_X86_64
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define CURRENT_ARCH ATF_ARCH_ARM64
#else
    #define CURRENT_ARCH ATF_ARCH_X86_64
#endif

/* Helper to create directory recursively */
static int mkdir_recursive(const char* path) {
    char tmp[1024];
    char* p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') { // LCOV_EXCL_LINE
        tmp[len - 1] = 0; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    return 0;
}

/* Helper to get directory from filepath */
static void get_directory(const char* filepath, char* dir, size_t dir_size) {
    const char* last_slash = strrchr(filepath, '/');
    if (last_slash) {
        size_t len = last_slash - filepath;
        if (len >= dir_size) len = dir_size - 1;
        memcpy(dir, filepath, len);
        dir[len] = '\0';
    } else {
        dir[0] = '\0';
    }
}

AtfDetailWriter* atf_detail_writer_create(const char* filepath,
                                          uint32_t thread_id,
                                          uint8_t clock_type) {
    if (!filepath) return NULL;

    /* Create directory if needed */
    char dir[1024];
    get_directory(filepath, dir, sizeof(dir));
    if (dir[0] != '\0') {
        mkdir_recursive(dir);
    }

    /* Allocate writer */
    AtfDetailWriter* writer = (AtfDetailWriter*)calloc(1, sizeof(AtfDetailWriter));
    if (!writer) return NULL; // LCOV_EXCL_LINE

    /* Open file for writing */
    writer->file = fopen(filepath, "wb");
    if (!writer->file) {
        free(writer);
        return NULL;
    }

    /* Initialize state */
    writer->event_count = 0;
    writer->bytes_written = 0;
    writer->time_start_ns = 0;
    writer->time_end_ns = 0;
    writer->index_seq_start = UINT32_MAX;
    writer->index_seq_end = 0;
    writer->thread_id = thread_id;
    writer->clock_type = clock_type;

    /* Write placeholder header */
    memset(&writer->header, 0, sizeof(writer->header));
    memcpy(writer->header.magic, "ATD2", 4);
    writer->header.endian = 0x01;
    writer->header.version = 1;
    writer->header.arch = CURRENT_ARCH;
    writer->header.os = CURRENT_OS;
    writer->header.flags = 0;
    writer->header.thread_id = thread_id;
    writer->header.events_offset = 64;
    writer->header.event_count = 0;
    writer->header.bytes_length = 0;
    writer->header.index_seq_start = 0;
    writer->header.index_seq_end = 0;

    if (fwrite(&writer->header, sizeof(writer->header), 1, writer->file) != 1) { // LCOV_EXCL_START
        fclose(writer->file);
        free(writer);
        return NULL;
    } // LCOV_EXCL_STOP

    return writer;
}

int atf_detail_writer_write_event(AtfDetailWriter* writer,
                                  uint32_t index_seq,
                                  uint64_t timestamp,
                                  uint16_t event_type,
                                  const void* payload,
                                  size_t payload_size) {
    if (!writer || !writer->file) return -EINVAL;
    if (!payload && payload_size > 0) return -EINVAL;

    /* Build event header */
    DetailEventHeader header;
    header.total_length = sizeof(DetailEventHeader) + payload_size;
    header.event_type = event_type;
    header.flags = 0;
    header.index_seq = index_seq;
    header.thread_id = writer->thread_id;
    header.timestamp = timestamp;

    /* Update time range */
    if (writer->event_count == 0) {
        writer->time_start_ns = timestamp;
    }
    writer->time_end_ns = timestamp;

    /* Update index sequence range */
    if (index_seq < writer->index_seq_start) {
        writer->index_seq_start = index_seq;
    }
    if (index_seq > writer->index_seq_end) {
        writer->index_seq_end = index_seq;
    }

    /* Write header */
    if (fwrite(&header, sizeof(header), 1, writer->file) != 1) { // LCOV_EXCL_LINE
        return -EIO; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    /* Write payload */
    if (payload_size > 0) {
        if (fwrite(payload, payload_size, 1, writer->file) != 1) { // LCOV_EXCL_LINE
            return -EIO; // LCOV_EXCL_LINE
        } // LCOV_EXCL_LINE
    }

    writer->event_count++;
    writer->bytes_written += header.total_length;

    return 0;
}

int atf_detail_writer_finalize(AtfDetailWriter* writer) {
    if (!writer || !writer->file) return -EINVAL;

    /* Flush events */
    if (fflush(writer->file) != 0) { // LCOV_EXCL_LINE
        return -EIO; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    /* Write footer */
    AtfDetailFooter footer;
    memset(&footer, 0, sizeof(footer));
    memcpy(footer.magic, "2DTA", 4);
    footer.checksum = 0;  /* TODO: Implement CRC32 */
    footer.event_count = writer->event_count;
    footer.bytes_length = writer->bytes_written;
    footer.time_start_ns = writer->time_start_ns;
    footer.time_end_ns = writer->time_end_ns;

    if (fwrite(&footer, sizeof(footer), 1, writer->file) != 1) { // LCOV_EXCL_LINE
        return -EIO; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    /* Update header */
    writer->header.event_count = writer->event_count;
    writer->header.bytes_length = writer->bytes_written;
    writer->header.index_seq_start = writer->index_seq_start;
    writer->header.index_seq_end = writer->index_seq_end;

    /* Seek to beginning and rewrite header */
    if (fseek(writer->file, 0, SEEK_SET) != 0) { // LCOV_EXCL_LINE
        return -EIO; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    if (fwrite(&writer->header, sizeof(writer->header), 1, writer->file) != 1) { // LCOV_EXCL_LINE
        return -EIO; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    /* Flush header */
    if (fflush(writer->file) != 0) { // LCOV_EXCL_LINE
        return -EIO; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    return 0;
}

void atf_detail_writer_close(AtfDetailWriter* writer) {
    if (!writer) return;

    if (writer->file) {
        fclose(writer->file);
    }

    free(writer);
}
