/**
 * @file atf_index_writer.c
 * @brief Implementation of ATF v2 index file writer
 *
 * Writes fixed 32-byte index events to disk with buffered I/O for performance.
 */

#include "atf_index_writer_private.h"
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

AtfIndexWriter* atf_index_writer_create(const char* filepath,
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
    AtfIndexWriter* writer = (AtfIndexWriter*)calloc(1, sizeof(AtfIndexWriter));
    if (!writer) return NULL; // LCOV_EXCL_LINE

    /* Open file for writing */
    writer->file = fopen(filepath, "wb");
    if (!writer->file) {
        free(writer);
        return NULL;
    }

    /* Initialize state */
    writer->event_count = 0;
    writer->time_start_ns = 0;
    writer->time_end_ns = 0;
    writer->thread_id = thread_id;
    writer->clock_type = clock_type;

    /* Write placeholder header (will be updated at finalize) */
    memset(&writer->header, 0, sizeof(writer->header));
    memcpy(writer->header.magic, "ATI2", 4);
    writer->header.endian = 0x01;
    writer->header.version = 1;
    writer->header.arch = CURRENT_ARCH;
    writer->header.os = CURRENT_OS;
    writer->header.flags = 0;
    writer->header.thread_id = thread_id;
    writer->header.clock_type = clock_type;
    writer->header.event_size = 32;
    writer->header.event_count = 0;
    writer->header.events_offset = 64;
    writer->header.footer_offset = 64;  /* Will be updated */
    writer->header.time_start_ns = 0;
    writer->header.time_end_ns = 0;

    if (fwrite(&writer->header, sizeof(writer->header), 1, writer->file) != 1) { // LCOV_EXCL_START
        fclose(writer->file);
        free(writer);
        return NULL;
    } // LCOV_EXCL_STOP

    return writer;
}

int atf_index_writer_write_event(AtfIndexWriter* writer, const IndexEvent* event) {
    if (!writer || !writer->file || !event) return -EINVAL;

    /* Update time range */
    if (writer->event_count == 0) {
        writer->time_start_ns = event->timestamp_ns;
    }
    writer->time_end_ns = event->timestamp_ns;

    /* Write event */
    if (fwrite(event, sizeof(IndexEvent), 1, writer->file) != 1) { // LCOV_EXCL_LINE
        return -EIO; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    writer->event_count++;
    return 0;
}

int atf_index_writer_finalize(AtfIndexWriter* writer) {
    if (!writer || !writer->file) return -EINVAL;

    /* Flush events */
    if (fflush(writer->file) != 0) { // LCOV_EXCL_LINE
        return -EIO; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    /* Get footer offset */
    long footer_offset = ftell(writer->file);
    if (footer_offset < 0) { // LCOV_EXCL_LINE
        return -EIO; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    /* Write footer */
    AtfIndexFooter footer;
    memset(&footer, 0, sizeof(footer));
    memcpy(footer.magic, "2ITA", 4);
    footer.checksum = 0;  /* TODO: Implement CRC32 */
    footer.event_count = writer->event_count;
    footer.time_start_ns = writer->time_start_ns;
    footer.time_end_ns = writer->time_end_ns;
    footer.bytes_written = writer->event_count * sizeof(IndexEvent);

    if (fwrite(&footer, sizeof(footer), 1, writer->file) != 1) { // LCOV_EXCL_LINE
        return -EIO; // LCOV_EXCL_LINE
    } // LCOV_EXCL_LINE

    /* Update header */
    writer->header.event_count = writer->event_count;
    writer->header.footer_offset = footer_offset;
    writer->header.time_start_ns = writer->time_start_ns;
    writer->header.time_end_ns = writer->time_end_ns;

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

void atf_index_writer_close(AtfIndexWriter* writer) {
    if (!writer) return;

    if (writer->file) {
        fclose(writer->file);
    }

    free(writer);
}
