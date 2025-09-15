// Unit tests for HookRegistry (module/symbol -> function id)

#include <gtest/gtest.h>
#include <tracer_backend/agent/hook_registry.h>

using ada::agent::HookRegistry;
using ada::agent::make_function_id;
using ada::agent::fnv1a32_ci;

TEST(hook_registry__register_symbol__then_stable_id, unit) {
    HookRegistry reg;
    uint64_t id1 = reg.register_symbol("/usr/lib/libalpha.dylib", "foo");
    uint64_t id2 = reg.register_symbol("/usr/lib/libalpha.dylib", "foo");
    ASSERT_EQ(id1, id2);

    uint64_t id3 = reg.register_symbol("/usr/lib/libalpha.dylib", "bar");
    ASSERT_NE(id3, id1);

    // Upper 32 bits must equal module id; lower must be 1 for foo, 2 for bar
    uint32_t mod = reg.get_module_id("/usr/lib/libalpha.dylib");
    ASSERT_EQ((id1 >> 32), static_cast<uint64_t>(mod));
    ASSERT_EQ((id1 & 0xffffffffULL), 1ULL);
    ASSERT_EQ((id3 & 0xffffffffULL), 2ULL);
}

TEST(hook_registry__different_modules__then_different_module_ids, unit) {
    HookRegistry reg;
    uint64_t a1 = reg.register_symbol("/usr/lib/liba.so", "sym");
    uint64_t b1 = reg.register_symbol("/usr/lib/libb.so", "sym");
    ASSERT_NE((a1 >> 32), (b1 >> 32));
}

