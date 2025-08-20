# Span Semantics and Correlation (Normative)

Defines how ADA models execution spans and stitches events across async and parallel runtimes.

## 1. Span Types
- Frame span: delimited by machine-level function entry/return (prologue/epilogue). Always emitted when hooks exist.
- Logical span: delimited by the runtime-level start/complete of an async job, task, block, or operation. Preferred for narratives.

Precedence: logical spans override frame spans for the same high-level function; when absent, fall back to frame span.

## 2. Pairing Rules
- Sync functions: frame ENTRY↔RETURN.
- Async functions (Swift concurrency): job start ↔ final continuation resume completion.
- GCD (dispatch_async): submit ↔ block execute complete.
- pthreads: thread start routine entry ↔ return (or pthread_exit).
- Callback pattern: initiating call ↔ callback entry/return.

Unmatched allowed when: cancellation, crash, tail-calls, longjmp/exceptions, process exit. Annotate reason when detectable.

## 3. Correlation Keys
- Swift concurrency: Task ID / Job pointer, Continuation pointer.
- GCD: Block pointer, Queue label, Group pointer (for groups/notify).
- pthreads: pthread_t / TID, start routine function pointer.
- NSOperation: Operation object pointer.

## 4. Timestamps
- Use monotonic clocks in tracer backends; encode as Protobuf Timestamp.
- For logical spans that cross threads, maintain ordering with recorded ns timestamps; do not assume causality from thread IDs.

## 5. Event Mapping to ATF V4
- Frame spans: FunctionCall/FunctionReturn from TRACER_SPEC capture prologue/epilogue.
- Logical spans: represented as FunctionCall/FunctionReturn on synthesized function names with correlation metadata fields (extensions) or via derived indexes. MVP: emit frame events; derive logical spans offline.

## 6. Derived Span Construction (MVP)
- Build per-thread shadow stacks for frame spans.
- For logical spans, join runtime-specific events (see runtime mapping docs) using correlation keys; create derived span records (not necessarily re-emitted, but indexed for queries).

## 7. Timeouts & Cancellations
- Default unmatched timeout: 5s (configurable) to consider a span “open”; annotate upon closure if resumed later.
- Cancellation events close spans with reason=canceled.

## 8. Metrics
- Span build success rate, unmatched counts, average and p99 durations, number of cross-thread spans.

## 9. References
- See runtime mappings under `docs/specs/runtime_mappings/` for platform-specific hooks.
