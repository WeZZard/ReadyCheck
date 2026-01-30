# Unified Clock Mechanism for ADA

**Date**: 2026-01-30
**Context**: Investigation of CLI capture producing 0 events revealed clock mismatch between controller and agent.

## Problem

Different components used different clock sources:

| Component | Original Clock | Issue |
|-----------|----------------|-------|
| Controller | `g_get_monotonic_time()` → `mach_continuous_time()` | Counts sleep |
| Agent | `mach_absolute_time()` | Doesn't count sleep |
| Screen recorder | `CMSampleBufferGetPresentationTimeStamp()` | Media clock |
| Voice recorder | `AVAudioRecorder` | Sample-count based |

This caused health check failures when timestamps didn't match, forcing fallback to GLOBAL_ONLY mode.

## Benchmarks (Apple Silicon)

```
mach_absolute_time():        5.45 ns/call
mach_absolute_time()+conv:   8.56 ns/call
clock_gettime(MONOTONIC):   17.91 ns/call
```

## Recommendation: `mach_continuous_time()` with Cached Conversion

```c
static inline uint64_t ada_timestamp_ns(void) {
    static mach_timebase_info_data_t tb = {0};
    if (__builtin_expect(tb.denom == 0, 0)) {
        mach_timebase_info(&tb);
    }
    return mach_continuous_time() * tb.numer / tb.denom;
}
```

### Rationale

| Criterion | `mach_continuous_time()` | `mach_absolute_time()` | `clock_gettime()` |
|-----------|-------------------------|------------------------|-------------------|
| Speed | ~6-8 ns | ~5 ns | ~18 ns |
| Counts sleep | Yes | No | Yes |
| Syscall | No (commpage) | No (commpage) | No (commpage) |
| A/V framework aligned | Yes (CMClock compatible) | Legacy only | Wrapper |

### Cross-Platform Mapping

| Platform | Recommended | Semantics |
|----------|-------------|-----------|
| macOS | `mach_continuous_time()` | Monotonic, includes sleep |
| Linux | `clock_gettime(CLOCK_BOOTTIME)` | Equivalent to continuous |
| Windows | `QueryPerformanceCounter()` | Different sleep semantics |

## A/V Synchronization Strategy

Record a **sync point** at capture start to correlate all timelines:

```c
typedef struct {
    uint64_t trace_ns;      // ada_timestamp_ns()
    CMTime   media_time;    // CMClockGetTime(CMClockGetHostTimeClock())
    uint64_t wall_clock;    // For human-readable correlation
} AdaSyncPoint;
```

During analysis, all components convert to a common timeline using this sync point.

## Current State (Temporary Fix)

Changed agent from `mach_absolute_time()` to `clock_gettime(CLOCK_MONOTONIC)` for compatibility with GLib-based controller. This adds ~10 ns overhead per timestamp but ensures health checks pass.

**Impact**: ~11 μs additional overhead per 584-event trace session. Negligible.

## Future Architecture

When replacing Frida:

1. Use `mach_continuous_time()` uniformly across all native components
2. Record sync point at session start for A/V correlation
3. Consider exposing timestamp API to plugins/extensions for consistency

## References

- Apple: [Mach Absolute Time](https://developer.apple.com/documentation/kernel/1462446-mach_absolute_time)
- Apple: [Mach Continuous Time](https://developer.apple.com/documentation/kernel/1646199-mach_continuous_time)
- GLib: [g_get_monotonic_time](https://docs.gtk.org/glib/func.get_monotonic_time.html)
