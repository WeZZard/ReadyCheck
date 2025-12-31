---
epic: E2
milestone: M1
slug: M1_E2_INDEX_PIPELINE
status: completed
---

# Epic M1_E2: Index Pipeline

**Goal:** Implement the event drain pipeline from shared memory to ATF files, including CLI interface and signal handling.

## Features

- **Drain Thread:** Background thread for draining events from SHM
- **ATF Writer:** Binary format writer for trace data
- **CLI Parser:** Command-line interface for tracer control
- **Signal Handling:** Graceful shutdown on SIGINT/SIGTERM

## Constraints

- **Non-Blocking Drain:** Drain thread must not block agent writes
- **Deterministic Shutdown:** Clean termination with all data persisted

## Iteration Breakdown

| Iteration | Focus | Status |
|-----------|-------|--------|
| I1: Drain Thread | Background drain from ring buffers | completed |
| I2: Per-Thread Drain | Thread-aware event draining | completed |
| I3: ATF V4 Writer | ATF format v4 binary writer | completed |
| I4: CLI Parser | Command-line argument parsing | completed |
| I5: Duration Timer | Trace duration timing control | completed |
| I6: Signal Shutdown | SIGINT/SIGTERM handling | completed |
| I7: Selective Persistence | Triggered event persistence | completed |
