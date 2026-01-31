// Debug dylib detection for Apple platforms
//
// Xcode Debug builds with ENABLE_DEBUG_DYLIB=YES create a thin "stub" executable
// that loads the real application code from a *.debug.dylib. This module detects
// such stubs and identifies the real dylib to trace.
//
// Detection heuristic: Parse Mach-O load commands for LC_LOAD_DYLIB referencing
// a *.debug.dylib file.

#ifndef ADA_DEBUG_DYLIB_DETECTION_H
#define ADA_DEBUG_DYLIB_DETECTION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Information about a detected debug dylib stub
typedef struct {
    /// True if the binary at base_address is a debug stub
    bool is_debug_stub;

    /// Path to the debug dylib (e.g., "/path/to/App.debug.dylib")
    /// Only valid if is_debug_stub is true.
    char debug_dylib_path[1024];

    /// Base address of the loaded debug dylib (0 if not yet found)
    /// Use ada_find_loaded_debug_dylib() to populate this after detection.
    uintptr_t debug_dylib_base;
} DebugDylibInfo;

/// Detect if a Mach-O binary is a debug dylib stub.
///
/// Parses the Mach-O load commands at base_address looking for LC_LOAD_DYLIB
/// entries that reference a *.debug.dylib file.
///
/// @param base_address  Runtime base address of the loaded Mach-O binary
/// @param module_path   Path to the module (used to resolve @rpath)
/// @param out_info      Output structure to receive detection results
///
/// @return true if detection was successful (check out_info->is_debug_stub),
///         false if base_address or out_info is invalid
bool ada_detect_debug_dylib_stub(uintptr_t base_address,
                                  const char* module_path,
                                  DebugDylibInfo* out_info);

/// Find the loaded debug dylib in the current process.
///
/// Searches the dyld image list for the debug dylib path stored in info.
/// If found, updates info->debug_dylib_base with the runtime base address.
///
/// @param info  Debug dylib info (must have valid debug_dylib_path)
///
/// @return true if the debug dylib was found and base address was set,
///         false if info is null, path is empty, or dylib not found
bool ada_find_loaded_debug_dylib(DebugDylibInfo* info);

#ifdef __cplusplus
}
#endif

#endif // ADA_DEBUG_DYLIB_DETECTION_H
