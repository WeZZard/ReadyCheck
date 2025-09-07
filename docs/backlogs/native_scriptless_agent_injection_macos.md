# Backlog: Native (Scriptless) Agent Injection on macOS

Context
- MVP uses a QuickJS-based loader to load the native agent dylib and call `agent_init` inside the target process. This path is reliable in our environment.
- Direct native injection (Frida device/injector APIs) intermittently fails on macOS with errors like:
  - "Module not found … Foundation" (earlier)
  - "Module not found … libbsm" or "Malformed dependency ordinal" (after agent linkage cleanup)
- These errors originate from Frida's injector path inside the target process (dyld/policy/loader behavior), not from our agent's own dependencies.

Goal
- Replace the QuickJS loader with a robust native (scriptless) injection path on macOS.

Open Work
- Collect dyld logs in target process (DYLD_PRINT_LIBRARIES/APIS) during injection to classify failures.
- Prototype with Frida Injector APIs (frida_injector_inject_library_file_sync) vs device APIs; compare behavior.
- Validate signing/policy constraints (experimentally, e.g., disable-library-validation on test target only) to isolate hardened runtime impacts (not for shipping).
- Keep agent dependencies minimal (done: Foundation/libbsm removed) and add `@loader_path` RPATH if needed.
- Once stable across spawn/attach targets, make native injection default; update tests/docs; then remove QuickJS loader.

Definition of Done
- Native injection passes all existing loader/lifecycle/integration tests locally and in CI.
- No injector/dyld errors; logs clean without timeouts.
- QuickJS loader removed or gated off by default without test regressions.

