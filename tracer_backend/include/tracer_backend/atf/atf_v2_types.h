/**
 * @file atf_v2_types.h
 * @brief ADA Trace Format V2 - Raw binary type definitions
 *
 * Type definitions for ATF v2 format as specified in docs/specs/TRACE_SCHEMA.md
 * This is a zero-overhead binary format optimized for 10M+ events/sec throughput.
 */

#ifndef TRACER_BACKEND_ATF_V2_TYPES_H
#define TRACER_BACKEND_ATF_V2_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Platform and Architecture Constants ===== */

/* Architecture values */
#define ATF_ARCH_X86_64  1
#define ATF_ARCH_ARM64   2

/* Operating system values */
#define ATF_OS_IOS       1
#define ATF_OS_ANDROID   2
#define ATF_OS_MACOS     3
#define ATF_OS_LINUX     4
#define ATF_OS_WINDOWS   5

/* Clock type values */
#define ATF_CLOCK_MACH_CONTINUOUS  1
#define ATF_CLOCK_QPC              2
#define ATF_CLOCK_BOOTTIME         3

/* Event kind values */
#define ATF_EVENT_KIND_CALL      1
#define ATF_EVENT_KIND_RETURN    2
#define ATF_EVENT_KIND_EXCEPTION 3

/* Detail event types */
#define ATF_DETAIL_EVENT_FUNCTION_CALL   3
#define ATF_DETAIL_EVENT_FUNCTION_RETURN 4

/* Special values */
#define ATF_NO_DETAIL_SEQ UINT32_MAX

/* Index header flags */
#define ATF_INDEX_FLAG_HAS_DETAIL_FILE (1U << 0)

/* ===== Index File Structures ===== */

/**
 * Index File Header - 64 bytes
 * Written as placeholder first, updated at finalization
 */
typedef struct __attribute__((packed)) {
    /* Identity (16 bytes) */
    uint8_t  magic[4];           /* "ATI2" (ATF Index v2) */
    uint8_t  endian;             /* 0x01 = little-endian */
    uint8_t  version;            /* 1 */
    uint8_t  arch;               /* 1=x86_64, 2=arm64 */
    uint8_t  os;                 /* 1=iOS, 2=Android, 3=macOS, 4=Linux, 5=Windows */
    uint32_t flags;              /* Bit 0: has_detail_file */
    uint32_t thread_id;          /* Thread ID for this file */

    /* Timing metadata (8 bytes) */
    uint8_t  clock_type;         /* 1=mach_continuous, 2=qpc, 3=boottime */
    uint8_t  _reserved1[3];
    uint32_t _reserved2;

    /* Event layout (8 bytes) */
    uint32_t event_size;         /* 32 bytes per event */
    uint32_t event_count;        /* Total number of events */

    /* Offsets (16 bytes) */
    uint64_t events_offset;      /* Offset to first event */
    uint64_t footer_offset;      /* Offset to footer (for recovery) */

    /* Time range (16 bytes) */
    uint64_t time_start_ns;      /* First event timestamp */
    uint64_t time_end_ns;        /* Last event timestamp */
} AtfIndexHeader;

/* Compile-time assertion for header size */
_Static_assert(sizeof(AtfIndexHeader) == 64, "AtfIndexHeader must be 64 bytes");

/**
 * Index Event - 32 bytes (fixed size)
 * Captures ALL function call/return events
 */
typedef struct __attribute__((packed)) {
    uint64_t timestamp_ns;       /* Platform continuous clock (genlock) */
    uint64_t function_id;        /* (moduleId << 32) | symbolIndex */
    uint32_t thread_id;          /* OS thread identifier */
    uint32_t event_kind;         /* CALL=1, RETURN=2, EXCEPTION=3 */
    uint32_t call_depth;         /* Call stack depth */
    uint32_t detail_seq;         /* Forward link to detail event (UINT32_MAX = none) */
} IndexEvent;

/* Compile-time assertion for event size */
_Static_assert(sizeof(IndexEvent) == 32, "IndexEvent must be 32 bytes");

/**
 * Index File Footer - 64 bytes
 * Authoritative source for crash recovery
 */
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];           /* "2ITA" (reversed) */
    uint32_t checksum;           /* CRC32 of events section */
    uint64_t event_count;        /* Actual event count (authoritative) */
    uint64_t time_start_ns;      /* First event timestamp */
    uint64_t time_end_ns;        /* Last event timestamp */
    uint64_t bytes_written;      /* Total bytes in events section */
    uint8_t  reserved[24];
} AtfIndexFooter;

/* Compile-time assertion for footer size */
_Static_assert(sizeof(AtfIndexFooter) == 64, "AtfIndexFooter must be 64 bytes");

/* ===== Detail File Structures ===== */

/**
 * Detail File Header - 64 bytes
 * References the index file, created only when detail recording is active
 */
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];           /* "ATD2" (ATF Detail v2) */
    uint8_t  endian;             /* 0x01 = little-endian */
    uint8_t  version;            /* 1 */
    uint8_t  arch;               /* 1=x86_64, 2=arm64 */
    uint8_t  os;                 /* 1=iOS, 2=Android, 3=macOS, 4=Linux */
    uint32_t flags;              /* Reserved */
    uint32_t thread_id;          /* Thread ID for this file */
    uint32_t _reserved1;         /* Padding */
    uint64_t events_offset;      /* Offset to first event (typically 64) */
    uint64_t event_count;        /* Number of detail events */
    uint64_t bytes_length;       /* Total bytes in events section */
    uint64_t index_seq_start;    /* First index sequence number covered */
    uint64_t index_seq_end;      /* Last index sequence number covered */
    uint8_t  _reserved2[4];      /* Padding to 64 bytes */
} AtfDetailHeader;

/* Compile-time assertion for header size */
_Static_assert(sizeof(AtfDetailHeader) == 64, "AtfDetailHeader must be 64 bytes");

/**
 * Detail Event Header - 24 bytes
 * Followed by variable-length payload
 */
typedef struct __attribute__((packed)) {
    uint32_t total_length;       /* Including this header and payload */
    uint16_t event_type;         /* FUNCTION_CALL=3, FUNCTION_RETURN=4, etc. */
    uint16_t flags;              /* Event-specific flags */
    uint32_t index_seq;          /* Backward link to index event position */
    uint32_t thread_id;          /* Thread that generated event */
    uint64_t timestamp;          /* Monotonic nanoseconds (same as index) */
    /* Payload follows (registers, stack, etc.) */
} DetailEventHeader;

/* Compile-time assertion for header size */
_Static_assert(sizeof(DetailEventHeader) == 24, "DetailEventHeader must be 24 bytes");

/**
 * Detail Function Payload (ARM64)
 * Variable size due to stack snapshot
 */
typedef struct __attribute__((packed)) {
    uint64_t function_id;        /* Same as index event */
    uint64_t x_regs[8];          /* x0-x7 (arguments or return value) */
    uint64_t lr;                 /* Link register */
    uint64_t fp;                 /* Frame pointer */
    uint64_t sp;                 /* Stack pointer */
    uint16_t stack_size;         /* Bytes of stack captured (0-256) */
    uint16_t _reserved;
    /* uint8_t stack_snapshot[stack_size] follows */
} DetailFunctionPayload;

/**
 * Detail File Footer - 64 bytes
 */
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];           /* "2DTA" (reversed) */
    uint32_t checksum;           /* CRC32 of events section */
    uint64_t event_count;        /* Actual event count */
    uint64_t bytes_length;       /* Actual bytes in events section */
    uint64_t time_start_ns;      /* First event timestamp */
    uint64_t time_end_ns;        /* Last event timestamp */
    uint8_t  reserved[24];
} AtfDetailFooter;

/* Compile-time assertion for footer size */
_Static_assert(sizeof(AtfDetailFooter) == 64, "AtfDetailFooter must be 64 bytes");

/* ===== Helper Functions ===== */

/**
 * Check if an index event has an associated detail event
 * @param event Pointer to index event
 * @return true if event has detail, false otherwise
 */
static inline int index_event_has_detail(const IndexEvent* event) {
    return event->detail_seq != ATF_NO_DETAIL_SEQ;
}

/**
 * Get the detail sequence number from an index event
 * @param event Pointer to index event
 * @return Detail sequence number, or ATF_NO_DETAIL_SEQ if none
 */
static inline uint32_t index_event_get_detail_seq(const IndexEvent* event) {
    return event->detail_seq;
}

/**
 * Get the index sequence number from a detail event header
 * @param header Pointer to detail event header
 * @return Index sequence number
 */
static inline uint32_t detail_event_get_index_seq(const DetailEventHeader* header) {
    return header->index_seq;
}

#ifdef __cplusplus
}
#endif

#endif /* TRACER_BACKEND_ATF_V2_TYPES_H */
