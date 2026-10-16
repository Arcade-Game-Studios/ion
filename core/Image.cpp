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

uint16_t readU16LE(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t readU32LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
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

// Refuse absurd dimensions before allocating (also guards the size math
// below against overflow). 16k x 16k RGBA is 1 GiB.
constexpr uint64_t kMaxPixels = (uint64_t)16384 * 16384;

// ---------------------------------------------------------------------------
// PNG
// ---------------------------------------------------------------------------

struct PngInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bitDepth = 0;
    uint32_t colorType = 0;
    uint32_t channels = 0;
    bool interlaced = false;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> trns;
};

// Reverses the per-scanline filters of one (sub)image in place. `data`
// holds rows of (1 + stride) bytes; the filtered bytes are replaced by the
// raw scanline bytes (the filter-type byte is left in place).
bool unfilter(uint8_t* data, uint32_t rows, size_t stride, size_t bpp) {
    const uint8_t* prev = nullptr;
    for (uint32_t y = 0; y < rows; y++) {
        uint8_t* row = data + (size_t)y * (stride + 1);
        uint8_t filter = row[0];
        uint8_t* cur = row + 1;
        for (size_t x = 0; x < stride; x++) {
            int a = x >= bpp ? cur[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int c = (x >= bpp && prev) ? prev[x - bpp] : 0;
            int value = cur[x];
            switch (filter) {
                case 0: break;
                case 1: value += a; break;
                case 2: value += b; break;
                case 3: value += (a + b) / 2; break;
                case 4: value += paethPredictor(a, b, c); break;
                default: return false;
            }
            cur[x] = (uint8_t)value;
        }
        prev = cur;
    }
    return true;
}

// Reads sample `index` (in samples, not pixels) from a raw scanline.
uint32_t readSample(const uint8_t* row, size_t index, uint32_t bitDepth) {
    switch (bitDepth) {
        case 8: return row[index];
        case 16: return ((uint32_t)row[index * 2] << 8) | row[index * 2 + 1];
        default: {
            size_t bit = index * bitDepth;
            uint32_t byte = row[bit / 8];
            uint32_t shift = 8 - bitDepth - (uint32_t)(bit % 8);
            return (byte >> shift) & ((1u << bitDepth) - 1);
        }
    }
}

// Scales a sample of the given bit depth to 8 bits.
uint8_t to8(uint32_t sample, uint32_t bitDepth) {
    switch (bitDepth) {
        case 1: return sample ? 255 : 0;
        case 2: return (uint8_t)(sample * 0x55);
        case 4: return (uint8_t)(sample * 0x11);
        case 8: return (uint8_t)sample;
        case 16: return (uint8_t)(sample >> 8);
        default: return 0;
    }
}

// Converts one raw scanline of `count` pixels to RGBA8, writing pixel i to
// out[i * outStep].
void convertRow(const PngInfo& info, const uint8_t* row, uint32_t count, uint8_t* out,
                size_t outStep) {
    const uint32_t depth = info.bitDepth;
    const std::vector<uint8_t>& trns = info.trns;
    for (uint32_t x = 0; x < count; x++) {
        uint8_t* dst = out + (size_t)x * outStep;
        uint8_t r = 0, g = 0, b = 0, a = 255;
        switch (info.colorType) {
            case 0: { // gray
                uint32_t v = readSample(row, x, depth);
                r = g = b = to8(v, depth);
                // tRNS holds one 16-bit gray value (at the image's bit
                // depth) that is fully transparent.
                if (trns.size() >= 2 && v == (((uint32_t)trns[0] << 8) | trns[1])) {
                    a = 0;
                }
                break;
            }
            case 2: { // RGB
                uint32_t vr = readSample(row, (size_t)x * 3, depth);
                uint32_t vg = readSample(row, (size_t)x * 3 + 1, depth);
                uint32_t vb = readSample(row, (size_t)x * 3 + 2, depth);
                r = to8(vr, depth);
                g = to8(vg, depth);
                b = to8(vb, depth);
                if (trns.size() >= 6 && vr == (((uint32_t)trns[0] << 8) | trns[1]) &&
                    vg == (((uint32_t)trns[2] << 8) | trns[3]) &&
                    vb == (((uint32_t)trns[4] << 8) | trns[5])) {
                    a = 0;
                }
                break;
            }
            case 3: { // indexed
                uint32_t index = readSample(row, x, depth);
                if (index * 3 + 2 < info.palette.size()) {
                    r = info.palette[index * 3];
                    g = info.palette[index * 3 + 1];
                    b = info.palette[index * 3 + 2];
                }
                if (index < trns.size()) {
                    a = trns[index]; // per-entry alpha
                }
                break;
            }
            case 4: // gray + alpha
                r = g = b = to8(readSample(row, (size_t)x * 2, depth), depth);
                a = to8(readSample(row, (size_t)x * 2 + 1, depth), depth);
                break;
            case 6: // RGBA
                r = to8(readSample(row, (size_t)x * 4, depth), depth);
                g = to8(readSample(row, (size_t)x * 4 + 1, depth), depth);
                b = to8(readSample(row, (size_t)x * 4 + 2, depth), depth);
                a = to8(readSample(row, (size_t)x * 4 + 3, depth), depth);
                break;
        }
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = a;
    }
}

size_t rowBytes(uint32_t pixels, uint32_t channels, uint32_t bitDepth) {
    return ((size_t)pixels * channels * bitDepth + 7) / 8;
}

bool decodePng(const uint8_t* data, size_t size, uint32_t& width,
               uint32_t& height, std::vector<uint8_t>& rgba) {
    static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 8 || std::memcmp(data, signature, 8) != 0) {
        return false;
    }

    PngInfo info;
    std::vector<uint8_t> idat;
    size_t pos = 8;
    bool seenHeader = false;
    while (pos + 8 <= size) {
        uint32_t length = readU32(data + pos);
        uint32_t chunkType = readU32(data + pos + 4);
        pos += 8;
        if (length > size - pos) {
            return false;
        }
        const uint8_t* chunk = data + pos;

        if (chunkType == 0x49484452) {  // IHDR
            if (length < 13 || seenHeader) {
                return false;
            }
            seenHeader = true;
            info.width = readU32(chunk);
            info.height = readU32(chunk + 4);
            info.bitDepth = chunk[8];
            info.colorType = chunk[9];
            if (chunk[10] != 0 || chunk[11] != 0 || chunk[12] > 1) {
                return false;  // unknown compression / filter / interlace method
            }
            info.interlaced = chunk[12] == 1;
        } else if (chunkType == 0x49444154) {  // IDAT
            idat.insert(idat.end(), chunk, chunk + length);
        } else if (chunkType == 0x504C5445) {  // PLTE
            info.palette.assign(chunk, chunk + length);
        } else if (chunkType == 0x74524E53) {  // tRNS
            info.trns.assign(chunk, chunk + length);
        } else if (chunkType == 0x49454E44) {  // IEND
            break;
        }

        pos += (size_t)length + 4;  // skip data + CRC
    }

    if (!seenHeader || info.width == 0 || info.height == 0 || idat.empty()) {
        return false;
    }
    if ((uint64_t)info.width * info.height > kMaxPixels) {
        ION_LOG_ERROR("Image: PNG too large (%ux%u)", info.width, info.height);
        return false;
    }

    // Valid (color type, bit depth) combinations from the PNG spec.
    const uint32_t d = info.bitDepth;
    switch (info.colorType) {
        case 0: info.channels = 1; if (d != 1 && d != 2 && d != 4 && d != 8 && d != 16) return false; break;
        case 2: info.channels = 3; if (d != 8 && d != 16) return false; break;
        case 3: info.channels = 1; if (d != 1 && d != 2 && d != 4 && d != 8) return false; break;
        case 4: info.channels = 2; if (d != 8 && d != 16) return false; break;
        case 6: info.channels = 4; if (d != 8 && d != 16) return false; break;
        default: return false;
    }
    if (info.colorType == 3 && info.palette.empty()) {
        return false;
    }

    // Sub-images: one for non-interlaced, seven Adam7 passes otherwise.
    struct Pass {
        uint32_t x0, y0, dx, dy;
    };
    static const Pass kAdam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                   {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static const Pass kSingle[1] = {{0, 0, 1, 1}};
    const Pass* passes = info.interlaced ? kAdam7 : kSingle;
    int passCount = info.interlaced ? 7 : 1;

    size_t rawSize = 0;
    for (int p = 0; p < passCount; p++) {
        const Pass& pass = passes[p];
        uint32_t pw = info.width > pass.x0 ? (info.width - pass.x0 + pass.dx - 1) / pass.dx : 0;
        uint32_t ph = info.height > pass.y0 ? (info.height - pass.y0 + pass.dy - 1) / pass.dy : 0;
        if (pw && ph) {
            rawSize += (rowBytes(pw, info.channels, d) + 1) * ph;
        }
    }

    std::vector<uint8_t> raw(rawSize);
    uLongf destLen = (uLongf)raw.size();
    if (uncompress(raw.data(), &destLen, idat.data(), (uLong)idat.size()) != Z_OK ||
        destLen != raw.size()) {
        ION_LOG_ERROR("Image: failed to decompress PNG data");
        return false;
    }

    width = info.width;
    height = info.height;
    rgba.assign((size_t)width * height * 4, 255);
    size_t bpp = rowBytes(1, info.channels, d); // filter byte distance (>= 1)
    if (bpp == 0) {
        bpp = 1;
    }
    uint8_t* cursor = raw.data();
    for (int p = 0; p < passCount; p++) {
        const Pass& pass = passes[p];
        uint32_t pw = width > pass.x0 ? (width - pass.x0 + pass.dx - 1) / pass.dx : 0;
        uint32_t ph = height > pass.y0 ? (height - pass.y0 + pass.dy - 1) / pass.dy : 0;
        if (!pw || !ph) {
            continue;
        }
        size_t stride = rowBytes(pw, info.channels, d);
        if (!unfilter(cursor, ph, stride, bpp)) {
            return false;
        }
        for (uint32_t y = 0; y < ph; y++) {
            const uint8_t* row = cursor + (size_t)y * (stride + 1) + 1;
            uint32_t outY = pass.y0 + y * pass.dy;
            uint8_t* out = rgba.data() + ((size_t)outY * width + pass.x0) * 4;
            convertRow(info, row, pw, out, (size_t)pass.dx * 4);
        }
        cursor += (stride + 1) * ph;
    }
    return true;
}

// ---------------------------------------------------------------------------
// BMP (uncompressed 24/32-bit and BI_BITFIELDS 32-bit)
// ---------------------------------------------------------------------------

int maskShift(uint32_t mask) {
    int shift = 0;
    while (mask && !(mask & 1u)) {
        mask >>= 1;
        shift++;
    }
    return shift;
}

uint8_t maskValue(uint32_t pixel, uint32_t mask) {
    if (!mask) {
        return 255;
    }
    int shift = maskShift(mask);
    uint32_t bits = mask >> shift;
    uint32_t v = (pixel & mask) >> shift;
    return (uint8_t)(bits == 255 ? v : (v * 255 + bits / 2) / bits);
}

bool decodeBmp(const uint8_t* data, size_t size, uint32_t& width, uint32_t& height,
               std::vector<uint8_t>& rgba) {
    if (size < 54 || data[0] != 'B' || data[1] != 'M') {
        return false;
    }
    uint32_t pixelOffset = readU32LE(data + 10);
    uint32_t headerSize = readU32LE(data + 14);
    if (headerSize < 40 || 14 + (size_t)headerSize > size) {
        return false;
    }
    int32_t w = (int32_t)readU32LE(data + 18);
    int32_t h = (int32_t)readU32LE(data + 22);
    uint16_t bits = readU16LE(data + 28);
    uint32_t compression = readU32LE(data + 30);
    if (w <= 0 || h == 0 || h == INT32_MIN || (bits != 24 && bits != 32)) {
        ION_LOG_WARN("Image: unsupported BMP (%dx%d, %d bpp)", (int)w, (int)h, (int)bits);
        return false;
    }
    bool topDown = h < 0;
    uint32_t absH = (uint32_t)(topDown ? -h : h);
    if ((uint64_t)w * absH > kMaxPixels) {
        return false;
    }
    uint32_t rMask = 0x00FF0000, gMask = 0x0000FF00, bMask = 0x000000FF, aMask = 0;
    if (compression == 3 && bits == 32) { // BI_BITFIELDS
        if (14 + 40 + 12 > size) {
            return false;
        }
        rMask = readU32LE(data + 54);
        gMask = readU32LE(data + 58);
        bMask = readU32LE(data + 62);
        aMask = headerSize >= 56 && 14 + 56 <= size ? readU32LE(data + 66) : 0;
    } else if (compression != 0) {
        ION_LOG_WARN("Image: compressed BMP is not supported");
        return false;
    } else if (bits == 32) {
        aMask = 0xFF000000; // common convention for 32-bit BI_RGB
    }
    size_t stride = (((size_t)w * bits + 31) / 32) * 4; // rows are 4-byte aligned
    if (pixelOffset > size || stride * absH > size - pixelOffset) {
        return false;
    }
    width = (uint32_t)w;
    height = absH;
    rgba.assign((size_t)width * height * 4, 255);
    bool anyAlpha = false;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t srcRow = topDown ? y : height - 1 - y;
        const uint8_t* src = data + pixelOffset + srcRow * stride;
        uint8_t* dst = rgba.data() + (size_t)y * width * 4;
        for (uint32_t x = 0; x < width; x++) {
            if (bits == 24) {
                dst[0] = src[x * 3 + 2];
                dst[1] = src[x * 3 + 1];
                dst[2] = src[x * 3];
            } else {
                uint32_t pixel = readU32LE(src + x * 4);
                dst[0] = maskValue(pixel, rMask);
                dst[1] = maskValue(pixel, gMask);
                dst[2] = maskValue(pixel, bMask);
                dst[3] = aMask ? maskValue(pixel, aMask) : 255;
                anyAlpha = anyAlpha || dst[3] != 0;
            }
            dst += 4;
        }
    }
    // Many writers leave the "alpha" byte zero in 32-bit BI_RGB files;
    // treat an all-zero alpha channel as opaque.
    if (bits == 32 && compression == 0 && !anyAlpha) {
        for (size_t i = 3; i < rgba.size(); i += 4) {
            rgba[i] = 255;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// TGA (true-color 24/32-bit and grayscale 8-bit, raw or RLE)
// ---------------------------------------------------------------------------

bool decodeTga(const uint8_t* data, size_t size, uint32_t& width, uint32_t& height,
               std::vector<uint8_t>& rgba) {
    if (size < 18) {
        return false;
    }
    uint8_t idLength = data[0];
    uint8_t colorMapType = data[1];
    uint8_t imageType = data[2];
    uint32_t w = readU16LE(data + 12);
    uint32_t h = readU16LE(data + 14);
    uint8_t bits = data[16];
    uint8_t descriptor = data[17];
    bool rle = imageType == 10 || imageType == 11;
    bool gray = imageType == 3 || imageType == 11;
    bool trueColor = imageType == 2 || imageType == 10;
    if (colorMapType != 0 || (!gray && !trueColor) || w == 0 || h == 0) {
        return false;
    }
    if ((trueColor && bits != 24 && bits != 32) || (gray && bits != 8)) {
        return false;
    }
    size_t bytesPerPixel = bits / 8;
    size_t pos = 18 + (size_t)idLength;
    size_t pixelCount = (size_t)w * h;
    std::vector<uint8_t> pixels(pixelCount * bytesPerPixel);
    if (!rle) {
        if (pos + pixels.size() > size) {
            return false;
        }
        std::memcpy(pixels.data(), data + pos, pixels.size());
    } else {
        size_t written = 0;
        while (written < pixelCount) {
            if (pos >= size) {
                return false;
            }
            uint8_t header = data[pos++];
            size_t run = (size_t)(header & 0x7F) + 1;
            if (written + run > pixelCount) {
                return false;
            }
            if (header & 0x80) { // repeated pixel
                if (pos + bytesPerPixel > size) {
                    return false;
                }
                for (size_t i = 0; i < run; i++) {
                    std::memcpy(&pixels[(written + i) * bytesPerPixel], data + pos,
                                bytesPerPixel);
                }
                pos += bytesPerPixel;
            } else { // literal run
                if (pos + run * bytesPerPixel > size) {
                    return false;
                }
                std::memcpy(&pixels[written * bytesPerPixel], data + pos, run * bytesPerPixel);
                pos += run * bytesPerPixel;
            }
            written += run;
        }
    }
    bool topDown = (descriptor & 0x20) != 0;
    bool rightToLeft = (descriptor & 0x10) != 0;
    width = w;
    height = h;
    rgba.assign(pixelCount * 4, 255);
    for (uint32_t y = 0; y < h; y++) {
        uint32_t srcY = topDown ? y : h - 1 - y;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t srcX = rightToLeft ? w - 1 - x : x;
            const uint8_t* src = &pixels[((size_t)srcY * w + srcX) * bytesPerPixel];
            uint8_t* dst = &rgba[((size_t)y * w + x) * 4];
            if (gray) {
                dst[0] = dst[1] = dst[2] = src[0];
            } else { // stored BGR(A)
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                if (bytesPerPixel == 4) {
                    dst[3] = src[3];
                }
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// PNG encoder
// ---------------------------------------------------------------------------

void writeU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

void writeChunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data,
                size_t length) {
    writeU32BE(out, (uint32_t)length);
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (length) {
        out.insert(out.end(), data, data + length);
    }
    uLong crc = crc32(0L, out.data() + start, (uInt)(length + 4));
    writeU32BE(out, (uint32_t)crc);
}

}  // namespace

bool loadImageFromMemory(const uint8_t* data, size_t size, uint32_t& width,
                         uint32_t& height, std::vector<uint8_t>& rgba) {
    if (!data || size == 0) {
        return false;
    }
    // Dispatch on signature; TGA has none, so it goes last.
    if (size >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return decodePng(data, size, width, height, rgba);
    }
    if (size >= 2 && data[0] == 'B' && data[1] == 'M') {
        return decodeBmp(data, size, width, height, rgba);
    }
    return decodeTga(data, size, width, height, rgba);
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
    if (!loadImageFromMemory(data.data(), data.size(), width, height, rgba)) {
        ION_LOG_ERROR("Image: unsupported or corrupt image '%s'", path.c_str());
        return false;
    }
    return true;
}

std::vector<uint8_t> encodePng(uint32_t width, uint32_t height, const uint8_t* rgba) {
    std::vector<uint8_t> out;
    if (!rgba || width == 0 || height == 0 || (uint64_t)width * height > kMaxPixels) {
        return out;
    }
    // Filter every row with "Up" (or None for the first): cheap and
    // compresses typical game art and screenshots well.
    size_t stride = (size_t)width * 4;
    std::vector<uint8_t> filtered((stride + 1) * height);
    for (uint32_t y = 0; y < height; y++) {
        uint8_t* dst = filtered.data() + (size_t)y * (stride + 1);
        const uint8_t* row = rgba + (size_t)y * stride;
        if (y == 0) {
            dst[0] = 0;
            std::memcpy(dst + 1, row, stride);
        } else {
            dst[0] = 2;
            const uint8_t* prev = row - stride;
            for (size_t x = 0; x < stride; x++) {
                dst[1 + x] = (uint8_t)(row[x] - prev[x]);
            }
        }
    }
    uLongf compressedSize = compressBound((uLong)filtered.size());
    std::vector<uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, filtered.data(), (uLong)filtered.size(),
                  Z_DEFAULT_COMPRESSION) != Z_OK) {
        return out;
    }

    static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.insert(out.end(), signature, signature + 8);
    uint8_t ihdr[13];
    for (int i = 0; i < 4; i++) {
        ihdr[i] = (uint8_t)(width >> (24 - 8 * i));
        ihdr[4 + i] = (uint8_t)(height >> (24 - 8 * i));
    }
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // RGBA
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    writeChunk(out, "IHDR", ihdr, sizeof(ihdr));
    writeChunk(out, "IDAT", compressed.data(), compressedSize);
    writeChunk(out, "IEND", nullptr, 0);
    return out;
}

bool savePng(const std::string& path, uint32_t width, uint32_t height, const uint8_t* rgba) {
    std::vector<uint8_t> png = encodePng(width, height, rgba);
    if (png.empty()) {
        return false;
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        ION_LOG_ERROR("Image: cannot write '%s'", path.c_str());
        return false;
    }
    file.write(reinterpret_cast<const char*>(png.data()), (std::streamsize)png.size());
    return (bool)file;
}

void flipImageVertically(uint32_t width, uint32_t height, uint8_t* rgba) {
    if (!rgba) {
        return;
    }
    size_t stride = (size_t)width * 4;
    std::vector<uint8_t> tmp(stride);
    for (uint32_t y = 0; y < height / 2; y++) {
        uint8_t* a = rgba + (size_t)y * stride;
        uint8_t* b = rgba + (size_t)(height - 1 - y) * stride;
        std::memcpy(tmp.data(), a, stride);
        std::memcpy(a, b, stride);
        std::memcpy(b, tmp.data(), stride);
    }
}

void premultiplyAlpha(uint32_t width, uint32_t height, uint8_t* rgba) {
    if (!rgba) {
        return;
    }
    size_t count = (size_t)width * height;
    for (size_t i = 0; i < count; i++) {
        uint8_t* p = rgba + i * 4;
        uint32_t a = p[3];
        p[0] = (uint8_t)((p[0] * a + 127) / 255);
        p[1] = (uint8_t)((p[1] * a + 127) / 255);
        p[2] = (uint8_t)((p[2] * a + 127) / 255);
    }
}

}  // namespace ion
