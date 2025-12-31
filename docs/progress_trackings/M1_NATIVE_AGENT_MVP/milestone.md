---
milestone: M1
slug: M1_NATIVE_AGENT_MVP
status: completed
---

# Milestone M1: Native Agent MVP

**Goal:** Deliver a minimal viable product for native agent tracing on macOS, including process injection, function hooking, ring buffer management, event collection, and ATF file generation.

## High-Level User Stories

| ID | Story | Status |
|----|-------|--------|
| US-014 | Start program with tracing | completed |
| US-016 | Trace completion notification | completed |
| US-017 | Arm tracing triggers | completed |
| US-018 | Select key symbols | completed |
| US-019 | Principled startup timeout | completed |
| US-020 | Simple CLI tracing | completed |

## Epic Breakdown

| Epic | Focus | Iterations | Status |
|------|-------|------------|--------|
| E1: Native Agent Injection | Shared memory, ring buffer, agent loading, thread registration | 10 | completed |
| E2: Index Pipeline | Drain thread, ATF writer, CLI parser, signal shutdown | 7 | completed |
| E3: Hardening | Backpressure, metrics collection/reporting, integration validation | 4 | completed |
| E4: Query Engine | ATF reader, JSON-RPC server, trace info and events APIs | 4 | completed |
| E5: Documentation | Getting started, architecture, API reference, examples | 4 | completed |
| E6: Push to Work | Async script load timeout handling | 1 | completed |
| E7: ATF V2 | ATF V2 writer, reader, and integration | 3 | completed |

## Summary

Milestone M1 established the complete tracing infrastructure:
- **Data Plane:** Native C/C++ agent with Frida-based injection and hooking
- **Control Plane:** Rust tracer for lifecycle management and event collection
- **Query Layer:** Python-based ATF reader with JSON-RPC API
- **Documentation:** Comprehensive getting started, architecture, and API docs
