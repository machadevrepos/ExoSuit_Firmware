#include <cstdint>
#include <cstring>

#include <exo/protocol/ble_record_protocol.h>

static_assert(sizeof(exo::StartSessionMessage) == 19U,
              "Legacy StartSession wire format must remain unchanged");
static_assert(sizeof(exo::StartSessionV2Message) == 21U,
              "StartSessionV2 wire format must include a 16-bit source mask");
static_assert(exo::kStartSessionV2WireSize == 21U,
              "StartSessionV2 codec size must remain stable");
static_assert(static_cast<uint8_t>(exo::RecordCommand::StartSessionV2) == 0x11U,
              "StartSessionV2 must use a collision-free command id");
static_assert(static_cast<uint16_t>(exo::RecordSourceId::Node4) == 4U,
              "Legacy RecordSourceId values must remain unchanged");
static_assert(static_cast<uint16_t>(exo::RecordSourceId::Node12) == 12U,
              "RecordSourceId must represent all twelve nodes");

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
    if (std::memcmp(&message, expected, sizeof(expected)) != 0) return 1;

    uint8_t encoded[exo::kStartSessionV2WireSize] = { 0U };
    if (!exo::start_session_v2_encode(message, encoded, sizeof(encoded)) ||
            std::memcmp(encoded, expected, sizeof(expected)) != 0) {
        return 2;
    }

    exo::StartSessionV2Message decoded{};
    if (!exo::start_session_v2_decode(encoded, sizeof(encoded), decoded) ||
            decoded.session_id != message.session_id ||
            decoded.start_timestamp_us != message.start_timestamp_us ||
            decoded.safety_duration_ms != message.safety_duration_ms ||
            decoded.selected_source_mask != message.selected_source_mask ||
            decoded.stream_interval_ms != message.stream_interval_ms) {
        return 3;
    }
    if (exo::start_session_v2_decode(encoded, sizeof(encoded) - 1U, decoded)) return 4;
    encoded[1] = 0x03U;
    if (exo::start_session_v2_decode(encoded, sizeof(encoded), decoded)) return 5;
    return 0;
}
