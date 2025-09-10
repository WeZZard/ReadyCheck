# M1_E1_I7 Backlogs: Registry Readiness, Warm‑Up, and Fallback

## Sprint Overview
Duration: 3–4 days
Priority: P0 (production reliability for per‑thread rings)
Dependencies: I6 complete (offsets‑only SHM)

## Tasks
1) IPC fields in ControlBlock (0.5d)
- Add: `registry_ready (u32)`, `drain_heartbeat_ns (u64)`, `registry_epoch (u32)`, `registry_version (u32)`, `registry_mode (u32)`.
- Document memory ordering (controller: release; agent: acquire).

2) Controller readiness + heartbeat (0.5d)
- Set `registry_ready=1` after registry + drain thread live.
- Tick `drain_heartbeat_ns` every loop (monotonic ns).
- Default `registry_mode` to `dual_write` after ready, advance to `per_thread_only` when healthy.

3) Agent state machine (1.5d)
- Implement `global_only → dual_write → per_thread_only` transitions based on readiness + heartbeat.
- Handle stall fallback and recovery.
- React to `registry_epoch` changes by re‑attaching and re‑warming.
- Overflow behavior: mirror to global under spikes.

4) Observability (0.5d)
- Add counters: mirrored events, overflows, fallback activations, last heartbeat seen.
- Lightweight logs on mode/epoch transitions.

5) Tests (1d)
- Unit: state machine transitions, epoch bumps, stall fallback.
- Integration: readiness timing, warm‑up to steady state, drain stall injection.
- Perf: Verify warm‑up overhead bounded; steady‑state identical.

## Acceptance
- Heartbeat visible and advancing; `registry_ready` set.
- Agent reliably reaches `per_thread_only` on healthy controller; falls back and recovers on drain stall.
- No startup/event loss; integration tests green.

## Notes
- Keep env vars only as startup overrides (e.g., kill switch) — not for synchronization.
- Maintain global drain during rollout; deprecate later.

