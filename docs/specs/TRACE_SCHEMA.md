# ADA Trace Schema V4

This document defines the Protocol Buffers (Protobuf) schema for the ADA Trace Format (ATF). This schema is the data contract between the Instrumentation Backends (eBPF/Frida) and the Query Engine.

It is optimized to capture the specific data provided by function-level hooking and probing.

```protobuf
syntax = "proto3";

import "google/protobuf/timestamp.proto";

// The top-level message for any event in the trace.
message Event {
  uint64 event_id = 1;
  int32 thread_id = 2;
  google.protobuf.Timestamp timestamp = 3;

  oneof payload {
    TraceStart trace_start = 10;
    TraceEnd trace_end = 11;
    FunctionCall function_call = 12;
    FunctionReturn function_return = 13;
    SignalDelivery signal_delivery = 14;
  }
}

// --- Event Payloads ---

message TraceStart {
  string executable_path = 1;
  repeated string args = 2;
  string operating_system = 3; // e.g., "linux", "darwin"
  string cpu_architecture = 4; // e.g., "x86_64", "aarch64"
}

message TraceEnd {
  int32 exit_code = 1;
}

// Event for when a function is called.
// Captured by a uprobe (eBPF) or Frida Interceptor.
message FunctionCall {
  string symbol = 1;
  uint64 address = 2;

  // Contains only the registers relevant to the platform's calling convention (ABI).
  // e.g., on x86_64, this would be RDI, RSI, RDX, RCX, R8, R9.
  map<string, uint64> argument_registers = 3;

  // A shallow copy of the top of the stack.
  bytes stack_shallow_copy = 4;
}

// Event for when a function returns.
// Captured by a uretprobe (eBPF) or Frida Interceptor onReturn.
message FunctionReturn {
  string symbol = 1;
  uint64 address = 2;

  // Contains only the register(s) holding the return value.
  // e.g., on x86_64, this would be RAX.
  map<string, uint64> return_registers = 3;
}

// Event for when a signal is delivered to a thread.
message SignalDelivery {
  int32 number = 1;
  string name = 2; // e.g., "SIGSEGV"

  // For a crash, we want a full snapshot of the CPU state.
  map<string, uint64> registers = 3;
}
```

### Design Rationale

*   **Semantic Events:** This schema focuses on the high-level semantic events of `FunctionCall` and `FunctionReturn`, which eBPF and Frida are designed to provide directly.
*   **Efficiency:** Instead of capturing all CPU registers for every event, we only capture the registers relevant to the specific event type. Argument-passing registers are stored for a `FunctionCall`, return-value registers for a `FunctionReturn`, and a full register dump is reserved for critical events like a `SignalDelivery`.
*   **Portability:** By abstracting registers into a `map<string, uint64>`, the format remains portable across different CPU architectures. The Query Engine will contain the logic to interpret which keys (`"RDI"`, `"RAX"`, etc.) are meaningful on each architecture.