# Runtime Mapping: Swift Concurrency

Defines the hook points and correlation strategy for Swift async/await.

## Symbols to instrument (names may vary by version)

- `swift_task_create`, `swift_task_enqueue`, `swift_job_run`
- `swift_continuation_init`, `swift_continuation_resume`
- `swift_task_future_complete`
- Optional: `swift_task_group_*`, `swift_task_enqueueOnExecutor`

## Correlation keys

- Task ID / Job pointer (from `swift_job_run` argument)
- Continuation pointer

## Events and spans

- Logical span start: on `swift_job_run` entry; attribute top Swift frame via backtrace for function name.
- Suspension: on `swift_continuation_init` (capture continuation pointer); mark current logical span suspended.
- Resume: on `swift_continuation_resume` (by continuation pointer); advance state.
- Logical span end: upon final resume completion or `swift_task_future_complete`.

## Notes

- Use monotonic timestamp (mach time).
- Attribute to async function by capturing a short backtrace at `swift_job_run`.
- Performance: treat as hot path; shared-memory ring buffer; minimal per-event work.
