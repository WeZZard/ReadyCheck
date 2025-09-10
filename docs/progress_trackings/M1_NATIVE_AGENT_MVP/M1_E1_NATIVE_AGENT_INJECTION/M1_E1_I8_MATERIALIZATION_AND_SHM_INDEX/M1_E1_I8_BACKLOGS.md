# M1_E1_I8 Backlogs: Address Materialization & SHM Index Registration

## Sprint Overview
Duration: 1.5 days
Priority: P0
Dependencies: I6 complete

## Tasks

1) Control block: SHM directory (3h)
- Add `ShmDirectory` with one entry (registry arena) published by controller.
- Initialize at controller startup; immutable for session.

2) Per-process mapping (3h)
- Add attach helper to map directory entries and store local bases by index.
- Validate name/size match before use.
- Clarify accessors compute addresses per call; no per-lane caches.

3) Accessors (2h)
- Use per-call address computation `(index, offset)` via inline helpers; for I7, index=0 (registry arena).
- No persistent address caches; no invalidation logic required.

4) Tests (4h)
- Unit/integration as per test plan; error-path tests for mismatches.

5) Docs (1h)
- Document directory schema and attach behavior.

## Acceptance
- Directory present and identical across processes.
- Accessors materialize via index+offset and pass all tests.
