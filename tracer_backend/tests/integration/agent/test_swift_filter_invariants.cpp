// Integration test: verify ada_is_swift_compiler_stub() invariants against real Swift binaries.
// Uses `nm -j` to enumerate symbols from fixture binaries, then asserts:
//   - No compiler-generated stub survives the filter (negative invariants)
//   - User code and protocol witness thunks are never filtered (positive invariants)

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <tracer_backend/agent/swift_detection.h>
#include "ada_paths.h"
}

// ─── Helpers ───────────────────────────────────────────────────────────────────

// Run `nm -j <path>` and return all non-empty symbol names.
static std::vector<std::string> enumerate_symbols(const std::string& binary_path) {
    std::vector<std::string> symbols;
    std::string cmd = "nm -j '" + binary_path + "' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return symbols;

    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        size_t len = strlen(buf);
        // Strip trailing newline
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
            buf[--len] = '\0';
        }
        if (len > 0) {
            symbols.emplace_back(buf);
        }
    }
    pclose(pipe);
    return symbols;
}

// Check if a symbol name is a Swift-mangled name ($s or _$s prefix).
static bool is_swift_mangled(const char* name) {
    if (strncmp(name, "_$s", 3) == 0) return true;
    if (strncmp(name, "$s", 2) == 0) return true;
    return false;
}

// Check if a symbol ends with a given suffix.
static bool ends_with(const std::string& s, const char* suffix) {
    size_t slen = s.size();
    size_t sufflen = strlen(suffix);
    if (slen < sufflen) return false;
    return s.compare(slen - sufflen, sufflen, suffix) == 0;
}

// Check if a symbol starts with a given prefix.
static bool starts_with(const std::string& s, const char* prefix) {
    return strncmp(s.c_str(), prefix, strlen(prefix)) == 0;
}

// ─── Fixture ───────────────────────────────────────────────────────────────────

static const char* swift_fixture_path() {
    static std::string path =
        std::string(ADA_WORKSPACE_ROOT) + "/target/" + ADA_BUILD_PROFILE +
        "/tracer_backend/test/test_swift_simple";
    return path.c_str();
}

class SwiftFilterInvariantsTest : public ::testing::Test {
protected:
    std::vector<std::string> all_symbols_;
    std::vector<std::string> survivors_;  // symbols NOT filtered by ada_is_swift_compiler_stub

    void SetUp() override {
        all_symbols_ = enumerate_symbols(swift_fixture_path());
        if (all_symbols_.empty()) {
            GTEST_SKIP() << "No symbols found in " << swift_fixture_path()
                         << " (fixture may not be built)";
        }
        for (const auto& sym : all_symbols_) {
            if (!ada_is_swift_compiler_stub(sym.c_str())) {
                survivors_.push_back(sym);
            }
        }
        printf("[Filter] %zu total symbols, %zu survivors, %zu filtered\n",
               all_symbols_.size(), survivors_.size(),
               all_symbols_.size() - survivors_.size());
    }
};

// ─── Negative invariants: these must NOT survive ────────────────────────────

TEST_F(SwiftFilterInvariantsTest,
       no_swift_runtime_helpers_survive) {
    for (const auto& sym : survivors_) {
        EXPECT_FALSE(starts_with(sym, "__swift_"))
            << "Runtime helper survived filter: " << sym;
        EXPECT_FALSE(starts_with(sym, "___swift_"))
            << "Runtime helper survived filter: " << sym;
    }
}

TEST_F(SwiftFilterInvariantsTest,
       no_objectdestroy_stubs_survive) {
    for (const auto& sym : survivors_) {
        EXPECT_FALSE(starts_with(sym, "objectdestroy"))
            << "objectdestroy stub survived filter: " << sym;
        EXPECT_FALSE(starts_with(sym, "_objectdestroy"))
            << "_objectdestroy stub survived filter: " << sym;
    }
}

TEST_F(SwiftFilterInvariantsTest,
       no_block_abi_helpers_survive) {
    for (const auto& sym : survivors_) {
        EXPECT_FALSE(starts_with(sym, "block_copy_helper"))
            << "block_copy_helper survived filter: " << sym;
        EXPECT_FALSE(starts_with(sym, "block_destroy_helper"))
            << "block_destroy_helper survived filter: " << sym;
    }
}

TEST_F(SwiftFilterInvariantsTest,
       no_metadata_accessors_survive) {
    for (const auto& sym : survivors_) {
        if (!is_swift_mangled(sym.c_str())) continue;
        EXPECT_FALSE(ends_with(sym, "Ma"))
            << "Metadata accessor (Ma) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "Tm"))
            << "Type metadata accessor (Tm) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "Mi"))
            << "Metaclass init (Mi) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "Mr"))
            << "Type metadata completion (Mr) survived filter: " << sym;
    }
}

TEST_F(SwiftFilterInvariantsTest,
       no_witness_table_internals_survive) {
    for (const auto& sym : survivors_) {
        if (!is_swift_mangled(sym.c_str())) continue;
        EXPECT_FALSE(ends_with(sym, "Wl"))
            << "Witness table lazy accessor (Wl) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "WL"))
            << "Witness table lazy accessor (WL) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "wcp"))
            << "Witness table copy (wcp) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "wca"))
            << "Witness table accessor (wca) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "wct"))
            << "Witness table template (wct) survived filter: " << sym;
    }
}

TEST_F(SwiftFilterInvariantsTest,
       no_back_deploy_thunks_survive) {
    for (const auto& sym : survivors_) {
        if (!is_swift_mangled(sym.c_str())) continue;
        EXPECT_FALSE(ends_with(sym, "Wb"))
            << "Back-deploy thunk (Wb) survived filter: " << sym;
    }
}

TEST_F(SwiftFilterInvariantsTest,
       no_outlined_operations_survive) {
    for (const auto& sym : survivors_) {
        if (!is_swift_mangled(sym.c_str())) continue;
        EXPECT_FALSE(ends_with(sym, "Oe"))
            << "Outlined retain (Oe) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "Oh"))
            << "Outlined release (Oh) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "Ob"))
            << "Outlined copy (Ob) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "Oc"))
            << "Outlined consume (Oc) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "Od"))
            << "Outlined destroy (Od) survived filter: " << sym;
        EXPECT_FALSE(ends_with(sym, "Oy"))
            << "Outlined init with copy (Oy) survived filter: " << sym;
    }
}

// ─── Positive invariants: these MUST survive ────────────────────────────────

TEST_F(SwiftFilterInvariantsTest,
       protocol_witness_thunks_TW_survive) {
    // Collect all TW symbols from the full list
    size_t tw_total = 0;
    size_t tw_survived = 0;
    for (const auto& sym : all_symbols_) {
        if (is_swift_mangled(sym.c_str()) && ends_with(sym, "TW")) {
            tw_total++;
            if (!ada_is_swift_compiler_stub(sym.c_str())) {
                tw_survived++;
            } else {
                ADD_FAILURE() << "Protocol witness thunk was incorrectly filtered: " << sym;
            }
        }
    }
    printf("[Filter] TW symbols: %zu total, %zu survived\n", tw_total, tw_survived);
    EXPECT_EQ(tw_total, tw_survived)
        << "Some protocol witness thunks (TW) were incorrectly filtered";
}

TEST_F(SwiftFilterInvariantsTest,
       non_swift_symbols_survive) {
    // All non-Swift symbols must pass through unfiltered.
    // Exclude prefix patterns that ada_is_swift_compiler_stub() intentionally catches
    // beyond what ada_is_swift_symbol_name() detects.
    for (const auto& sym : all_symbols_) {
        if (!ada_is_swift_symbol_name(sym.c_str()) &&
            !starts_with(sym, "___swift_") &&
            !starts_with(sym, "__swift_") &&
            !starts_with(sym, "objectdestroy") &&
            !starts_with(sym, "_objectdestroy") &&
            !starts_with(sym, "block_copy_helper") &&
            !starts_with(sym, "block_destroy_helper")) {
            EXPECT_FALSE(ada_is_swift_compiler_stub(sym.c_str()))
                << "Non-Swift symbol was incorrectly filtered: " << sym;
        }
    }
}

// ─── Sanity check: filter actually does something ───────────────────────────

TEST_F(SwiftFilterInvariantsTest,
       filter_removes_at_least_some_symbols) {
    size_t filtered = all_symbols_.size() - survivors_.size();
    printf("[Filter] Filtered %zu of %zu symbols (%.1f%%)\n",
           filtered, all_symbols_.size(),
           100.0 * filtered / all_symbols_.size());
    // If the fixture has Swift symbols, the filter should remove at least some
    bool has_swift = false;
    for (const auto& sym : all_symbols_) {
        if (is_swift_mangled(sym.c_str())) { has_swift = true; break; }
    }
    if (has_swift) {
        EXPECT_GT(filtered, 0u)
            << "Filter removed zero symbols from a binary with Swift symbols";
    }
}
