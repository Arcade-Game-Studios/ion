#include <ion/render/Image.hpp>

#include <ion/core/Log.hpp>

#include <cstdlib>
#include <cstring>
#include <fstream>

#include <zlib.h>

namespace ion {

namespace {

uint32_t readU32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int paethPredictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return (pb <= pc) ? b : c;
}

bool decodePng(const uint8_t* data, size_t size, uint32_t& width,
               uint32_t& height, std::vector<uint8_t>& rgba) {
    static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 8 || std::memcmp(data, signature, 8) != 0) {
        return false;
    }

    uint32_t bitDepth = 0;
    uint32_t colorType = 0;
    uint32_t interlace = 0;
    std::vector<uint8_t> idat;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> trns;

    size_t pos = 8;
    bool seenHeader = false;
    while (pos + 8 <= size) {
        uint32_t length = readU32(data + pos);
        uint32_t chunkType = readU32(data + pos + 4);
        pos += 8;
        if (pos + length > size) {
            return false;
        }
        const uint8_t* chunk = data + pos;

        if (chunkType == 0x49484452) {  // IHDR
            if (length < 13 || seenHeader) {
                return false;
            }
            seenHeader = true;
            width = readU32(chunk);
            height = readU32(chunk + 4);
            bitDepth = chunk[8];
            colorType = chunk[9];
            if (chunk[10] != 0 || chunk[11] != 0) {
                return false;  // compressed / filter method unsupported
            }
            interlace = chunk[12];
        } else if (chunkType == 0x49444154) {  // IDAT
            idat.insert(idat.end(), chunk, chunk + length);
        } else if (chunkType == 0x504C5445) {  // PLTE
            palette.assign(chunk, chunk + length);
        } else if (chunkType == 0x74524E53) {  // tRNS
            trns.assign(chunk, chunk + length);
        }

        pos += length + 4;  // skip data + CRC
    }

    if (!seenHeader || width == 0 || height == 0) {
        return false;
    }
    if (bitDepth != 8 || interlace != 0) {
        ION_LOG_WARN("Image: unsupported PNG bit depth %u or interlace mode %u",
                     bitDepth, interlace);
        return false;
    }

    uint32_t channels = 0;
    switch (colorType) {
        case 0: channels = 1; break;  // grayscale
        case 2: channels = 3; break;  // RGB
        case 3: channels = 1; break;  // indexed
        case 4: channels = 2; break;  // gray + alpha
        case 6: channels = 4; break;  // RGBA
        default: return false;
    }

    uint32_t stride = width * channels;
    uint64_t rawSize = (uint64_t)(stride + 1) * height;
    if (rawSize > (uint64_t)1 << 30) {
        return false;
    }

    std::vector<uint8_t> raw((size_t)rawSize);
    uLongf destLen = (uLongf)raw.size();
    if (uncompress(raw.data(), &destLen, idat.data(), (uLong)idat.size()) !=
        Z_OK) {
        ION_LOG_ERROR("Image: failed to decompress PNG data");
        return false;
    }
    if (destLen != raw.size()) {
        return false;
    }

    // Unfilter scanlines into a flat pixel buffer.
    std::vector<uint8_t> unfiltered((size_t)(stride * height));
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* src = raw.data() + (size_t)(stride + 1) * y;
        uint8_t filter = src[0];
        const uint8_t* prev = (y == 0) ? nullptr : unfiltered.data() +
                                                      (size_t)(y - 1) * stride;
        uint8_t* out = unfiltered.data() + (size_t)y * stride;
        for (uint32_t x = 0; x < stride; x++) {
            int rawVal = src[1 + x];
            int a = (x >= channels) ? out[x - channels] : 0;
            int b = prev ? prev[x] : 0;
            int c = (x >= channels && prev) ? prev[x - channels] : 0;
            int value = 0;
            switch (filter) {
                case 0: value = rawVal; break;
                case 1: value = rawVal + a; break;
                case 2: value = rawVal + b; break;
                case 3: value = rawVal + (a + b) / 2; break;
                case 4: value = rawVal + paethPredictor(a, b, c); break;
                default: return false;
            }
            out[x] = (uint8_t)value;
        }
    }

    // Convert to RGBA8.
    rgba.assign((size_t)width * height * 4, 255);
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* src = unfiltered.data() + (size_t)y * stride;
        uint8_t* dst = rgba.data() + (size_t)y * width * 4;
        for (uint32_t x = 0; x < width; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            switch (colorType) {
                case 0:  // gray
                    r = g = b = src[0];
                    if (!trns.empty() && trns.size() >= 2 &&
                        src[0] == trns[0]) {
                        a = trns[1];
                    }
                    break;
                case 2:  // RGB
                    r = src[0];
                    g = src[1];
                    b = src[2];
                    if (!trns.empty() && trns.size() >= 6 && r == trns[0] &&
                        g == trns[2] && b == trns[4]) {
                        a = trns[5];
                    }
                    break;
                case 3: {  // indexed
                    uint8_t index = src[0];
                    if (index * 3 + 2 < palette.size()) {
                        r = palette[index * 3];
                        g = palette[index * 3 + 1];
                        b = palette[index * 3 + 2];
                    }
                    if (index < trns.size()) {
                        a = trns[index];
                    }
                    break;
                }
                case 4:  // gray + alpha
                    r = g = b = src[0];
                    a = src[1];
                    break;
                case 6:  // RGBA
                    r = src[0];
                    g = src[1];
                    b = src[2];
                    a = src[3];
                    break;
            }
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = a;
            src += channels;
            dst += 4;
        }
    }
    return true;
}

}  // namespace

bool loadImageFromMemory(const uint8_t* data, size_t size, uint32_t& width,
                         uint32_t& height, std::vector<uint8_t>& rgba) {
    return decodePng(data, size, width, height, rgba);
}

bool loadImage(const std::string& path, uint32_t& width, uint32_t& height,
               std::vector<uint8_t>& rgba) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ION_LOG_ERROR("Image: cannot open '%s'", path.c_str());
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    if (data.empty()) {
        ION_LOG_ERROR("Image: empty file '%s'", path.c_str());
        return false;
    }
    return loadImageFromMemory(data.data(), data.size(), width, height, rgba);
}

}  // namespace ion
