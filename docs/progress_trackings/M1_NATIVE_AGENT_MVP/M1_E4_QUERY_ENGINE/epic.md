---
epic: E4
milestone: M1
slug: M1_E4_QUERY_ENGINE
status: completed
---

# Epic M1_E4: Query Engine

**Goal:** Build the query layer for reading and analyzing ATF trace files via JSON-RPC API.

## Features

- **ATF Reader:** Python-based ATF file parser
- **JSON-RPC Server:** RPC interface for query operations
- **Trace Info API:** Metadata and summary endpoints
- **Events/Spans API:** Detailed event and span queries

## Constraints

- **Token Budget Aware:** Responses sized for AI agent consumption
- **Streaming Support:** Large result sets streamed efficiently

## Iteration Breakdown

| Iteration | Focus | Status |
|-----------|-------|--------|
| I1: ATF Reader | ATF binary format parser | completed |
| I2: JSON-RPC Server | RPC server implementation | completed |
| I3: Trace Info API | /trace_info endpoint | completed |
| I4: Events/Spans API | /events and /spans endpoints | completed |
