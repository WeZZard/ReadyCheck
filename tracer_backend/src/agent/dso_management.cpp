// Implementation of lightweight DSO registry and callbacks.

#include <tracer_backend/agent/dso_management.h>

#include <algorithm>
#include <mutex>

namespace ada {
namespace agent {

// -----------------------------
// DsoRegistry
// -----------------------------

DsoRegistry::DsoRegistry() : dsos_() {}

void DsoRegistry::add(const std::string& path, uintptr_t base, void* handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Try match by handle first (if provided), else by base.
    auto it = dsos_.end();
    if (handle != nullptr) {
        it = std::find_if(dsos_.begin(), dsos_.end(), [&](const DsoInfo& d){ return d.handle == handle; });
    }
    if (it == dsos_.end() && base != 0) {
        it = std::find_if(dsos_.begin(), dsos_.end(), [&](const DsoInfo& d){ return d.base == base; });
    }
    if (it != dsos_.end()) {
        it->path = path;
        if (base != 0) it->base = base;
        if (handle != nullptr) it->handle = handle;
    } else {
        dsos_.push_back(DsoInfo{path, base, handle});
    }
}

bool DsoRegistry::remove_by_handle(void* handle) {
    if (!handle) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto old_size = dsos_.size();
    dsos_.erase(std::remove_if(dsos_.begin(), dsos_.end(), [&](const DsoInfo& d){ return d.handle == handle; }), dsos_.end());
    return dsos_.size() != old_size;
}

bool DsoRegistry::remove_by_base(uintptr_t base) {
    if (!base) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto old_size = dsos_.size();
    dsos_.erase(std::remove_if(dsos_.begin(), dsos_.end(), [&](const DsoInfo& d){ return d.base == base; }), dsos_.end());
    return dsos_.size() != old_size;
}

std::vector<DsoInfo> DsoRegistry::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dsos_;
}

bool DsoRegistry::find_by_handle(void* handle, DsoInfo* out) const {
    if (!handle) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(dsos_.begin(), dsos_.end(), [&](const DsoInfo& d){ return d.handle == handle; });
    if (it == dsos_.end()) return false;
    if (out) *out = *it;
    return true;
}

bool DsoRegistry::find_by_base(uintptr_t base, DsoInfo* out) const {
    if (!base) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(dsos_.begin(), dsos_.end(), [&](const DsoInfo& d){ return d.base == base; });
    if (it == dsos_.end()) return false;
    if (out) *out = *it;
    return true;
}

void DsoRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    dsos_.clear();
}

// -----------------------------
// Global wiring
// -----------------------------

static DsoRegistry g_registry;

DsoRegistry& dso_registry() { return g_registry; }

void dso_on_load(const char* path, void* handle, uintptr_t base) {
    g_registry.add(path ? std::string(path) : std::string(""), base, handle);
}

void dso_on_unload(void* handle, uintptr_t base) {
    // prefer handle, fall back to base
    if (handle) {
        if (g_registry.remove_by_handle(handle)) return;
    }
    if (base) {
        (void)g_registry.remove_by_base(base);
    }
}

} // namespace agent
} // namespace ada

