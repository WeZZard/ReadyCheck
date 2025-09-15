// Unit tests for comprehensive hook planning utilities

#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/agent/exclude_list.h>
}

#include <tracer_backend/agent/hook_registry.h>
#include <tracer_backend/agent/comprehensive_hooks.h>

using ada::agent::HookRegistry;
using ada::agent::HookPlanEntry;
using ada::agent::plan_module_hooks;
using ada::agent::plan_comprehensive_hooks;

TEST(comprehensive_hooks__plan_module_hooks__then_excludes_filtered, unit) {
    AdaExcludeList* xs = ada_exclude_create(8);
    ASSERT_NE(xs, nullptr);
    ada_exclude_add(xs, "memcpy");
    ada_exclude_add(xs, "malloc");

    HookRegistry reg;
    std::vector<std::string> exports = {"memcpy", "foo", "malloc", "bar"};
    auto plan = plan_module_hooks("/usr/bin/main", exports, xs, reg);

    // memcpy/malloc are filtered out; foo/bar remain
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0].symbol, "foo");
    EXPECT_EQ(plan[1].symbol, "bar");

    // Function ids must be stable and increasing for module
    EXPECT_NE(plan[0].function_id, plan[1].function_id);

    ada_exclude_destroy(xs);
}

TEST(comprehensive_hooks__plan_comprehensive__then_combines_modules, unit) {
    AdaExcludeList* xs = ada_exclude_create(8);
    ASSERT_NE(xs, nullptr);
    // No excludes here

    HookRegistry reg;
    std::vector<std::string> main_exports = {"alpha", "beta"};
    std::vector<std::string> dso_names = {"/usr/lib/libx.so", "/usr/lib/liby.so"};
    std::vector<std::vector<std::string>> dso_exports = {{"f1", "f2"}, {"g1"}};

    auto all = plan_comprehensive_hooks(main_exports, dso_names, dso_exports, xs, reg);
    ASSERT_EQ(all.size(), 5u);

    // Check that ids share module-id upper bits for the same module
    uint64_t main_id0 = all[0].function_id;
    uint64_t main_id1 = all[1].function_id;
    EXPECT_EQ(main_id0 >> 32, main_id1 >> 32);

    // DSO 0 exports
    uint64_t d0_0 = all[2].function_id;
    uint64_t d0_1 = all[3].function_id;
    EXPECT_EQ(d0_0 >> 32, d0_1 >> 32);
    EXPECT_NE(d0_0 >> 32, main_id0 >> 32);

    // DSO 1 export must have different module id
    EXPECT_NE(all[4].function_id >> 32, d0_0 >> 32);

    ada_exclude_destroy(xs);
}

