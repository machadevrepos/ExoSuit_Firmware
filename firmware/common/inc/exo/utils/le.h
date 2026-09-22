#ifndef EXO_UTILS_LE_H_
#define EXO_UTILS_LE_H_

#include <cstddef>
#include <cstdint>

namespace exo {

/* Little-endian wire packing shared by the bridge codec and protocol headers. */
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
    return static_cast<uint16_t>(static_cast<uint16_t>(src[0]) |
                                 static_cast<uint16_t>(static_cast<uint16_t>(src[1]) << 8U));
}

inline uint32_t get_le32(const uint8_t *src)
{
    return static_cast<uint32_t>(src[0]) |
           (static_cast<uint32_t>(src[1]) << 8U) |
           (static_cast<uint32_t>(src[2]) << 16U) |
           (static_cast<uint32_t>(src[3]) << 24U);
}

}  // namespace exo

#endif  // EXO_UTILS_LE_H_
