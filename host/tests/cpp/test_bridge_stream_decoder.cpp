#include <cassert>
#include <cstdint>

#include <exo/bridge/frame_codec.h>
#include <exo/bridge/stream_decoder.h>

int main()
{
    const uint8_t source[] = {0x00U, 0x01U, 0x02U, 0x7EU, 0x00U, 0xFFU};
    exo::bridge::Frame original{};
    original.lane = exo::bridge::Lane::Reliable;
    original.flags = 0x1200U;
    original.boot_epoch = 7U;
    original.sequence = 42U;
    original.payload = source;
    original.payload_length = sizeof(source);

    uint8_t encoded[exo::bridge::kMaxEncodedFrameLength]{};
    size_t encoded_length = 0U;
    assert(exo::bridge::encode(original, encoded, sizeof(encoded), &encoded_length) ==
           exo::bridge::Status::Ok);

    exo::bridge::StreamDecoder<> decoder;
    for (size_t i = 0U; i < encoded_length; ++i) {
        (void)decoder.push(encoded[i]);
    }

    uint8_t payload[32]{};
    exo::bridge::Frame decoded{};
    assert(decoder.pop(decoded, payload, sizeof(payload)));
    assert(decoded.lane == original.lane);
    assert(decoded.flags == original.flags);
    assert(decoded.boot_epoch == original.boot_epoch);
    assert(decoded.sequence == original.sequence);
    assert(decoded.payload_length == sizeof(source));
    for (size_t i = 0U; i < sizeof(source); ++i) assert(payload[i] == source[i]);

    const uint8_t bad[] = {0x03U, 0x11U, 0x00U};
    for (uint8_t byte : bad) (void)decoder.push(byte);
    assert(decoder.counters().malformed == 1U);

    (void)decoder.push(0U);
    assert(decoder.counters().empty_delimiters == 1U);
    return 0;
}
