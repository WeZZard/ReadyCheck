# M1_E1_I6 Backlogs: Offset-Only SHM

## Status: ✅ COMPLETED

## Completion Summary
- Refactored SHM structures to use offsets instead of absolute pointers
- Implemented header-only raw ring operations for direct buffer access
- Controller caching optimized to avoid repeated attach/destroy operations
- Performance targets maintained: fast path unchanged, registration overhead minimal
- All tests passing; SHM inspection confirms offset-only representation

## Sprint Overview
Duration: 2 days
Priority: P0 (foundational for relocatability)
Dependencies: I5 complete

## Tasks

1) Refactor SHM structs (4h)
- Remove absolute pointers from SHM layout headers.
- Add `layout_off` and `RingDescriptor {bytes, offset}`.
- Keep SPSC queues and counters unchanged.

2) Registration writes offsets (3h)
- Allocate layout and rings from arena bump allocator.
- Populate offsets and sizes; initialize queues.
- Mark active with release ordering.

3) Raw ring helpers + per-call compute (3h)
- Add header-only raw ring operations working directly on RingBufferHeader (no handles).
- Update lane accessors to compute addresses per call and invoke raw helpers.

3.5) Controller caching (2h)
- Cache attached per-thread ring views by slot index in the controller to avoid repeated attach/destroy.
- Validate lifetime across iterations and thread unregister/re-register.

4) Tests and perf (4h)
- Unit: invariants and materialization.
- Integration: attach + round-trip.
- Perf sanity: fast path and registration budgets.

5) Docs (2h)
- Update architecture notes; record SHM schema changes.

## Acceptance
- All I6 tests pass; no regressions in prior suites.
- SHM inspection confirms offsets-only representation.

Status update: All tasks completed; unit and integration tests passing locally. Agent/controller updated to use header-only operations; compatibility fallback preserved.

## Notes
- I6 will remove the `ADA_ENABLE_REGISTRY_RINGS` gate by making the offset-only registry path the default data-plane once stable.
- Global rings drain path can remain as fallback/testing option until fully retired.
