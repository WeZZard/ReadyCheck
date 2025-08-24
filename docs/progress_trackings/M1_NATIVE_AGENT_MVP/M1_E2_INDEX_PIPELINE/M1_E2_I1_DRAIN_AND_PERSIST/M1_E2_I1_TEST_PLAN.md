# Test Plan — M1 E2 I1 Drain and Persist

## Goals
Verify ring-pool swap-and-dump works for both lanes and persistence is correct and bounded.

## Cases
- Index lane: fill to full → exactly one dump; swap occurs immediately; header correct; record count matches within tolerance
- Detail lane: full without marked → no dump; full with marked → exactly one dump; swap occurs; flag cleared
- Pool exhaustion: simulate slow dump; with small K spares, verify `*_pool_exhausted_count` increments and no deadlock; index lane continues

## Procedure
1. Run tracer against `test_cli` for fixed 3–5s; generate enough traffic for index full; validate index dump and header by reading first 32 bytes and counting records
2. Configure agent to mark a known function; run until detail ring becomes full; validate exactly one detail dump occurred (size prefix) and swap happened
3. Throttle controller dump (dev knob) to trigger pool exhaustion; assert counters increment; index lane unaffected

## Acceptance
- Index header valid; record count > 0 and within tolerance; swap latency acceptable
- Detail dump observed only on full+marked; swap immediate; no deadlock; counters updated
