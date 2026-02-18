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

// Helper: check if all characters from ptr to end are lowercase a-z
static bool all_lowercase(const char* ptr, const char* end) {
    for (const char* p = ptr; p < end; ++p) {
        if (*p < 'a' || *p > 'z') return false;
    }
    return ptr < end; // must have at least one char
}

bool ada_is_swift_compiler_stub(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;

    // ── Prefix checks (non-Swift-mangled names) ──

    // __swift_* / ___swift_* — runtime helpers (memcpy, noop, etc.)
    if (strncmp(name, "___swift_", 9) == 0) return true;
    if (strncmp(name, "__swift_", 8) == 0) return true;

    // objectdestroy* / _objectdestroy* — SIL-generated destructors
    if (strncmp(name, "_objectdestroy", 14) == 0) return true;
    if (strncmp(name, "objectdestroy", 13) == 0) return true;

    // block_copy_helper* / block_destroy_helper* — ObjC block ABI helpers
    if (strncmp(name, "block_copy_helper", 17) == 0) return true;
    if (strncmp(name, "block_destroy_helper", 20) == 0) return true;

    // ── Suffix checks (only for Swift-mangled $s/_$s symbols) ──

    const char* mangled = nullptr;
    if (strncmp(name, "_$s", 3) == 0) {
        mangled = name + 3;
    } else if (strncmp(name, "$s", 2) == 0) {
        mangled = name + 2;
    }

    if (mangled == nullptr) return false;

    size_t len = strlen(name);
    const char* end = name + len;

    // Need at least some chars after the prefix for suffix matching
    if (end <= mangled) return false;

    // Fixed two-character suffixes
    // Tm — type metadata accessor
    if (len >= 2 && end[-2] == 'T' && end[-1] == 'm') return true;
    // Wb — back-deploy thunk
    if (len >= 2 && end[-2] == 'W' && end[-1] == 'b') return true;
    // Mi — metaclass init
    if (len >= 2 && end[-2] == 'M' && end[-1] == 'i') return true;
    // Mr — type metadata completion
    if (len >= 2 && end[-2] == 'M' && end[-1] == 'r') return true;
    // Ma — metadata accessor
    if (len >= 2 && end[-2] == 'M' && end[-1] == 'a') return true;
    // Wl — witness table lazy accessor
    if (len >= 2 && end[-2] == 'W' && end[-1] == 'l') return true;
    // WL — witness table lazy accessor (uppercase variant)
    if (len >= 2 && end[-2] == 'W' && end[-1] == 'L') return true;
    // Oe — outlined retain
    if (len >= 2 && end[-2] == 'O' && end[-1] == 'e') return true;
    // Oh — outlined release
    if (len >= 2 && end[-2] == 'O' && end[-1] == 'h') return true;
    // Ob — outlined copy
    if (len >= 2 && end[-2] == 'O' && end[-1] == 'b') return true;
    // Oc — outlined consume
    if (len >= 2 && end[-2] == 'O' && end[-1] == 'c') return true;
    // Od — outlined destroy
    if (len >= 2 && end[-2] == 'O' && end[-1] == 'd') return true;
    // Oy — outlined init with copy
    if (len >= 2 && end[-2] == 'O' && end[-1] == 'y') return true;

    // Three-character suffixes
    // wcp — witness table copy
    if (len >= 3 && end[-3] == 'w' && end[-2] == 'c' && end[-1] == 'p') return true;
    // wca — witness table accessor
    if (len >= 3 && end[-3] == 'w' && end[-2] == 'c' && end[-1] == 'a') return true;
    // wct — witness table template
    if (len >= 3 && end[-3] == 'w' && end[-2] == 'c' && end[-1] == 't') return true;

    // Ow[a-z]+ — outlined value witness
    // Scan backwards: find 'Ow' then verify trailing chars are all lowercase a-z
    for (const char* p = end - 3; p >= mangled; --p) {
        if (p[0] == 'O' && p[1] == 'w') {
            const char* trailing = p + 2;
            if (trailing < end && all_lowercase(trailing, end)) return true;
            break;
        }
    }

    // Vw[a-z]+ — value witness table entries (same scan pattern as Ow)
    for (const char* p = end - 3; p >= mangled; --p) {
        if (p[0] == 'V' && p[1] == 'w') {
            const char* trailing = p + 2;
            if (trailing < end && all_lowercase(trailing, end)) return true;
            break;
        }
    }

    // NOTE: TW (protocol witness thunks) is intentionally NOT filtered.
    // These contain inlined implementations in Release builds.

    return false;
}
