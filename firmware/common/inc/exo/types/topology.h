#ifndef EXO_TYPES_TOPOLOGY_H_
#define EXO_TYPES_TOPOLOGY_H_

#include <cstdint>

namespace exo {

using NodeId = uint8_t;
using SourceMask = uint16_t;

constexpr NodeId kUncommissionedNodeId = 0U;
constexpr NodeId kFirstNodeId = 1U;
constexpr NodeId kLastNodeId = 12U;
constexpr uint8_t kNodesPerHub = 6U;

enum class HubId : uint8_t {
    Invalid = 0U,
    Main = 1U,
    Lower = 2U
};

constexpr bool is_valid_node_id(NodeId id)
{
    return id >= kFirstNodeId && id <= kLastNodeId;
}

constexpr HubId hub_for_node(NodeId id)
{
    if (!is_valid_node_id(id)) return HubId::Invalid;
    return id <= kFirstNodeId + kNodesPerHub - 1U ? HubId::Main : HubId::Lower;
}

constexpr bool hub_owns_node(HubId hub, NodeId id)
{
    return is_valid_node_id(id) && hub_for_node(id) == hub;
}

/* Source zero is the Master; source bits 1 through 12 represent nodes. */
constexpr SourceMask source_bit(uint8_t source_id)
{
    return source_id <= kLastNodeId
        ? static_cast<SourceMask>(static_cast<SourceMask>(1U) << source_id)
        : static_cast<SourceMask>(0U);
}

}  // namespace exo

#endif  // EXO_TYPES_TOPOLOGY_H_
