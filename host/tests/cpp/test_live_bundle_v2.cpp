#include <cstdint>
#include <cstring>
#include <iostream>

#include <exo/protocol/live_bundle_v2.h>

namespace {

int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": expected " #expr "\n"; \
        ++failures; \
    } \
} while (0)

using Bundle = exo::LiveBundleV2Message<56U>;

void test_wire_contract_and_round_trip()
{
    static_assert(exo::kLiveBundleLegacyMarker == 0x03U,
                  "Legacy live bundle marker must remain stable");
    static_assert(exo::kLiveBundleV2Marker == 0x04U,
                  "Timestamped live bundle marker must be 0x04");
    static_assert(exo::kLiveBundleV2HeaderSize == 16U,
                  "Timestamped live bundle header must remain stable");
    static_assert(exo::kLiveBundleV2WireSize(56U, 20U) == 92U,
                  "Current BNO+ICM bundle must be 92 bytes");
    static_assert(exo::kLiveBundleV2WireSize(56U, 20U) <= BLEPIPE_MAX_APP_PAYLOAD,
                  "Timestamped live bundle must fit the BLEPipe application ceiling");

    Bundle message{};
    message.flags = exo::kLiveBundleV2FlagBnoPresent |
                    exo::kLiveBundleV2FlagIcmPresent;
    message.bundle_sequence = 0x10203040U;
    message.bno_acquired_ms = 0x11223344U;
    message.icm_acquired_ms = 0x55667788U;
    message.bno_payload_length = 56U;
    message.icm_payload_length = 20U;
    for (uint8_t i = 0U; i < message.bno_payload_length; ++i) {
        message.bno_payload[i] = i;
    }
    for (uint8_t i = 0U; i < message.icm_payload_length; ++i) {
        message.icm_payload[i] = static_cast<uint8_t>(0xA0U + i);
    }

    uint8_t encoded[exo::kLiveBundleV2WireSize(56U, 20U)] = { 0U };
    size_t encoded_len = 0U;
    EXPECT_TRUE(exo::live_bundle_v2_encode(message, encoded, sizeof(encoded),
                                           &encoded_len));
    EXPECT_TRUE(encoded_len == sizeof(encoded));
    const uint8_t expected_prefix[] = {
        0x04U, 0x03U, 0x38U, 0x14U,
        0x40U, 0x30U, 0x20U, 0x10U,
        0x44U, 0x33U, 0x22U, 0x11U,
        0x88U, 0x77U, 0x66U, 0x55U
    };
    EXPECT_TRUE(std::memcmp(encoded, expected_prefix, sizeof(expected_prefix)) == 0);
    EXPECT_TRUE(std::memcmp(encoded + exo::kLiveBundleV2HeaderSize,
                            message.bno_payload, message.bno_payload_length) == 0);
    EXPECT_TRUE(std::memcmp(encoded + exo::kLiveBundleV2HeaderSize +
                                message.bno_payload_length,
                            message.icm_payload, message.icm_payload_length) == 0);

    Bundle decoded{};
    EXPECT_TRUE(exo::live_bundle_v2_decode(encoded, encoded_len, decoded));
    EXPECT_TRUE(decoded.flags == message.flags);
    EXPECT_TRUE(decoded.bundle_sequence == message.bundle_sequence);
    EXPECT_TRUE(decoded.bno_acquired_ms == message.bno_acquired_ms);
    EXPECT_TRUE(decoded.icm_acquired_ms == message.icm_acquired_ms);
    EXPECT_TRUE(decoded.bno_payload_length == message.bno_payload_length);
    EXPECT_TRUE(decoded.icm_payload_length == message.icm_payload_length);
    EXPECT_TRUE(std::memcmp(decoded.bno_payload, message.bno_payload,
                            message.bno_payload_length) == 0);
    EXPECT_TRUE(std::memcmp(decoded.icm_payload, message.icm_payload,
                            message.icm_payload_length) == 0);
}

void test_validation_and_single_sensor_bundle()
{
    Bundle message{};
    message.flags = exo::kLiveBundleV2FlagIcmPresent;
    message.bundle_sequence = 7U;
    message.icm_acquired_ms = 99U;
    message.icm_payload_length = 2U;
    message.icm_payload[0] = 0xAAU;
    message.icm_payload[1] = 0x55U;

    uint8_t encoded[exo::kLiveBundleV2WireSize(0U, 2U)] = { 0U };
    size_t encoded_len = 0U;
    EXPECT_TRUE(exo::live_bundle_v2_encode(message, encoded, sizeof(encoded),
                                           &encoded_len));
    EXPECT_TRUE(encoded_len == sizeof(encoded));

    Bundle decoded{};
    EXPECT_TRUE(exo::live_bundle_v2_decode(encoded, encoded_len, decoded));
    EXPECT_TRUE(decoded.bno_payload_length == 0U);
    EXPECT_TRUE(decoded.icm_payload_length == 2U);

    EXPECT_TRUE(!exo::live_bundle_v2_decode(encoded, encoded_len - 1U, decoded));
    encoded[0] = exo::kLiveBundleLegacyMarker;
    EXPECT_TRUE(!exo::live_bundle_v2_decode(encoded, encoded_len, decoded));

    message.flags = exo::kLiveBundleV2FlagBnoPresent;
    message.bno_payload_length = 1U;
    message.icm_payload_length = 1U;
    EXPECT_TRUE(!exo::live_bundle_v2_encode(message, encoded, sizeof(encoded),
                                            &encoded_len));

    message.flags = 0U;
    message.bno_payload_length = 0U;
    message.icm_payload_length = 0U;
    EXPECT_TRUE(!exo::live_bundle_v2_encode(message, encoded, sizeof(encoded),
                                            &encoded_len));

    using OversizedBundle = exo::LiveBundleV2Message<200U>;
    OversizedBundle oversized{};
    oversized.flags = exo::kLiveBundleV2FlagBnoPresent |
                      exo::kLiveBundleV2FlagIcmPresent;
    oversized.bno_payload_length = 200U;
    oversized.icm_payload_length = 200U;
    uint8_t oversized_output[BLEPIPE_MAX_APP_PAYLOAD + 1U] = { 0U };
    EXPECT_TRUE(!exo::live_bundle_v2_encode(oversized, oversized_output,
                                            sizeof(oversized_output),
                                            &encoded_len));
}

}  // namespace

int main()
{
    test_wire_contract_and_round_trip();
    test_validation_and_single_sensor_bundle();

    if (failures != 0) {
        std::cerr << failures << " live bundle check(s) failed\n";
        return 1;
    }
    std::cout << "live bundle checks passed\n";
    return 0;
}
