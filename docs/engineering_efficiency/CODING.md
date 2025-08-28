# CODING

## When This Applies
You are in CODING stage when:
- Implementing features
- Writing source files
- Creating tests
- Adding new components

## MANDATORY Rules for Coding

### 1. Build Orchestration - CRITICAL
**CARGO IS THE SINGLE DRIVER - NO EXCEPTIONS**

#### Adding Tests
When you add ANY test to CMakeLists.txt, you MUST:
1. Add test definition to `CMakeLists.txt`
2. **ADD TEST TO `build.rs` binaries list** ← YOU FORGOT THIS!
   ```rust
   ("build/test_name", "test/test_name"),
   ```
3. Tests run via `cargo test`, NEVER directly

#### Common Mistakes
❌ **WRONG**: 
```bash
cd tracer_backend && cmake -B build && ./build/test_something
```

✅ **RIGHT**:
```bash
cargo build --release
./target/release/tracer_backend/test/test_something
```

### 2. Language-Specific Rules

#### C/C++
- Include guards in headers
- Use `extern "C"` for C++ → C interfaces
- Memory layout must be explicit (no tail pointer arithmetic)
- Provide debug_dump() functions

#### Rust
- All C/C++ components accessed via build.rs orchestration
- Use bindgen for FFI bindings
- Tests run through cargo test

#### Python
- Built via maturin
- Never run pytest directly
- Use `cargo test` for Python tests

### 3. Memory Layout (Critical for C/C++)
❌ **WRONG**: Unstructured tail allocation
```c
uint8_t* tail = memory + sizeof(Header);
tail += some_size;  // What is this?
```

✅ **RIGHT**: Explicit structures
```c
struct Layout {
    Header header;
    Data data[SIZE];
    Queue queue[COUNT];
};
```

### 4. Test Integration
Every test needs:
1. Definition in CMakeLists.txt
2. Entry in build.rs binaries list
3. Orchestration through Cargo

## Build Artifacts Location (After `cargo build`)

### Predictable Paths (No Hash Suffixes)
- **Binaries**: `target/{debug,release}/tracer_backend/bin/`
- **Tests**: `target/{debug,release}/tracer_backend/test/`
- **Libraries**: `target/{debug,release}/tracer_backend/lib/`
- **Examples**: `target/{debug,release}/frida_examples/`

### IDE Integration (compile_commands.json)

Automatically generated for C/C++ IntelliSense:
- **Generated**: `target/debug/build/<component>-*/out/build/compile_commands.json`
- **Symlink**: `target/compile_commands.json`
- **VSCode**: Pre-configured in `.vscode/c_cpp_properties.json`
- **CLion**: Point to `target/compile_commands.json`
- **Neovim/clangd**: Uses symlink automatically

Benefits:
- Accurate code completion
- Include path resolution
- Go to definition
- Error squiggles

## Project Structure Requirements

```
component/
├── Cargo.toml       # MANDATORY - Component manifest
├── build.rs         # For C/C++ orchestration
├── src/            # Source code
├── include/        # C/C++ headers
├── tests/          # Test files
└── CMakeLists.txt  # ONLY if C/C++, NEVER run directly
```

## Debugging

### C/C++ Binaries
```bash
# Debug test with LLDB
lldb target/release/tracer_backend/test/test_name
(lldb) run --gtest_filter="pattern"
```

### Rust Code
```bash
# With backtrace
RUST_BACKTRACE=1 cargo test
```

## Red Flags
If you're:
- Running CMake directly
- Using raw pointer arithmetic  
- Running tests from build/ directory
- Not updating build.rs when adding tests
- Looking for binaries in CMake build/ directory

STOP! You're violating coding requirements.