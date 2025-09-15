#include <tracer_backend/agent/comprehensive_hooks.h>
#include <tracer_backend/agent/hook_registry.h>

extern "C" {
#include <tracer_backend/agent/exclude_list.h>
}

#include <algorithm>

namespace ada {
namespace agent {

static inline bool is_excluded(AdaExcludeList* xs, const std::string& name) {
    if (!xs) return false;
    return ada_exclude_contains(xs, name.c_str());
}

std::vector<HookPlanEntry> plan_module_hooks(
    const std::string& module_path,
    const std::vector<std::string>& exports,
    AdaExcludeList* excludes,
    HookRegistry& registry) {

    std::vector<HookPlanEntry> out;
    out.reserve(exports.size());
    for (const auto& sym : exports) {
        if (sym.empty()) continue;
        if (is_excluded(excludes, sym)) continue;
        uint64_t id = registry.register_symbol(module_path, sym);
        out.push_back(HookPlanEntry{sym, id});
    }
    return out;
}

std::vector<HookPlanEntry> plan_comprehensive_hooks(
    const std::vector<std::string>& main_exports,
    const std::vector<std::string>& dso_names,
    const std::vector<std::vector<std::string>>& dso_exports,
    AdaExcludeList* excludes,
    HookRegistry& registry) {

    std::vector<HookPlanEntry> all;
    // Main binary uses a special pseudo-path for stability
    auto main_plan = plan_module_hooks("<main>", main_exports, excludes, registry);
    all.insert(all.end(), main_plan.begin(), main_plan.end());

    const size_t n = dso_names.size();
    for (size_t i = 0; i < n; ++i) {
        const std::string& name = dso_names[i];
        const auto& exps = (i < dso_exports.size()) ? dso_exports[i] : std::vector<std::string>();
        auto plan = plan_module_hooks(name, exps, excludes, registry);
        all.insert(all.end(), plan.begin(), plan.end());
    }
    return all;
}

} // namespace agent
} // namespace ada

