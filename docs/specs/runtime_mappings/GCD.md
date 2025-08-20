# Runtime Mapping: Grand Central Dispatch (GCD)

Defines hook points and correlation for dispatch queues.

## Symbols / functions

- Submit: `dispatch_async`, `dispatch_barrier_async`, `dispatch_group_enter/leave/notify`
- Execute: internal callout (e.g., `_dispatch_client_callout`) or block invoke trampoline

## Correlation keys

- Block pointer, Queue label, Group pointer

## Events and spans

- Logical span start: on submit; record block pointer and queue.
- Logical span execute: when callout begins; associate with submit via block pointer.
- Logical span end: on callout return; for groups, `leave`/`notify` drive completion.

## Notes

- dispatch_async returns immediately; the logical span is submit→execute-return.
- Some symbols are private; resolve dynamically and gate by OS version.
