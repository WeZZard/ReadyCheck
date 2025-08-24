# Tech Design — M1 E2 I1 Drain and Persist

## Objective
Implement ring-pool swap-and-dump for both lanes and persist snapshots to durable files (index always; detail only on dump triggers).

## Design
- Control block
  - Per lane: ring descriptors array, `active_ring_idx` (atomic), `submit_q` (agent→controller) and `free_q` (controller→agent)
  - Detail-only: `marked_event_seen_since_last_dump`
- Agent behavior
  - Index: on ring full → push active idx to `index_submit_q`; swap to `index_free_q` spare if available; else continue with drop-oldest until spare returns
  - Detail: on marked event set flag; on ring full AND flag → push to `detail_submit_q`; swap to spare; clear flag; else continue with drop-oldest
- Controller behavior
  - Poll submit queues; on ring idx → snapshot the fixed-size ring (wrap-safe) and write to file
  - Return ring idx to `free_q`; update dump metrics
- File formats
  - Index: `<output>/index.idx` with header (32B: magic `ADAIDX1\0`, record_size, pid, session_id, reserved) and fixed records (`IndexEvent`)
  - Detail: `<output>/detail.bin` raw snapshots per dump (later: segment headers); for M1, allow a simple concatenation with per-dump size prefix

## Out of Scope
- Protobuf encoding; manifest; global indexes beyond per-file headers.

## References
- docs/tech_designs/SHARED_MEMORY_IPC_MECHANISM.md
