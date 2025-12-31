---
epic: E1
milestone: M1
slug: M1_E1_NATIVE_AGENT_INJECTION
status: completed
---

# Epic M1_E1: Native Agent Injection

**Goal:** Establish the core infrastructure for native agent injection, including shared memory setup, ring buffer management, and thread registration.

## Features

- **Shared Memory IPC:** Lock-free shared memory between agent and tracer
- **Ring Buffer Core:** High-performance circular buffer for event logging
- **Agent Loader:** Frida-based agent injection and loading
- **Thread Registry:** Per-thread state tracking and registration

## Constraints

- **Lock-Free Design:** No mutex/lock primitives in hot path
- **Offset-Only Addressing:** All pointers stored as offsets for cross-process safety
- **Cache-Line Alignment:** Data structures aligned for optimal CPU cache usage

## Iteration Breakdown

| Iteration | Focus | Status |
|-----------|-------|--------|
| I1: Shared Memory Setup | Establish SHM allocation and mapping | completed |
| I2: Thread Registry | Per-thread state and TLS management | completed |
| I3: Ring Buffer Core | Lock-free ring buffer implementation | completed |
| I4: Agent Loader | Frida script injection and loading | completed |
| I5: Thread Registration | Agent-side thread registration flow | completed |
| I6: Offset-Only SHM | Convert pointers to offset-based addressing | completed |
| I7: Registry Readiness and Fallback | Ready signal and fallback mechanisms | completed |
| I8: Materialization and SHM Index | Per-call address materialization | completed |
| I9: Ring Pool Swap | Pool-based ring buffer management | completed |
| I10: Broad Coverage Hooks | Extended hook coverage for tracing | completed |
