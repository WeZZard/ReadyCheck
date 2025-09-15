// M1_E1_I10: Validate IndexEvent ABI layout and size

#include <gtest/gtest.h>

extern "C" {
#include <tracer_backend/utils/tracer_types.h>
}

// Use a distinct suite name prefix for filtering
TEST(m1_e1_i10__index_event_layout__then_expected_size, unit) {
    // IndexEvent must remain compact (32 bytes) for ring buffer efficiency
    EXPECT_EQ(sizeof(IndexEvent), 32u);

    // Basic field sanity: assign and read back
    IndexEvent e{};
    e.timestamp = 0xAABBCCDDEEFF0011ull;
    e.function_id = 0x1122334455667788ull;
    e.thread_id = 0xCAFEBABEu;
    e.event_kind = EVENT_KIND_CALL;
    e.call_depth = 7u;

    EXPECT_EQ(e.timestamp, 0xAABBCCDDEEFF0011ull);
    EXPECT_EQ(e.function_id, 0x1122334455667788ull);
    EXPECT_EQ(e.thread_id, 0xCAFEBABEu);
    EXPECT_EQ(e.event_kind, (uint32_t)EVENT_KIND_CALL);
    EXPECT_EQ(e.call_depth, 7u);
}

