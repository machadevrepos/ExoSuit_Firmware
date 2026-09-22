#ifndef EXO_BRIDGE_FRAME_CODEC_H_
#define EXO_BRIDGE_FRAME_CODEC_H_

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace exo::bridge {

static constexpr uint8_t kFrameVersion = 1U;
static constexpr uint16_t kMaxBlePipeFrameLength = 244U;
static constexpr size_t kHeaderLength = 14U;
static constexpr size_t kCrcLength = 4U;
static constexpr size_t kMaxRawFrameLength =
    kHeaderLength + kMaxBlePipeFrameLength + kCrcLength;
static constexpr size_t kMaxEncodedFrameLength =
    kMaxRawFrameLength + ((kMaxRawFrameLength + 253U) / 254U) + 1U;

enum class Lane : uint8_t {
    Live = 0U,
    Reliable = 1U
};

enum class Status : uint8_t {
    Ok = 0U,
    BadArgument,
    TooShort,
    TooLong,
    BadVersion,
    BadLane,
    BadLength,
    BadFrame,
    BadCrc
};

struct Frame {
    uint8_t version = kFrameVersion;
    Lane lane = Lane::Live;
    uint16_t flags = 0U;
    uint32_t boot_epoch = 0U;
    uint32_t sequence = 0U;
    const uint8_t *payload = nullptr;
    uint16_t payload_length = 0U;
};

inline bool valid_lane(Lane lane)
{
    return lane == Lane::Live || lane == Lane::Reliable;
}

inline void put_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = static_cast<uint8_t>(value & 0xFFU);
    dst[1] = static_cast<uint8_t>(value >> 8U);
}

inline void put_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>(value & 0xFFU);
    dst[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    dst[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    dst[3] = static_cast<uint8_t>(value >> 24U);
}

inline uint16_t get_le16(const uint8_t *src)
{
    return static_cast<uint16_t>(src[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(src[1]) << 8U);
}

inline uint32_t get_le32(const uint8_t *src)
{
    return static_cast<uint32_t>(src[0]) |
           (static_cast<uint32_t>(src[1]) << 8U) |
           (static_cast<uint32_t>(src[2]) << 16U) |
           (static_cast<uint32_t>(src[3]) << 24U);
}

inline uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

inline Status cobs_encode(const uint8_t *input, size_t input_length,
                          uint8_t *output, size_t output_capacity,
                          size_t *output_length)
{
    if (input == nullptr || output == nullptr || output_length == nullptr) {
        return Status::BadArgument;
    }
    if (output_capacity < input_length + (input_length / 254U) + 2U) {
        return Status::TooLong;
    }

    size_t code_index = 0U;
    size_t write_index = 1U;
    uint8_t code = 1U;
    for (size_t i = 0U; i < input_length; ++i) {
        if (input[i] == 0U) {
            output[code_index] = code;
            code_index = write_index++;
            code = 1U;
        } else {
            output[write_index++] = input[i];
            ++code;
            if (code == 0U) {
                output[code_index] = 0xFFU;
                code_index = write_index++;
                code = 1U;
            }
        }
    }
    output[code_index] = code;
    output[write_index++] = 0U;
    *output_length = write_index;
    return Status::Ok;
}

inline Status cobs_decode(const uint8_t *input, size_t input_length,
                          uint8_t *output, size_t output_capacity,
                          size_t *output_length)
{
    if (input == nullptr || output == nullptr || output_length == nullptr) {
        return Status::BadArgument;
    }
    if (input_length < 2U || input[input_length - 1U] != 0U) {
        return Status::BadFrame;
    }

    const size_t body_length = input_length - 1U;
    size_t read_index = 0U;
    size_t write_index = 0U;
    while (read_index < body_length) {
        const uint8_t code = input[read_index++];
        if (code == 0U || read_index + static_cast<size_t>(code) - 1U > body_length) {
            return Status::BadFrame;
        }
        for (uint8_t i = 1U; i < code; ++i) {
            if (write_index >= output_capacity) return Status::TooLong;
            output[write_index++] = input[read_index++];
        }
        if (code != 0xFFU && read_index < body_length) {
            if (write_index >= output_capacity) return Status::TooLong;
            output[write_index++] = 0U;
        }
    }
    *output_length = write_index;
    return Status::Ok;
}

inline Status encode(const Frame &frame, uint8_t *output, size_t output_capacity,
                     size_t *output_length)
{
    if (output == nullptr || output_length == nullptr ||
        (frame.payload == nullptr && frame.payload_length != 0U)) {
        return Status::BadArgument;
    }
    if (frame.version != kFrameVersion) return Status::BadVersion;
    if (!valid_lane(frame.lane)) return Status::BadLane;
    if (frame.payload_length > kMaxBlePipeFrameLength) return Status::TooLong;

    uint8_t raw[kMaxRawFrameLength]{};
    raw[0] = frame.version;
    raw[1] = static_cast<uint8_t>(frame.lane);
    put_le16(&raw[2], frame.flags);
    put_le32(&raw[4], frame.boot_epoch);
    put_le32(&raw[8], frame.sequence);
    put_le16(&raw[12], frame.payload_length);
    if (frame.payload_length != 0U) {
        std::memcpy(&raw[kHeaderLength], frame.payload, frame.payload_length);
    }
    const size_t body_length = kHeaderLength + frame.payload_length;
    put_le32(&raw[body_length], crc32(raw, body_length));
    return cobs_encode(raw, body_length + kCrcLength, output, output_capacity,
                       output_length);
}

inline Status decode(const uint8_t *input, size_t input_length,
                     uint8_t *payload_output, size_t payload_capacity,
                     Frame *frame)
{
    if (input == nullptr || frame == nullptr || payload_output == nullptr) {
        return Status::BadArgument;
    }
    uint8_t raw[kMaxRawFrameLength]{};
    size_t raw_length = 0U;
    const Status cobs_status = cobs_decode(input, input_length, raw,
                                           sizeof(raw), &raw_length);
    if (cobs_status != Status::Ok) return cobs_status;
    if (raw_length < kHeaderLength + kCrcLength) return Status::TooShort;
    if (raw[0] != kFrameVersion) return Status::BadVersion;
    const Lane lane = static_cast<Lane>(raw[1]);
    if (!valid_lane(lane)) return Status::BadLane;

    const uint16_t payload_length = get_le16(&raw[12]);
    if (payload_length > kMaxBlePipeFrameLength) return Status::TooLong;
    if (raw_length != kHeaderLength + payload_length + kCrcLength) {
        return Status::BadLength;
    }
    const uint32_t received_crc = get_le32(&raw[raw_length - kCrcLength]);
    if (crc32(raw, raw_length - kCrcLength) != received_crc) {
        return Status::BadCrc;
    }
    if (payload_length > payload_capacity) return Status::TooLong;
    if (payload_length != 0U) {
        std::memcpy(payload_output, &raw[kHeaderLength], payload_length);
    }
    frame->version = raw[0];
    frame->lane = lane;
    frame->flags = get_le16(&raw[2]);
    frame->boot_epoch = get_le32(&raw[4]);
    frame->sequence = get_le32(&raw[8]);
    frame->payload = payload_output;
    frame->payload_length = payload_length;
    return Status::Ok;
}

}  // namespace exo::bridge

#endif  // EXO_BRIDGE_FRAME_CODEC_H_
