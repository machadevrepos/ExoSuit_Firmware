#include <cstdint>
#include <cstring>

#include <exo/protocol/ble_record_protocol.h>

static_assert(sizeof(exo::StartSessionMessage) == 19U,
              "Legacy StartSession wire format must remain unchanged");
static_assert(sizeof(exo::StartSessionV2Message) == 21U,
              "StartSessionV2 wire format must include a 16-bit source mask");
static_assert(static_cast<uint8_t>(exo::RecordCommand::StartSessionV2) == 0x11U,
              "StartSessionV2 must use a collision-free command id");

int main()
{
    exo::StartSessionMessage legacy{};
    legacy.command = exo::RecordCommand::StartSession;
    legacy.session_id = 0x12345678U;
    legacy.start_timestamp_us = 0x0102030405060708ULL;
    legacy.safety_duration_ms = 0x11223344U;
    legacy.selected_node_mask = 0x0AU;
    legacy.stream_interval_ms = 40U;
    const uint8_t legacy_expected[] = {
        0x0FU, 0x78U, 0x56U, 0x34U, 0x12U,
        0x08U, 0x07U, 0x06U, 0x05U, 0x04U, 0x03U, 0x02U, 0x01U,
        0x44U, 0x33U, 0x22U, 0x11U, 0x0AU, 0x28U
    };
    if (std::memcmp(&legacy, legacy_expected, sizeof(legacy_expected)) != 0) return 1;

    exo::StartSessionV2Message message{};
    message.command = exo::RecordCommand::StartSessionV2;
    message.protocol_version = 2U;
    message.session_id = 0x12345678U;
    message.start_timestamp_us = 0x0102030405060708ULL;
    message.safety_duration_ms = 0x11223344U;
    message.selected_source_mask = 0x1002U;
    message.stream_interval_ms = 40U;
    const uint8_t expected[] = {
        0x11U, 0x02U, 0x78U, 0x56U, 0x34U, 0x12U,
        0x08U, 0x07U, 0x06U, 0x05U, 0x04U, 0x03U, 0x02U, 0x01U,
        0x44U, 0x33U, 0x22U, 0x11U, 0x02U, 0x10U, 0x28U
    };
    return std::memcmp(&message, expected, sizeof(expected)) == 0 ? 0 : 1;
}
