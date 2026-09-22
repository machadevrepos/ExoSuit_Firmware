#ifndef BLE_SESSION_CONTROL_H_
#define BLE_SESSION_CONTROL_H_

#include <stdint.h>

#include <exo/types/topology.h>

namespace exo {

static constexpr uint8_t kSessionMasterSourceBit = 0x01U;
static constexpr uint8_t kSessionNodeMask = 0x1EU;
static constexpr SourceMask kSessionNodeSourceMask = 0x1FFEU;

constexpr bool session_node_mask_valid(uint8_t selected_node_mask)
{
    return selected_node_mask != 0U &&
           (selected_node_mask & static_cast<uint8_t>(~kSessionNodeMask)) == 0U;
}

constexpr uint8_t session_missing_node_mask(uint8_t selected_node_mask,
                                           uint8_t ready_node_mask)
{
    return static_cast<uint8_t>(selected_node_mask &
                                static_cast<uint8_t>(~ready_node_mask));
}

constexpr uint8_t session_expected_source_mask(uint8_t selected_node_mask)
{
    return static_cast<uint8_t>(kSessionMasterSourceBit |
                                (selected_node_mask & kSessionNodeMask));
}

constexpr bool session_source_mask_valid(SourceMask selected_source_mask)
{
    return selected_source_mask != 0U &&
           (selected_source_mask & static_cast<SourceMask>(~kSessionNodeSourceMask)) == 0U;
}

constexpr SourceMask session_missing_source_mask(SourceMask selected_source_mask,
                                                 SourceMask ready_source_mask)
{
    return static_cast<SourceMask>(selected_source_mask &
                                   static_cast<SourceMask>(~ready_source_mask));
}

constexpr SourceMask session_expected_source_mask_v2(SourceMask selected_source_mask)
{
    return static_cast<SourceMask>(source_bit(0U) |
                                   (selected_source_mask & kSessionNodeSourceMask));
}

} // namespace exo

#endif
