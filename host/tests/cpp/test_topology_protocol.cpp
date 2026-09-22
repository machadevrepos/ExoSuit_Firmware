#include <cstdint>
#include <iostream>

#include <exo/protocol/blepipe_proto.h>
#include <exo/types/topology.h>

namespace {

int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": expected " #expr "\n"; \
        ++failures; \
    } \
} while (0)

void test_node_ranges_and_ownership()
{
    EXPECT_TRUE(exo::is_valid_node_id(1U));
    EXPECT_TRUE(exo::is_valid_node_id(12U));
    EXPECT_TRUE(!exo::is_valid_node_id(0U));
    EXPECT_TRUE(!exo::is_valid_node_id(13U));

    EXPECT_TRUE(exo::hub_for_node(1U) == exo::HubId::Main);
    EXPECT_TRUE(exo::hub_for_node(6U) == exo::HubId::Main);
    EXPECT_TRUE(exo::hub_for_node(7U) == exo::HubId::Lower);
    EXPECT_TRUE(exo::hub_for_node(12U) == exo::HubId::Lower);
    EXPECT_TRUE(exo::hub_owns_node(exo::HubId::Main, 6U));
    EXPECT_TRUE(!exo::hub_owns_node(exo::HubId::Main, 7U));
    EXPECT_TRUE(exo::hub_owns_node(exo::HubId::Lower, 12U));
    EXPECT_TRUE(!exo::hub_owns_node(exo::HubId::Lower, 13U));
}

void test_source_bits_cover_master_and_all_nodes()
{
    EXPECT_TRUE(exo::source_bit(0U) == 0x0001U);
    EXPECT_TRUE(exo::source_bit(1U) == 0x0002U);
    EXPECT_TRUE(exo::source_bit(12U) == 0x1000U);
    EXPECT_TRUE(exo::source_bit(13U) == 0U);
}

void test_checked_blepipe_leaf_addresses()
{
    static_assert(BLEPIPE_ID_HUB2 == 0x0002U, "U11 must have a stable BLEPipe id");

    uint16_t address = 0U;
    EXPECT_TRUE(blepipe_leaf_id_from_node(1U, &address));
    EXPECT_TRUE(address == 0x0101U);
    EXPECT_TRUE(blepipe_leaf_id_from_node(12U, &address));
    EXPECT_TRUE(address == 0x010CU);

    address = 0xBEEFU;
    EXPECT_TRUE(!blepipe_leaf_id_from_node(0U, &address));
    EXPECT_TRUE(address == 0xBEEFU);
    EXPECT_TRUE(!blepipe_leaf_id_from_node(13U, &address));
    EXPECT_TRUE(address == 0xBEEFU);
    EXPECT_TRUE(!blepipe_leaf_id_from_node(1U, nullptr));
}

}  // namespace

int main()
{
    test_node_ranges_and_ownership();
    test_source_bits_cover_master_and_all_nodes();
    test_checked_blepipe_leaf_addresses();

    if (failures != 0) {
        std::cerr << failures << " topology/protocol check(s) failed\n";
        return 1;
    }
    std::cout << "topology/protocol checks passed\n";
    return 0;
}
