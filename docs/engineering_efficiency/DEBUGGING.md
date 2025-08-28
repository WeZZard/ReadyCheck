# Debugging Guide

This document provides essential debugging techniques and configuration requirements for effective debugging in the ADA project.

## macOS Developer Permissions (CRITICAL)

### The Problem
On macOS, debugging tools like `lldb`, `dtrace`, and `sample` require developer permissions to attach to processes. Without proper permissions, you'll experience:
- LLDB hanging when trying to attach to processes
- "Operation not permitted" errors
- Silent failures in debugging tools
- Inability to examine process memory or threads

### Terminal Multiplexer Warning
**IMPORTANT**: Terminal multiplexers (tmux, screen, etc.) may NOT inherit developer permissions even if your terminal has them. This is because:
- Multiplexers create separate process sessions
- Developer permissions are not automatically propagated to child sessions
- The multiplexer daemon may have been started before permissions were granted

**Best Practice**: When debugging, use a native terminal window or ensure your multiplexer session has proper permissions by restarting it after granting permissions.

### Solution: Enable Developer Permissions

Execute these commands to enable developer permissions for your user:

```bash
# Enable DevToolsSecurity globally
sudo /usr/sbin/DevToolsSecurity --enable

# Add your user to the _developer group
sudo dseditgroup -o edit -t user -a "$USER" _developer

# Verify membership (should show _developer in the list)
groups

# You may need to restart your terminal or even reboot for full effect
```

### Verification
Test that permissions are working:
```bash
# Create a test program
echo 'int main() { while(1); }' > test.c && cc test.c -o test
./test &
TEST_PID=$!

# Try to attach with lldb (should work without sudo)
lldb -p $TEST_PID -o "quit"

# Clean up
kill -9 $TEST_PID
rm test test.c
```

## LLDB Best Practices

### Basic Commands for Debugging Hangs

```bash
# Attach to a running process
lldb -p <PID>

# Get thread list and backtraces
(lldb) thread list
(lldb) bt all

# Examine specific thread
(lldb) thread select <thread_id>
(lldb) bt
(lldb) frame variable
(lldb) register read

# Check for deadlocks
(lldb) thread backtrace all
```

### Automated Debugging Script

For quick diagnosis of hanging processes:

```bash
#!/bin/bash
PID=$1
lldb -p $PID -batch \
    -o "thread list" \
    -o "bt all" \
    -o "thread select 1" \
    -o "frame variable" \
    -o "quit"
```

### Debugging Core Dumps

```bash
# Enable core dumps
ulimit -c unlimited

# Run your program
./your_program

# If it crashes, debug the core
lldb ./your_program -c /cores/core.<PID>
```

## Alternative Debugging Tools

### 1. sample (macOS-specific)
Quick way to get stack traces without attaching a debugger:
```bash
sample <PID> <duration_in_seconds> -file output.txt
```

### 2. dtrace/dtruss (macOS)
Trace system calls:
```bash
sudo dtruss -p <PID>
```

### 3. leaks
Check for memory leaks:
```bash
leaks <PID>
```

### 4. vmmap
Examine virtual memory:
```bash
vmmap <PID>
```

## Build Optimization for Debugging

### CMake Debug Builds
Always use debug builds when investigating issues:
```cmake
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

### Cargo Debug Builds
```bash
cargo build  # Debug by default
cargo build --release  # Only for performance testing
```

### Compiler Flags for Better Debugging
- `-g3`: Maximum debug information
- `-O0`: No optimization (easier to follow in debugger)
- `-fno-omit-frame-pointer`: Better stack traces
- `-fsanitize=address`: Address sanitizer for memory issues
- `-fsanitize=thread`: Thread sanitizer for race conditions

## Common Debugging Patterns

### 1. Printf Debugging Enhancement
When adding debug output:
```c++
#define DEBUG_LOG(fmt, ...) \
    do { \
        printf("[%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); \
        fflush(stdout); \
    } while(0)
```

Always use `fflush(stdout)` after printf in multithreaded code to ensure output is visible immediately.

### 2. Binary Search for Hangs
When a test hangs:
1. Add printf statements at strategic points
2. Binary search to narrow down the exact line
3. Check for:
   - Infinite loops
   - Deadlocks (mutex/atomic operations)
   - Blocking I/O
   - Race conditions

### 3. Thread Synchronization Issues
Common patterns to check:
```c++
// Potential deadlock
mutex1.lock();
mutex2.lock();  // Thread A locks in this order

// Another thread:
mutex2.lock();
mutex1.lock();  // Thread B locks in opposite order - DEADLOCK!
```

### 4. Memory Corruption Detection
```bash
# Use AddressSanitizer
export ASAN_OPTIONS=verbosity=1:halt_on_error=0:print_stats=1
cargo clean && cargo build
./target/debug/your_test
```

## Integration Test Debugging

### Running Single Tests
```bash
# Google Test
./test_binary --gtest_filter="TestSuite.TestName"

# Cargo
cargo test test_name

# CTest
ctest -R test_name -V
```

### Verbose Output
```bash
# Google Test
./test_binary --gtest_print_time=1

# Cargo
cargo test -- --nocapture

# CTest
ctest -V
```

### Timeout Issues
For long-running tests:
```bash
# Increase test timeout
export RUST_TEST_TIME_UNIT=60000
cargo test

# Or use timeout command
timeout 60 ./test_binary
```

## Performance Profiling

### Instruments (macOS)
```bash
instruments -t "Time Profiler" -D output.trace ./your_program
```

### Sample-based Profiling
```bash
# Simple sampling
while true; do
    sample $PID 0.1 2>/dev/null | grep "your_function"
    sleep 1
done
```

## Troubleshooting Build Issues

### Force Rebuild Detection
When changes aren't being detected:
```bash
# For Cargo + CMake projects
rm -rf target/debug/build/<package>-*
cargo build

# For pure CMake
rm -rf build/CMakeCache.txt
cmake ..
```

### Add Proper Dependencies in build.rs
```rust
fn main() {
    // Tell Cargo to rerun if these change
    println!("cargo:rerun-if-changed=src/");
    println!("cargo:rerun-if-changed=include/");
    println!("cargo:rerun-if-changed=CMakeLists.txt");
}
```

## Quick Debugging Checklist

When debugging a hanging or crashing program:

1. ✅ Check developer permissions are enabled
2. ✅ Use native terminal (not multiplexer) for first attempt
3. ✅ Build with debug symbols (`-g` flag)
4. ✅ Disable optimizations (`-O0`)
5. ✅ Add strategic printf/fflush statements
6. ✅ Check for obvious issues:
   - Array bounds
   - Null pointer dereferences
   - Uninitialized variables
   - Race conditions
   - Deadlocks
7. ✅ Use appropriate sanitizer (address/thread/undefined)
8. ✅ Capture core dump if crashing
9. ✅ Get thread backtraces if hanging
10. ✅ Check system resources (memory, file descriptors)

## Platform-Specific Notes

### macOS
- SIP (System Integrity Protection) may block some debugging
- Code signing requirements for injected code
- Use `codesign` for debugging signed binaries

### Linux
- Check ptrace_scope: `cat /proc/sys/kernel/yama/ptrace_scope`
- May need CAP_SYS_PTRACE capability
- Use `gdb` instead of `lldb`

### CI/SSH Debugging
- Remember to sign binaries for remote debugging
- Use `screen` or `tmux` to maintain sessions
- Consider using `rr` for deterministic replay debugging

## References
- [LLDB Tutorial](https://lldb.llvm.org/use/tutorial.html)
- [Apple Developer Tools](https://developer.apple.com/documentation/security/disabling_and_enabling_system_integrity_protection)
- [Google Test Advanced Guide](https://github.com/google/googletest/blob/master/docs/advanced.md)
- [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer)