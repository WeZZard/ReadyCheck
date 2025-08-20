# Two-Lane Flight Recorder Architecture Explained

**Date**: 2025-08-15  
**Type**: Technical Explanation  
**Purpose**: Clarify index vs detail events in the tracer

## Core Concept: Two-Lane Ring Buffer

The ADA Tracer uses a **two-lane architecture** inspired by aircraft flight recorders. Think of it like having two separate recording devices running simultaneously:

1. **Index Lane**: Lightweight, always-on recording
2. **Detail Lane**: Heavy, on-demand recording

```
Timeline:  [=====================================>]
           
Index Lane: ●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●  (continuous)
Detail Lane: ............██████████.............  (only when triggered)
                         ↑        ↑
                      trigger  trigger
                       fires    stops
```

## What is an "Event"?

An **event** is anything that happens during program execution that we want to record:
- Function call (entering a function)
- Function return (exiting a function)
- Exception thrown
- Memory allocation
- System call

## Index Events vs Detail Events

### Index Event (24 bytes) - The "Black Box"
```c
struct IndexEvent {
    uint64_t timestamp;    // WHEN: Nanosecond timestamp
    uint64_t function_id;  // WHAT: Which function (address/ID)
    uint32_t thread_id;    // WHERE: Which thread
    uint32_t event_kind;   // TYPE: Call/Return/Exception
}
```

**Purpose**: Minimal recording to understand program flow
**Example**: "Function malloc() was called at time T on thread 1"

### Detail Event (512 bytes) - The "Crash Recorder"
```c
struct DetailEvent {
    uint64_t timestamp;         // WHEN: Same as index
    uint64_t function_id;       // WHAT: Same as index
    uint32_t thread_id;         // WHERE: Same as index
    uint32_t event_kind;        // TYPE: Same as index
    
    // ADDITIONAL DETAIL:
    uint64_t x_regs[8];         // ARM64 registers (function arguments)
    uint64_t lr;                // Link register (return address)
    uint64_t fp;                // Frame pointer
    uint64_t sp;                // Stack pointer
    uint8_t stack_snapshot[128]; // Actual stack memory contents
    uint32_t stack_size;
    uint8_t _padding[272];      // (wasted space - to be optimized)
}
```

**Purpose**: Full context for debugging crashes
**Example**: "Function malloc() called with size=1024, from address 0x1234, stack contains [...]"

## Real-World Example

Let's trace this simple program:
```c
int main() {
    char* buffer = malloc(1024);  // Event 1
    strcpy(buffer, "Hello");      // Event 2
    printf("%s\n", buffer);        // Event 3
    free(buffer);                  // Event 4
    return 0;
}
```

### What Gets Recorded:

#### In Index Lane (ALWAYS):
```
Event 1: [timestamp: 1000, function: malloc,  thread: 1, type: CALL]
Event 2: [timestamp: 1100, function: malloc,  thread: 1, type: RETURN]
Event 3: [timestamp: 1200, function: strcpy,  thread: 1, type: CALL]
Event 4: [timestamp: 1300, function: strcpy,  thread: 1, type: RETURN]
Event 5: [timestamp: 1400, function: printf,  thread: 1, type: CALL]
Event 6: [timestamp: 1500, function: printf,  thread: 1, type: RETURN]
Event 7: [timestamp: 1600, function: free,    thread: 1, type: CALL]
Event 8: [timestamp: 1700, function: free,    thread: 1, type: RETURN]
```

#### In Detail Lane (ONLY IF TRIGGERED):
Say we set a trigger on `strcpy` (because it's dangerous):

```
Event 3 DETAILED: [
    timestamp: 1200,
    function: strcpy,
    thread: 1,
    type: CALL,
    arg0: 0x7fff5000 (destination buffer address),
    arg1: 0x1000dead (source string address),
    return_address: 0x10001234 (where strcpy will return to),
    stack_pointer: 0x7fff4ff0,
    stack_dump: [actual 128 bytes of stack memory],
    ...
]
```

## Why Two Lanes?

### The Problem with Single Recording:
- **Full detail always**: 512 bytes × 1M events/sec = 512 MB/sec (too much!)
- **Minimal only**: Not enough info when crash happens

### The Two-Lane Solution:

| Aspect | Index Lane | Detail Lane |
|--------|------------|-------------|
| **Purpose** | See everything | Debug problems |
| **Size** | 24 bytes/event | 512 bytes/event |
| **When** | Always recording | Only when triggered |
| **Buffer** | 8 MB → 1.5 GB | 64 MB → 1 GB |
| **Duration** | Minutes to hours | Seconds |
| **Use Case** | "What happened?" | "Why did it crash?" |

## Flight Recorder Analogy

Like an aircraft black box:

1. **Cockpit Voice Recorder** (Detail Lane)
   - Records last 30 minutes of cockpit audio
   - Overwrites old data continuously
   - Critical for understanding crash

2. **Flight Data Recorder** (Index Lane)
   - Records basic parameters for hours
   - Altitude, speed, heading
   - Shows overall flight path

## Trigger System

Triggers determine when to start recording detail events:

```rust
// Example triggers
TriggerKind::FunctionName("strcpy")     // Dangerous function
TriggerKind::ExceptionThrown            // Any exception
TriggerKind::MemoryPressure(90)         // Memory > 90%
TriggerKind::Manual                     // User pressed button
```

When triggered:
1. Save previous N milliseconds of detail (pre-roll)
2. Continue recording detail for M milliseconds (post-roll)
3. Return to index-only mode

## Memory Usage Comparison

### Scenario: 1 Million Events/Second

**Index-Only Mode:**
- Data rate: 1M × 24 bytes = 24 MB/sec
- 1 GB buffer holds: 43 seconds

**Detail-Only Mode:**
- Data rate: 1M × 512 bytes = 512 MB/sec
- 1 GB buffer holds: 2 seconds

**Mixed Mode (10% detail):**
- Index: 900K × 24 bytes = 21.6 MB/sec
- Detail: 100K × 512 bytes = 51.2 MB/sec
- Total: 72.8 MB/sec
- Duration: ~14 seconds

## Summary

- **Event**: Any traced program activity (function call, return, etc.)
- **Index Event**: Minimal 24-byte record, always captured
- **Detail Event**: Full 512-byte context, captured on trigger
- **Index Lane**: The continuous, lightweight recording buffer
- **Detail Lane**: The triggered, heavyweight recording buffer

The two-lane design allows us to:
1. Always know what happened (index)
2. Get full context when needed (detail)
3. Use memory efficiently
4. Maintain low overhead

Think of it as having both a security camera (index) that records everything at low quality, and a high-speed camera (detail) that captures critical moments in full detail.