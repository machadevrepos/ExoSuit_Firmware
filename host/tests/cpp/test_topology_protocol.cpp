#include <cstdint>
#include <cstring>
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
    EXPECT_TRUE(!exo::hub_owns_node(exo::HubId::Main, 0U));
    EXPECT_TRUE(!exo::hub_owns_node(exo::HubId::Main, 7U));
    EXPECT_TRUE(!exo::hub_owns_node(exo::HubId::Main, 12U));
    EXPECT_TRUE(!exo::hub_owns_node(exo::HubId::Lower, 6U));
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

void test_versioned_control_and_topology_payloads()
{
    static_assert(BLEPIPE_MSG_TOPOLOGY_V2 != BLEPIPE_MSG_TOPOLOGY,
                  "Topology V2 must not reuse the legacy message id");
    static_assert(BLEPIPE_MSG_STREAM_CONTROL_V2 != BLEPIPE_MSG_STREAM_CONTROL,
                  "Stream-control V2 must not reuse the legacy message id");
    static_assert(sizeof(blepipe_topology_v2_t) == 8U,
                  "Topology V2 wire format must remain compact");
    static_assert(sizeof(blepipe_stream_control_v2_t) == 5U,
                  "Stream-control V2 wire format must remain compact");

    const blepipe_topology_v2_t topology = {
        BLEPIPE_TOPOLOGY_PROTO_VER, 2U, 0x1003U, 0x007EU, 0x1000U
    };
    const uint8_t topology_expected[] = {
        0x02U, 0x02U, 0x03U, 0x10U, 0x7EU, 0x00U, 0x00U, 0x10U
    };
    EXPECT_TRUE(std::memcmp(&topology, topology_expected, sizeof(topology_expected)) == 0);

    const blepipe_stream_control_v2_t control = {
        BLEPIPE_STREAM_CONTROL_PROTO_VER, 0xA2U, 0x1002U, 40U
    };
    const uint8_t control_expected[] = { 0x02U, 0xA2U, 0x02U, 0x10U, 0x28U };
    EXPECT_TRUE(std::memcmp(&control, control_expected, sizeof(control_expected)) == 0);

    uint8_t encoded_topology[BLEPIPE_TOPOLOGY_V2_PAYLOAD_LEN] = { 0U };
    size_t encoded_len = 0U;
    EXPECT_TRUE(blepipe_topology_v2_encode(encoded_topology, sizeof(encoded_topology),
                                           &topology, &encoded_len) == BLEPIPE_STATUS_OK);
    EXPECT_TRUE(encoded_len == BLEPIPE_TOPOLOGY_V2_PAYLOAD_LEN);
    EXPECT_TRUE(std::memcmp(encoded_topology, topology_expected, sizeof(topology_expected)) == 0);

    blepipe_topology_v2_t decoded_topology = {};
    EXPECT_TRUE(blepipe_topology_v2_decode(encoded_topology, encoded_len,
                                           &decoded_topology) == BLEPIPE_STATUS_OK);
    EXPECT_TRUE(decoded_topology.protocol_version == topology.protocol_version);
    EXPECT_TRUE(decoded_topology.hub_id == topology.hub_id);
    EXPECT_TRUE(decoded_topology.present_source_mask == topology.present_source_mask);
    EXPECT_TRUE(decoded_topology.owned_source_mask == topology.owned_source_mask);
    EXPECT_TRUE(decoded_topology.fault_source_mask == topology.fault_source_mask);
    EXPECT_TRUE(blepipe_topology_v2_decode(encoded_topology, encoded_len - 1U,
                                           &decoded_topology) == BLEPIPE_STATUS_BAD_LENGTH);

    uint8_t encoded_control[BLEPIPE_STREAM_CONTROL_V2_PAYLOAD_LEN] = { 0U };
    encoded_len = 0U;
    EXPECT_TRUE(blepipe_stream_control_v2_encode(encoded_control, sizeof(encoded_control),
                                                 &control, &encoded_len) == BLEPIPE_STATUS_OK);
    EXPECT_TRUE(encoded_len == BLEPIPE_STREAM_CONTROL_V2_PAYLOAD_LEN);
    EXPECT_TRUE(std::memcmp(encoded_control, control_expected, sizeof(control_expected)) == 0);

    blepipe_stream_control_v2_t decoded_control = {};
    EXPECT_TRUE(blepipe_stream_control_v2_decode(encoded_control, encoded_len,
                                                 &decoded_control) == BLEPIPE_STATUS_OK);
    EXPECT_TRUE(decoded_control.protocol_version == control.protocol_version);
    EXPECT_TRUE(decoded_control.command == control.command);
    EXPECT_TRUE(decoded_control.target_source_mask == control.target_source_mask);
    EXPECT_TRUE(decoded_control.stream_interval_ms == control.stream_interval_ms);
    EXPECT_TRUE(blepipe_stream_control_v2_decode(encoded_control, encoded_len - 1U,
                                                 &decoded_control) == BLEPIPE_STATUS_BAD_LENGTH);
}

}  // namespace

int main()
{
    test_node_ranges_and_ownership();
    test_source_bits_cover_master_and_all_nodes();
    test_checked_blepipe_leaf_addresses();
    test_versioned_control_and_topology_payloads();

    if (failures != 0) {
        std::cerr << failures << " topology/protocol check(s) failed\n";
        return 1;
    }
    std::cout << "topology/protocol checks passed\n";
    return 0;
}
