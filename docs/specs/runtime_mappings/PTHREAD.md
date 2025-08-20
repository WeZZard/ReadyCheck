# Runtime Mapping: POSIX Threads (pthread)

Defines correlation for thread-based async.

## Hook points

- Thread creation: `pthread_create`
- Thread routine: function pointer passed to `pthread_create`
- Thread termination: return from routine or `pthread_exit`

## Correlation keys

- `pthread_t` / thread ID, routine function pointer

## Spans

- Logical span start: at routine entry on created thread
- Logical span end: at routine return or `pthread_exit`
- Optional: record association to creator thread via `pthread_create` site

## Notes

- Joinable vs detached threads affect lifecycle; record attributes when available.
