// Unit tests for DSO management registry

#include <gtest/gtest.h>
#include <tracer_backend/agent/dso_management.h>

using ada::agent::dso_on_load;
using ada::agent::dso_on_unload;
using ada::agent::dso_registry;
using ada::agent::DsoInfo;

TEST(dso_management__add_and_list__then_visible, unit) {
    auto& reg = dso_registry();
    reg.clear();
    dso_on_load("/usr/lib/libfoo.dylib", (void*)0x1111, 0xAAA0);
    dso_on_load("/usr/lib/libbar.dylib", (void*)0x2222, 0xBBB0);

    auto all = reg.list();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_TRUE(all[0].path.find("libfoo") != std::string::npos || all[1].path.find("libfoo") != std::string::npos);
}

TEST(dso_management__remove_by_handle__then_removed, unit) {
    auto& reg = dso_registry();
    reg.clear();
    void* h1 = (void*)0x1010;
    void* h2 = (void*)0x2020;
    dso_on_load("/tmp/liba.so", h1, 0x1000);
    dso_on_load("/tmp/libb.so", h2, 0x2000);

    auto all = reg.list();
    ASSERT_EQ(all.size(), 2u);

    dso_on_unload(h1, 0);
    all = reg.list();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_TRUE(all[0].path.find("libb") != std::string::npos);
}

TEST(dso_management__remove_by_base__then_removed, unit) {
    auto& reg = dso_registry();
    reg.clear();
    dso_on_load("/tmp/libc.so", nullptr, 0x3000);
    dso_on_load("/tmp/libd.so", nullptr, 0x4000);
    ASSERT_EQ(reg.list().size(), 2u);

    dso_on_unload(nullptr, 0x3000);
    auto all = reg.list();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_TRUE(all[0].path.find("libd") != std::string::npos);
}

