#include <tracer_backend/agent/hook_registry.h>

namespace ada {
namespace agent {

// FNV-1a 32-bit (case-insensitive ASCII)
uint32_t fnv1a32_ci(const std::string& s) {
    const uint32_t FNV_OFFSET = 2166136261u;
    const uint32_t FNV_PRIME  = 16777619u;
    uint32_t h = FNV_OFFSET;
    for (unsigned char c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + 32);
        h ^= static_cast<uint32_t>(c);
        h *= FNV_PRIME;
    }
    // avoid 0 as module id to keep debugging simpler
    if (h == 0) h = 0x9e3779b9u;
    return h;
}

HookRegistry::HookRegistry() : modules_() {}

uint64_t HookRegistry::register_symbol(const std::string& module_path, const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& me = modules_[module_path];
    if (me.module_id == 0) {
        me.module_id = fnv1a32_ci(module_path);
        me.next_index = 1u;
    }
    return register_symbol_locked(me, symbol);
}

uint64_t HookRegistry::register_symbol_locked(ModuleEntry& me, const std::string& symbol) {
    auto it = me.name_to_index.find(symbol);
    if (it != me.name_to_index.end()) {
        return make_function_id(me.module_id, it->second);
    }
    uint32_t idx = me.next_index++;
    me.name_to_index.emplace(symbol, idx);
    return make_function_id(me.module_id, idx);
}

bool HookRegistry::get_id(const std::string& module_path, const std::string& symbol, uint64_t* out_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = modules_.find(module_path);
    if (it == modules_.end()) return false;
    const auto& me = it->second;
    auto it2 = me.name_to_index.find(symbol);
    if (it2 == me.name_to_index.end()) return false;
    if (out_id) *out_id = make_function_id(me.module_id, it2->second);
    return true;
}

uint32_t HookRegistry::get_module_id(const std::string& module_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = modules_.find(module_path);
    if (it == modules_.end()) return 0u;
    return it->second.module_id;
}

uint32_t HookRegistry::get_symbol_count(const std::string& module_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = modules_.find(module_path);
    if (it == modules_.end()) return 0u;
    return static_cast<uint32_t>(it->second.name_to_index.size());
}

void HookRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    modules_.clear();
}

uint32_t HookRegistry::get_or_create_module_id_locked(const std::string& module_path) {
    auto& me = modules_[module_path];
    if (me.module_id == 0) {
        me.module_id = fnv1a32_ci(module_path);
        me.next_index = 1u;
    }
    return me.module_id;
}

} // namespace agent
} // namespace ada

