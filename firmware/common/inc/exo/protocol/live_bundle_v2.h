#ifndef EXO_LIVE_BUNDLE_V2_H_
#define EXO_LIVE_BUNDLE_V2_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <exo/protocol/blepipe_proto.h>

namespace exo {

static constexpr uint8_t kLiveBundleLegacyMarker = 0x03U;
static constexpr uint8_t kLiveBundleV2Marker = 0x04U;
static constexpr uint8_t kLiveBundleV2FlagBnoPresent = 0x01U;
static constexpr uint8_t kLiveBundleV2FlagIcmPresent = 0x02U;
static constexpr uint8_t kLiveBundleV2KnownFlags =
        kLiveBundleV2FlagBnoPresent | kLiveBundleV2FlagIcmPresent;
static constexpr uint16_t kLiveBundleV2HeaderSize = 16U;

constexpr size_t kLiveBundleV2WireSize(uint8_t bno_payload_length,
                                       uint8_t icm_payload_length)
{
    return static_cast<size_t>(kLiveBundleV2HeaderSize) +
           bno_payload_length + icm_payload_length;
}

template<uint8_t MaxSensorPayload>
struct LiveBundleV2Message {
    uint8_t flags = 0U;
    uint8_t bno_payload_length = 0U;
    uint8_t icm_payload_length = 0U;
    uint32_t bundle_sequence = 0U;
    uint32_t bno_acquired_ms = 0U;
    uint32_t icm_acquired_ms = 0U;
    uint8_t bno_payload[MaxSensorPayload] {};
    uint8_t icm_payload[MaxSensorPayload] {};
};

template<uint8_t MaxSensorPayload>
inline bool live_bundle_v2_fields_valid(const LiveBundleV2Message<MaxSensorPayload> &message)
{
    if ((message.flags & static_cast<uint8_t>(~kLiveBundleV2KnownFlags)) != 0U ||
            message.bno_payload_length > MaxSensorPayload ||
            message.icm_payload_length > MaxSensorPayload ||
            (message.bno_payload_length == 0U && message.icm_payload_length == 0U)) {
        return false;
    }
    const uint8_t expected_flags =
            (message.bno_payload_length != 0U ? kLiveBundleV2FlagBnoPresent : 0U) |
            (message.icm_payload_length != 0U ? kLiveBundleV2FlagIcmPresent : 0U);
    return message.flags == expected_flags &&
           kLiveBundleV2WireSize(message.bno_payload_length,
                                 message.icm_payload_length) <= BLEPIPE_MAX_APP_PAYLOAD;
}

inline void live_bundle_v2_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>(value & 0xFFU);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    dst[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    dst[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

inline uint32_t live_bundle_v2_get_u32(const uint8_t *src)
{
    return static_cast<uint32_t>(src[0]) |
           (static_cast<uint32_t>(src[1]) << 8) |
           (static_cast<uint32_t>(src[2]) << 16) |
           (static_cast<uint32_t>(src[3]) << 24);
}

template<uint8_t MaxSensorPayload>
inline bool live_bundle_v2_encode(const LiveBundleV2Message<MaxSensorPayload> &message,
                                  uint8_t *dst, size_t dst_len, size_t *encoded_len)
{
    if (dst == nullptr || encoded_len == nullptr ||
            !live_bundle_v2_fields_valid(message)) {
        return false;
    }
    const size_t wire_size = kLiveBundleV2WireSize(message.bno_payload_length,
                                                    message.icm_payload_length);
    if (dst_len < wire_size) {
        return false;
    }
    dst[0] = kLiveBundleV2Marker;
    dst[1] = message.flags;
    dst[2] = message.bno_payload_length;
    dst[3] = message.icm_payload_length;
    live_bundle_v2_put_u32(dst + 4U, message.bundle_sequence);
    live_bundle_v2_put_u32(dst + 8U, message.bno_acquired_ms);
    live_bundle_v2_put_u32(dst + 12U, message.icm_acquired_ms);
    size_t offset = kLiveBundleV2HeaderSize;
    if (message.bno_payload_length != 0U) {
        memcpy(dst + offset, message.bno_payload, message.bno_payload_length);
        offset += message.bno_payload_length;
    }
    if (message.icm_payload_length != 0U) {
        memcpy(dst + offset, message.icm_payload, message.icm_payload_length);
        offset += message.icm_payload_length;
    }
    *encoded_len = offset;
    return true;
}

template<uint8_t MaxSensorPayload>
inline bool live_bundle_v2_decode(const uint8_t *src, size_t src_len,
                                  LiveBundleV2Message<MaxSensorPayload> &message)
{
    if (src == nullptr || src_len < kLiveBundleV2HeaderSize ||
            src[0] != kLiveBundleV2Marker) {
        return false;
    }
    LiveBundleV2Message<MaxSensorPayload> decoded{};
    decoded.flags = src[1];
    decoded.bno_payload_length = src[2];
    decoded.icm_payload_length = src[3];
    decoded.bundle_sequence = live_bundle_v2_get_u32(src + 4U);
    decoded.bno_acquired_ms = live_bundle_v2_get_u32(src + 8U);
    decoded.icm_acquired_ms = live_bundle_v2_get_u32(src + 12U);
    if (!live_bundle_v2_fields_valid(decoded) ||
            src_len != kLiveBundleV2WireSize(decoded.bno_payload_length,
                                              decoded.icm_payload_length)) {
        return false;
    }
    size_t offset = kLiveBundleV2HeaderSize;
    if (decoded.bno_payload_length != 0U) {
        memcpy(decoded.bno_payload, src + offset, decoded.bno_payload_length);
        offset += decoded.bno_payload_length;
    }
    if (decoded.icm_payload_length != 0U) {
        memcpy(decoded.icm_payload, src + offset, decoded.icm_payload_length);
    }
    message = decoded;
    return true;
}

}  // namespace exo

#endif
