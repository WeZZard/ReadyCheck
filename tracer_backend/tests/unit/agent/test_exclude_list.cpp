// Unit tests for exclude list (hash-based)

#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/agent/exclude_list.h>
}

TEST(exclude_list__create_and_destroy__then_ok, unit) {
    AdaExcludeList* xs = ada_exclude_create(0);
    ASSERT_NE(xs, nullptr);
    ada_exclude_destroy(xs);
}

TEST(exclude_list__defaults__then_contains_hotspots, unit) {
    AdaExcludeList* xs = ada_exclude_create(16);
    ASSERT_NE(xs, nullptr);
    ada_exclude_add_defaults(xs);
    EXPECT_TRUE(ada_exclude_contains(xs, "malloc"));
    EXPECT_TRUE(ada_exclude_contains(xs, "free"));
    EXPECT_TRUE(ada_exclude_contains(xs, "objc_msgSend"));
    // Case-insensitive
    EXPECT_TRUE(ada_exclude_contains(xs, "MeMcPy"));
    ada_exclude_destroy(xs);
}

TEST(exclude_list__csv_add__then_contains_all_entries, unit) {
    AdaExcludeList* xs = ada_exclude_create(8);
    ASSERT_NE(xs, nullptr);
    ada_exclude_add_from_csv(xs, " Foo ,Bar; baz,  qux ");
    EXPECT_TRUE(ada_exclude_contains(xs, "foo"));
    EXPECT_TRUE(ada_exclude_contains(xs, "BAR"));
    EXPECT_TRUE(ada_exclude_contains(xs, "Baz"));
    EXPECT_TRUE(ada_exclude_contains(xs, "qux"));
    // Unknown
    EXPECT_FALSE(ada_exclude_contains(xs, "quux"));
    ada_exclude_destroy(xs);
}

TEST(exclude_list__hash_and_contains_hash__then_roundtrip, unit) {
    AdaExcludeList* xs = ada_exclude_create(4);
    ASSERT_NE(xs, nullptr);
    const char* name = "customSymbol";
    uint64_t h = ada_exclude_hash(name);
    EXPECT_FALSE(ada_exclude_contains_hash(xs, h));
    EXPECT_TRUE(ada_exclude_add(xs, name));
    EXPECT_TRUE(ada_exclude_contains_hash(xs, h));
    ada_exclude_destroy(xs);
}

