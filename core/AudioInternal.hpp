#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ion::audio_detail {

inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

// Converts `count` little-endian samples of the given encoding to float.
inline void convertPcm(const uint8_t* src, size_t count, uint16_t bits, bool isFloat,
                       float* out) {
    size_t bytes = bits / 8;
    for (size_t i = 0; i < count; i++) {
        const uint8_t* p = src + i * bytes;
        float v;
        if (isFloat) {
            uint32_t u = rd32(p);
            std::memcpy(&v, &u, 4);
        } else if (bits == 8) {
            v = ((int)p[0] - 128) / 128.0f;
        } else if (bits == 16) {
            v = (int16_t)rd16(p) / 32768.0f;
        } else if (bits == 24) {
            int32_t s = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 |
                                  (uint32_t)p[2] << 24);
            v = (float)(s >> 8) / 8388608.0f;
        } else {
            v = (float)((int32_t)rd32(p)) / 2147483648.0f;
        }
        out[i] = v;
    }
}

inline bool supportedPcm(uint16_t format, uint16_t bits, bool& isFloat) {
    isFloat = (format == 3 && bits == 32);
    return isFloat || (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32));
}

} // namespace ion::audio_detail
