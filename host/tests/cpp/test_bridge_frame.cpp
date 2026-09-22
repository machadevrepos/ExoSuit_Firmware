#include <cassert>
#include <cstdint>
#include <iostream>

#include <exo/bridge/frame_codec.h>

int main()
{
    const uint8_t payload[] = {0x00U, 0x01U, 0x02U, 0x00U, 0xFFU};
    exo::bridge::Frame frame{};
    frame.lane = exo::bridge::Lane::Reliable;
    frame.flags = 0x0003U;
    frame.boot_epoch = 0x11223344U;
    frame.sequence = 0x55667788U;
    frame.payload = payload;
    frame.payload_length = sizeof(payload);

    uint8_t encoded[exo::bridge::kMaxEncodedFrameLength]{};
    size_t encoded_length = 0U;
    assert(exo::bridge::encode(frame, encoded, sizeof(encoded), &encoded_length) ==
           exo::bridge::Status::Ok);
    assert(encoded[encoded_length - 1U] == 0U);

    const uint8_t expected[] = {
        0x04U, 0x01U, 0x01U, 0x03U, 0x0AU, 0x44U, 0x33U, 0x22U,
        0x11U, 0x88U, 0x77U, 0x66U, 0x55U, 0x05U, 0x01U, 0x03U,
        0x01U, 0x02U, 0x06U, 0xFFU, 0x98U, 0xB8U, 0x87U, 0x4DU,
        0x00U
    };
    assert(encoded_length == sizeof(expected));
    for (size_t i = 0U; i < sizeof(expected); ++i) {
        assert(encoded[i] == expected[i]);
    }

    uint8_t decoded_payload[sizeof(payload)]{};
    exo::bridge::Frame decoded{};
    decoded.payload = decoded_payload;
    assert(exo::bridge::decode(encoded, encoded_length, decoded_payload,
                               sizeof(decoded_payload), &decoded) ==
           exo::bridge::Status::Ok);
    assert(decoded.lane == exo::bridge::Lane::Reliable);
    assert(decoded.flags == frame.flags);
    assert(decoded.boot_epoch == frame.boot_epoch);
    assert(decoded.sequence == frame.sequence);
    assert(decoded.payload_length == sizeof(payload));
    for (size_t i = 0U; i < sizeof(payload); ++i) {
        assert(decoded_payload[i] == payload[i]);
    }

    uint8_t corrupt[exo::bridge::kMaxEncodedFrameLength]{};
    for (size_t i = 0U; i < encoded_length; ++i) corrupt[i] = encoded[i];
    corrupt[encoded_length - 2U] ^= 0x01U;
    assert(exo::bridge::decode(corrupt, encoded_length, decoded_payload,
                               sizeof(decoded_payload), &decoded) ==
           exo::bridge::Status::BadCrc);
    assert(exo::bridge::decode(encoded, encoded_length - 1U, decoded_payload,
                               sizeof(decoded_payload), &decoded) ==
           exo::bridge::Status::BadFrame);

    frame.payload_length = exo::bridge::kMaxBlePipeFrameLength + 1U;
    assert(exo::bridge::encode(frame, encoded, sizeof(encoded), &encoded_length) ==
           exo::bridge::Status::TooLong);

    frame.payload_length = sizeof(payload);
    frame.version = 2U;
    assert(exo::bridge::encode(frame, encoded, sizeof(encoded), &encoded_length) ==
           exo::bridge::Status::BadVersion);
    frame.version = exo::bridge::kFrameVersion;
    frame.lane = static_cast<exo::bridge::Lane>(2U);
    assert(exo::bridge::encode(frame, encoded, sizeof(encoded), &encoded_length) ==
           exo::bridge::Status::BadLane);

    std::cout << "bridge frame checks passed\n";
    return 0;
}
