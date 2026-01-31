// Debug dylib detection for Apple platforms (Mach-O LC_LOAD_DYLIB parsing)
//
// Detects Xcode Debug configuration builds that use ENABLE_DEBUG_DYLIB=YES,
// which creates a thin stub executable that loads real code from *.debug.dylib.

#include <tracer_backend/agent/debug_dylib_detection.h>

#ifdef __APPLE__
#include <mach-o/loader.h>
#include <mach-o/dyld.h>
#include <cstring>
#include <cstdio>    // snprintf
#include <libgen.h>  // dirname

namespace {

/// Check if a dylib name matches the debug dylib pattern.
/// Pattern: basename ends with ".debug.dylib"
bool is_debug_dylib_name(const char* name) {
    if (!name) return false;

    const char* suffix = ".debug.dylib";
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);

    if (name_len < suffix_len) return false;

    return strcmp(name + name_len - suffix_len, suffix) == 0;
}

/// Resolve @rpath/ prefix in a dylib path relative to the executable directory.
/// Returns true if resolution was successful (or no @rpath prefix).
bool resolve_rpath(const char* dylib_name, const char* module_path,
                   char* resolved_path, size_t resolved_size) {
    if (!dylib_name || !resolved_path || resolved_size == 0) return false;

    const char* rpath_prefix = "@rpath/";
    const char* executable_prefix = "@executable_path/";

    if (strncmp(dylib_name, rpath_prefix, strlen(rpath_prefix)) == 0 ||
        strncmp(dylib_name, executable_prefix, strlen(executable_prefix)) == 0) {
        // Need to resolve relative to module directory
        if (!module_path) return false;

        // Get directory of the module
        char module_copy[1024];
        strncpy(module_copy, module_path, sizeof(module_copy) - 1);
        module_copy[sizeof(module_copy) - 1] = '\0';
        char* dir = dirname(module_copy);

        // Get the relative path part (after @rpath/ or @executable_path/)
        const char* relative;
        if (strncmp(dylib_name, rpath_prefix, strlen(rpath_prefix)) == 0) {
            relative = dylib_name + strlen(rpath_prefix);
        } else {
            relative = dylib_name + strlen(executable_prefix);
        }

        // Combine: dir + "/" + relative
        int written = snprintf(resolved_path, resolved_size, "%s/%s", dir, relative);
        return written > 0 && static_cast<size_t>(written) < resolved_size;
    }

    // No @rpath prefix, use as-is
    strncpy(resolved_path, dylib_name, resolved_size - 1);
    resolved_path[resolved_size - 1] = '\0';
    return true;
}

} // anonymous namespace

extern "C" {

bool ada_detect_debug_dylib_stub(uintptr_t base_address,
                                  const char* module_path,
                                  DebugDylibInfo* out_info) {
    if (base_address == 0 || out_info == nullptr) {
        return false;
    }

    // Initialize output
    out_info->is_debug_stub = false;
    out_info->debug_dylib_path[0] = '\0';
    out_info->debug_dylib_base = 0;

    // Check Mach-O magic
    const uint32_t magic = *reinterpret_cast<const uint32_t*>(base_address);

    const uint8_t* load_commands_start = nullptr;
    uint32_t ncmds = 0;

    if (magic == MH_MAGIC_64) {
        // 64-bit Mach-O
        const struct mach_header_64* header =
            reinterpret_cast<const struct mach_header_64*>(base_address);
        load_commands_start = reinterpret_cast<const uint8_t*>(
            base_address + sizeof(struct mach_header_64));
        ncmds = header->ncmds;
    } else if (magic == MH_MAGIC) {
        // 32-bit Mach-O (less common but handle for completeness)
        const struct mach_header* header =
            reinterpret_cast<const struct mach_header*>(base_address);
        load_commands_start = reinterpret_cast<const uint8_t*>(
            base_address + sizeof(struct mach_header));
        ncmds = header->ncmds;
    } else {
        // Not a Mach-O binary
        return false;
    }

    // Iterate through load commands to find LC_LOAD_DYLIB with .debug.dylib
    const uint8_t* cmd_ptr = load_commands_start;
    for (uint32_t i = 0; i < ncmds; ++i) {
        const struct load_command* lc =
            reinterpret_cast<const struct load_command*>(cmd_ptr);

        if (lc->cmd == LC_LOAD_DYLIB) {
            const struct dylib_command* dylib_cmd =
                reinterpret_cast<const struct dylib_command*>(cmd_ptr);

            // Get the dylib name (null-terminated string after the struct)
            const char* dylib_name = reinterpret_cast<const char*>(
                cmd_ptr + dylib_cmd->dylib.name.offset);

            if (is_debug_dylib_name(dylib_name)) {
                out_info->is_debug_stub = true;

                // Resolve @rpath if present
                if (!resolve_rpath(dylib_name, module_path,
                                   out_info->debug_dylib_path,
                                   sizeof(out_info->debug_dylib_path))) {
                    // Fallback: copy as-is
                    strncpy(out_info->debug_dylib_path, dylib_name,
                            sizeof(out_info->debug_dylib_path) - 1);
                    out_info->debug_dylib_path[sizeof(out_info->debug_dylib_path) - 1] = '\0';
                }

                return true;
            }
        }

        cmd_ptr += lc->cmdsize;
    }

    // No debug dylib dependency found
    return true;
}

bool ada_find_loaded_debug_dylib(DebugDylibInfo* info) {
    if (!info || info->debug_dylib_path[0] == '\0') {
        return false;
    }

    // Search dyld image list for the debug dylib
    uint32_t image_count = _dyld_image_count();
    for (uint32_t i = 0; i < image_count; ++i) {
        const char* image_name = _dyld_get_image_name(i);
        if (!image_name) continue;

        // Check for exact match or basename match
        if (strcmp(image_name, info->debug_dylib_path) == 0) {
            // Get the base address from the mach_header pointer
            const struct mach_header* header = _dyld_get_image_header(i);
            if (header) {
                info->debug_dylib_base = reinterpret_cast<uintptr_t>(header);
                return true;
            }
        }

        // Also check if basenames match (in case paths differ)
        const char* image_basename = strrchr(image_name, '/');
        image_basename = image_basename ? image_basename + 1 : image_name;

        const char* target_basename = strrchr(info->debug_dylib_path, '/');
        target_basename = target_basename ? target_basename + 1 : info->debug_dylib_path;

        if (strcmp(image_basename, target_basename) == 0 &&
            is_debug_dylib_name(image_basename)) {
            const struct mach_header* header = _dyld_get_image_header(i);
            if (header) {
                // Update path to the actual loaded path for accuracy
                strncpy(info->debug_dylib_path, image_name,
                        sizeof(info->debug_dylib_path) - 1);
                info->debug_dylib_path[sizeof(info->debug_dylib_path) - 1] = '\0';
                info->debug_dylib_base = reinterpret_cast<uintptr_t>(header);
                return true;
            }
        }
    }

    return false;
}

} // extern "C"

#else // !__APPLE__

// Stub implementation for non-Apple platforms
// Debug dylibs are an Apple-specific feature (Xcode ENABLE_DEBUG_DYLIB)

extern "C" {

bool ada_detect_debug_dylib_stub(uintptr_t base_address,
                                  const char* module_path,
                                  DebugDylibInfo* out_info) {
    (void)base_address;
    (void)module_path;

    if (out_info) {
        out_info->is_debug_stub = false;
        out_info->debug_dylib_path[0] = '\0';
        out_info->debug_dylib_base = 0;
    }
    return out_info != nullptr;
}

bool ada_find_loaded_debug_dylib(DebugDylibInfo* info) {
    (void)info;
    return false;
}

} // extern "C"

#endif // __APPLE__
