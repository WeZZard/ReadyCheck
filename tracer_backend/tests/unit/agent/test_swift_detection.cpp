// Unit tests for Swift detection functions

#include <gtest/gtest.h>
#include <cstdlib>

extern "C" {
#include <tracer_backend/agent/swift_detection.h>
}

// =============================================================================
// Test: ada_is_swift_symbol_name
// =============================================================================

TEST(swift_symbol_name__dollar_s_prefix__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_swift_symbol_name("$sSomeSwiftSymbol"));
}

TEST(swift_symbol_name__dollar_S_prefix__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_swift_symbol_name("$SSomeSwiftSymbol"));
}

TEST(swift_symbol_name__underscore_dollar_s__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_swift_symbol_name("_$sSomeSwiftSymbol"));
}

TEST(swift_symbol_name__underscore_dollar_S__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_swift_symbol_name("_$SSomeSwiftSymbol"));
}

TEST(swift_symbol_name__swift_underscore_prefix__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_swift_symbol_name("swift_allocObject"));
}

TEST(swift_symbol_name__underscore_swift_prefix__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_swift_symbol_name("_swift_release"));
}

TEST(swift_symbol_name__double_underscore_swift__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_swift_symbol_name("__swift5_proto"));
}

TEST(swift_symbol_name__c_symbol__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_swift_symbol_name("malloc"));
    EXPECT_FALSE(ada_is_swift_symbol_name("free"));
    EXPECT_FALSE(ada_is_swift_symbol_name("objc_msgSend"));
    EXPECT_FALSE(ada_is_swift_symbol_name("_main"));
}

TEST(swift_symbol_name__null__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_swift_symbol_name(nullptr));
}

TEST(swift_symbol_name__empty__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_swift_symbol_name(""));
}

// =============================================================================
// Test: ada_is_stub_section_id
// =============================================================================

TEST(stub_section__stub_helper__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_stub_section_id("__stub_helper"));
    EXPECT_TRUE(ada_is_stub_section_id("0.__stub_helper"));
}

TEST(stub_section__auth_stubs__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_stub_section_id("__auth_stubs"));
    EXPECT_TRUE(ada_is_stub_section_id("0.__auth_stubs"));
}

TEST(stub_section__stubs__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_stub_section_id("__stubs"));
    EXPECT_TRUE(ada_is_stub_section_id("0.__stubs"));
}

TEST(stub_section__text__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_stub_section_id("__text"));
    EXPECT_FALSE(ada_is_stub_section_id("0.__text"));
}

TEST(stub_section__null__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_stub_section_id(nullptr));
}

TEST(stub_section__empty__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_stub_section_id(""));
}

// =============================================================================
// Test: ada_is_stub_section_name
// =============================================================================

TEST(stub_section_name__stub_helper__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_stub_section_name("__stub_helper"));
}

TEST(stub_section_name__auth_stubs__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_stub_section_name("__auth_stubs"));
}

TEST(stub_section_name__stubs__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_stub_section_name("__stubs"));
}

TEST(stub_section_name__text__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_stub_section_name("__text"));
}

// =============================================================================
// Test: ada_is_swift_section_name
// =============================================================================

TEST(swift_section_name__swift5_proto__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_swift_section_name("__swift5_proto"));
}

TEST(swift_section_name__swift5_types__then_returns_true, unit) {
    EXPECT_TRUE(ada_is_swift_section_name("__swift5_types"));
}

TEST(swift_section_name__text__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_swift_section_name("__text"));
}

TEST(swift_section_name__null__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_swift_section_name(nullptr));
}

// =============================================================================
// Test: ada_should_skip_swift_symbols
// =============================================================================

TEST(skip_swift__env_unset__then_returns_true, unit) {
    unsetenv("ADA_HOOK_SWIFT");
#ifdef __APPLE__
    EXPECT_TRUE(ada_should_skip_swift_symbols());
#else
    // On non-macOS, Swift symbols are not skipped by default
    EXPECT_FALSE(ada_should_skip_swift_symbols());
#endif
}

TEST(skip_swift__env_zero__then_returns_true, unit) {
    setenv("ADA_HOOK_SWIFT", "0", 1);
#ifdef __APPLE__
    EXPECT_TRUE(ada_should_skip_swift_symbols());
#else
    EXPECT_FALSE(ada_should_skip_swift_symbols());
#endif
    unsetenv("ADA_HOOK_SWIFT");
}

TEST(skip_swift__env_one__then_returns_false, unit) {
    setenv("ADA_HOOK_SWIFT", "1", 1);
    // When ADA_HOOK_SWIFT=1, Swift symbols should NOT be skipped
    EXPECT_FALSE(ada_should_skip_swift_symbols());
    unsetenv("ADA_HOOK_SWIFT");
}

TEST(skip_swift__env_empty__then_returns_true, unit) {
    setenv("ADA_HOOK_SWIFT", "", 1);
#ifdef __APPLE__
    EXPECT_TRUE(ada_should_skip_swift_symbols());
#else
    EXPECT_FALSE(ada_should_skip_swift_symbols());
#endif
    unsetenv("ADA_HOOK_SWIFT");
}

// =============================================================================
// Test: ada_is_swift_symbolic_metadata
// =============================================================================

TEST(symbolic_metadata__underscore_symbolic_prefix__then_returns_true, unit) {
    // Real example from Swift debug builds
    EXPECT_TRUE(ada_is_swift_symbolic_metadata("_symbolic ___SSC9CGContextC"));
    EXPECT_TRUE(ada_is_swift_symbolic_metadata("_symbolic"));
    EXPECT_TRUE(ada_is_swift_symbolic_metadata("_symbolic___"));
}

TEST(symbolic_metadata__symbolic_prefix__then_returns_true, unit) {
    // Without leading underscore (also valid)
    EXPECT_TRUE(ada_is_swift_symbolic_metadata("symbolic ___SSC9CGContextC"));
    EXPECT_TRUE(ada_is_swift_symbolic_metadata("symbolic"));
}

TEST(symbolic_metadata__regular_swift_symbol__then_returns_false, unit) {
    // Regular Swift symbols should NOT be flagged as symbolic metadata
    EXPECT_FALSE(ada_is_swift_symbolic_metadata("$sSomeFunction"));
    EXPECT_FALSE(ada_is_swift_symbolic_metadata("_$sSomeFunction"));
    EXPECT_FALSE(ada_is_swift_symbolic_metadata("swift_allocObject"));
}

TEST(symbolic_metadata__regular_c_symbol__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_swift_symbolic_metadata("malloc"));
    EXPECT_FALSE(ada_is_swift_symbolic_metadata("_main"));
    EXPECT_FALSE(ada_is_swift_symbolic_metadata("objc_msgSend"));
}

TEST(symbolic_metadata__null__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_swift_symbolic_metadata(nullptr));
}

TEST(symbolic_metadata__empty__then_returns_false, unit) {
    EXPECT_FALSE(ada_is_swift_symbolic_metadata(""));
}
