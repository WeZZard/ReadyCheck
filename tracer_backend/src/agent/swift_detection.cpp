// Swift symbol detection utilities

#include <tracer_backend/agent/swift_detection.h>
#include <cstdlib>
#include <cstring>

bool ada_is_swift_symbol_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    // Swift mangled names: $s..., $S..., _$s..., _$S...
    if (strncmp(name, "$s", 2) == 0 || strncmp(name, "$S", 2) == 0) return true;
    if (strncmp(name, "_$s", 3) == 0 || strncmp(name, "_$S", 3) == 0) return true;
    // Swift runtime functions
    if (strncmp(name, "swift_", 6) == 0) return true;
    if (strncmp(name, "_swift_", 7) == 0) return true;
    if (strncmp(name, "__swift", 7) == 0) return true;
    return false;
}

bool ada_is_stub_section_id(const char* id) {
    if (id == nullptr || id[0] == '\0') return false;
#ifdef __APPLE__
    if (strstr(id, "__stub_helper") != nullptr) return true;
    if (strstr(id, "__auth_stubs") != nullptr) return true;
    if (strstr(id, "__stubs") != nullptr) return true;
#endif
    return false;
}

bool ada_is_stub_section_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
#ifdef __APPLE__
    if (strstr(name, "__stub_helper") != nullptr) return true;
    if (strstr(name, "__auth_stubs") != nullptr) return true;
    if (strstr(name, "__stubs") != nullptr) return true;
#endif
    return false;
}

bool ada_is_swift_section_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
#ifdef __APPLE__
    if (strstr(name, "__swift") != nullptr) return true;
#endif
    return false;
}

bool ada_should_skip_swift_symbols(void) {
#ifdef __APPLE__
    const char* env = getenv("ADA_HOOK_SWIFT");
    if (env && env[0] == '1') return false;
    return true;
#else
    return false;
#endif
}

bool ada_is_swift_symbolic_metadata(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    // Swift symbolic metadata: "_symbolic ..." or "symbolic ..."
    // These are reflection strings containing type information, not executable code
    if (strncmp(name, "_symbolic", 9) == 0) return true;
    if (strncmp(name, "symbolic", 8) == 0) return true;
    return false;
}
