// Swift symbol detection utilities
// Used by frida_agent.cpp to filter Swift stubs and symbols

#ifndef TRACER_BACKEND_AGENT_SWIFT_DETECTION_H
#define TRACER_BACKEND_AGENT_SWIFT_DETECTION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Check if a symbol name is a Swift symbol (mangled or runtime)
// Returns true for: $s..., $S..., _$s..., _$S..., swift_..., _swift_..., __swift...
bool ada_is_swift_symbol_name(const char* name);

// Check if a section ID is a stub section (macOS: __stubs, __auth_stubs, __stub_helper)
// Stub sections contain trampolines, not actual code
bool ada_is_stub_section_id(const char* id);

// Check if a section name is a stub section
bool ada_is_stub_section_name(const char* name);

// Check if a section name is a Swift section (macOS: __swift...)
bool ada_is_swift_section_name(const char* name);

// Check if Swift symbols should be skipped based on environment
// Returns true if ADA_HOOK_SWIFT is not set to "1"
// This is the legacy behavior - default skip Swift symbols
bool ada_should_skip_swift_symbols(void);

#ifdef __cplusplus
}
#endif

#endif // TRACER_BACKEND_AGENT_SWIFT_DETECTION_H
