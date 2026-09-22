#ifndef EXO_UTILS_CRC32_H_
#define EXO_UTILS_CRC32_H_

#include <cstddef>
#include <cstdint>

namespace exo {

/* Reflected IEEE CRC-32 (poly 0xEDB88320, init/final 0xFFFFFFFF). Shared by
 * node runtime settings and the bridge frame codec. */
inline uint32_t crc32_ieee(const uint8_t *data, size_t length)
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

}  // namespace exo

#endif  // EXO_UTILS_CRC32_H_
