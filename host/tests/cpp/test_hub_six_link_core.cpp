#include <cassert>
#include <cstdint>
#include <iostream>

#include <exo/ble/hub_leaf_ble_manager.h>

int main()
{
    using Manager = exo::ble_hub::HubLeafBleManagerCore<6U, 7U>;
    Manager manager;
    uint8_t payload = 0U;
    for (uint8_t node = 7U; node <= 12U; ++node) {
        payload = static_cast<uint8_t>(0x80U + node);
        assert(manager.push_leaf_sample(node, 1U, &payload, 1U,
                                        static_cast<uint32_t>(node)));
    }
    payload = 0xFFU;
    assert(!manager.push_leaf_sample(6U, 1U, &payload, 1U, 6U));
    assert(manager.pending_live_sample_count() == 6U);
    assert(manager.live_rx_for_node(12U) == 1U);
    assert(manager.live_rx_for_node(6U) == 0U);

    std::cout << "six-link hub core checks passed\n";
    return 0;
}
