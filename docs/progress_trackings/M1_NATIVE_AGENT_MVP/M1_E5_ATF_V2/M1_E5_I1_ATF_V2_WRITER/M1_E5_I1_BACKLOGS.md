---
status: completed
date: 2025-12-24
supersedes: M1_E2_I3_ATF_V4_WRITER
---

# M1_E5_I1 Backlogs: ATF V2 Writer

## Sprint Overview

**Duration**: 3 days (24 hours)
**Priority**: P0 (Critical - closes M1 MVP loop)
**Dependencies**: Ring buffer infrastructure (M1_E2_I1, M1_E2_I2)

## Day 1: Type Definitions & Core Infrastructure (8 hours)

### ATF2-W-001: Define ATF v2 Types Header (P0, 2h)

**File**: `tracer_backend/include/tracer_backend/atf/atf_v2_types.h`

- [x] Define AtfIndexHeader (64 bytes) with magic "ATI2"
- [x] Define IndexEvent (32 bytes) with detail_seq field
- [x] Define AtfIndexFooter (64 bytes) with reversed magic
- [x] Define AtfDetailHeader (64 bytes) with magic "ATD2"
- [x] Define DetailEventHeader (24 bytes) with index_seq field
- [x] Define AtfDetailFooter (64 bytes) with reversed magic
- [x] Add static_assert for all struct sizes
- [x] Add platform detection macros (arch, os, clock_type)

### ATF2-W-002: Implement ThreadCounters (P0, 1.5h)

**File**: `tracer_backend/src/atf/thread_counters.c`

- [x] Define ThreadCounters struct with index_count, detail_count
- [x] Implement atf_reserve_sequences() for atomic dual reservation
- [x] Handle detail_enabled=false case (returns UINT32_MAX)
- [x] Add unit tests for sequence generation
- [x] Verify lock-free behavior (single producer per thread)

### ATF2-W-003: Index Writer Initialization (P0, 2h)

**File**: `tracer_backend/src/atf/atf_index_writer.c`

- [x] Implement atf_index_writer_create()
- [x] Create thread directory (thread_N/)
- [x] Write placeholder header (64 bytes)
- [x] Initialize FILE* with 64KB buffering
- [x] Track event_count, time_start_ns, time_end_ns

### ATF2-W-004: Index Event Writing (P0, 2.5h)

**File**: `tracer_backend/src/atf/atf_index_writer.c`

- [x] Implement atf_write_index_event()
- [x] Direct memcpy from ring buffer (zero encoding)
- [x] Update event_count and time tracking
- [x] Handle UINT32_MAX detail_seq (no detail)
- [x] Verify 32-byte alignment

## Day 2: Detail Writer & Finalization (8 hours)

### ATF2-W-005: Detail Writer Initialization (P0, 2h)

**File**: `tracer_backend/src/atf/atf_detail_writer.c`

- [x] Implement atf_detail_writer_create()
- [x] Write placeholder header (64 bytes)
- [x] Initialize FILE* with 64KB buffering
- [x] Track event_count, bytes_written

### ATF2-W-006: Detail Event Writing (P0, 2h)

**File**: `tracer_backend/src/atf/atf_detail_writer.c`

- [x] Implement atf_write_detail_event()
- [x] Write length-prefixed DetailEventHeader
- [x] Write DetailFunctionPayload (registers, stack)
- [x] Update index_seq backward link
- [x] Update byte count and time tracking

### ATF2-W-007: Writer Finalization (P0, 2h)

**Files**: `atf_index_writer.c`, `atf_detail_writer.c`

- [x] Implement atf_writer_finalize()
- [x] Seek back to header offset
- [x] Update header with final counts and timestamps
- [x] Write footer with CRC32 checksum
- [x] Flush and close file handles

### ATF2-W-008: Unified Writer API (P0, 2h)

**File**: `tracer_backend/src/atf/atf_thread_writer.c`

- [x] Implement AtfThreadWriter combining index + detail writers
- [x] Coordinate ThreadCounters between both writers
- [x] Implement atf_writer_create() unified API
- [x] Implement atf_writer_close() with finalization
- [x] Set has_detail_file flag in index header

## Day 3: Integration & Testing (8 hours)

### ATF2-W-009: Drain Thread Integration (P0, 3h)

**File**: `tracer_backend/src/drain/drain_thread.c`

- [ ] Create AtfThreadWriter per registered thread (DEFERRED: Drain thread currently uses ATF V4, migration planned for separate iteration)
- [ ] Drain index events from ring buffer to writer
- [ ] Drain detail events when detail recording active
- [ ] Handle session finalization on stop
- [ ] Coordinate with existing drain infrastructure

### ATF2-W-010: Unit Tests - Byte Layout (P0, 1.5h)

**File**: `tracer_backend/tests/unit/atf/test_atf_v2_layout.cpp`

- [x] Test AtfIndexHeader is 64 bytes with correct fields
- [x] Test IndexEvent is 32 bytes with detail_seq
- [x] Test AtfIndexFooter is 64 bytes with reversed magic
- [x] Test AtfDetailHeader is 64 bytes
- [x] Test DetailEventHeader has index_seq field

### ATF2-W-011: Unit Tests - Bidirectional Linking (P0, 1.5h)

**File**: `tracer_backend/tests/unit/atf/test_atf_v2_linking.cpp`

- [x] Test atf_reserve_sequences() returns paired sequences
- [x] Test detail_enabled=false returns UINT32_MAX
- [x] Test forward lookup (index → detail via detail_seq)
- [x] Test backward lookup (detail → index via index_seq)
- [x] Test sequence numbers are monotonic

### ATF2-W-012: Integration Tests (P0, 2h)

**File**: `tracer_backend/tests/integration/atf/test_atf_v2_writer.cpp`

- [x] Test index-only mode creates no detail file
- [x] Test index+detail mode creates both files
- [x] Test bidirectional links are valid
- [ ] Test crash recovery reads from footer (DEFERRED: requires ATF V2 Reader from M1_E5_I2)
- [ ] Test write throughput exceeds 10M events/sec (DEFERRED: benchmark test not blocking)

## Cleanup Tasks

### ATF2-W-013: Remove Protobuf Dependency (P1, 1h)

- [ ] Remove protobuf-c from CMakeLists.txt (DEFERRED: ATF V4 writer still in use by drain thread)
- [ ] Delete old ATF V4 writer code
- [x] Update build.rs for new source files
- [ ] Verify clean build without protobuf

## Definition of Done

- [x] All ATF v2 types defined with correct byte sizes
- [x] ThreadCounters reserves sequences atomically
- [x] Index writer writes 32-byte events sequentially
- [x] Detail writer writes length-prefixed events
- [x] Bidirectional linking (detail_seq ↔ index_seq) works
- [x] Writer finalization updates headers and writes footers
- [ ] Drain thread integration complete (DEFERRED: planned for separate iteration)
- [x] Unit tests pass (100% coverage on new code)
- [x] Integration tests pass
- [ ] Write throughput exceeds 10M events/sec (DEFERRED: benchmark test)
- [ ] Query engine can read output files (DEFERRED: requires M1_E5_I2 ATF V2 Reader)

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| Byte alignment issues | High | Medium | Use packed structs, static_assert sizes |
| Write performance regression | High | Low | Profile hot path, use buffered I/O |
| Detail file holes | Medium | Low | Compact design prevents holes |
| Crash during finalization | Medium | Medium | Footer-based recovery |
| Cross-platform endianness | Low | Low | Canonical little-endian with header flag |

## Dependencies

### Depends On:
- M1_E2_I1: Ring Buffer (provides SPSC infrastructure)
- M1_E2_I2: Drain Thread (integration point)

### Depended By:
- M1_E5_I2: ATF V2 Reader (consumes output files)
