#include <stdint.h>

#include <exo/protocol/ble_record_protocol.h>
#include <exo/protocol/ble_session_control.h>

static_assert(sizeof(exo::StopRecordMessage) == 5U,
              "StopRecord wire format must remain compact");
static_assert(sizeof(exo::StartSessionMessage) == 19U,
              "StartSession wire format must match the browser encoder");
static_assert(sizeof(exo::RetrySourceMessage) == 2U,
              "RetrySource wire format must match the browser encoder");

static_assert(exo::session_node_mask_valid(0x02U),
              "A selected one-node session must be valid");
static_assert(exo::session_node_mask_valid(0x1EU),
              "NODE1 through NODE4 must be valid");
static_assert(!exo::session_node_mask_valid(0x00U),
              "At least one Node must be selected");
static_assert(!exo::session_node_mask_valid(0x20U),
              "Bits outside NODE1 through NODE4 must be rejected");
static_assert(exo::session_missing_node_mask(0x0AU, 0x02U) == 0x08U,
              "Missing selected participants must be reported exactly");
static_assert(exo::session_expected_source_mask(0x0AU) == 0x0BU,
              "The Master bit must be added to selected Node bits");

static_assert(exo::session_source_mask_valid(0x0002U),
              "A selected twelve-node session must accept Node 1");
static_assert(exo::session_source_mask_valid(0x1FFEU),
              "A selected twelve-node session must accept Nodes 1 through 12");
static_assert(!exo::session_source_mask_valid(0x0001U),
              "The Master source cannot be selected as a Node source");
static_assert(!exo::session_source_mask_valid(0x2000U),
              "Source bits above Node 12 must be rejected");
static_assert(exo::session_missing_source_mask(0x000AU, 0x0002U) == 0x0008U,
              "Missing twelve-node participants must be reported exactly");
static_assert(exo::session_expected_source_mask_v2(static_cast<exo::SourceMask>(0x1000U)) == 0x1001U,
              "The Master bit must be added to a twelve-node source mask");

int main()
{
    return 0;
}
