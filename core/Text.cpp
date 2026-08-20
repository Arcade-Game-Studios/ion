#include <ion/render/Text.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace ion {

//
// Built-in 5x7 monospace bitmap font (ASCII 32-126). Each glyph is 7 rows;
// bit 0 (LSB) is the leftmost column. Public-domain style minimal font used
// so text rendering requires no external assets.
//
namespace {

constexpr uint8_t kGlyphWidth = 5;
constexpr uint8_t kGlyphHeight = 7;
constexpr uint8_t kPitch = 6; // glyph width + 1px spacing
constexpr uint8_t kCellHeight = 8; // glyph height + 1px spacing
constexpr uint8_t kColumns = 16;

constexpr uint8_t kFont5x7[95][kGlyphHeight] = {
    // ' ' 32
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // '!' 33
    {0x02, 0x02, 0x02, 0x02, 0x02, 0x00, 0x02},
    // '"' 34
    {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00},
    // '#' 35
    {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A},
    // '$' 36
    {0x04, 0x0E, 0x05, 0x06, 0x14, 0x0E, 0x04},
    // '%' 37
    {0x19, 0x0B, 0x04, 0x02, 0x01, 0x19, 0x18},
    // '&' 38
    {0x0C, 0x12, 0x14, 0x0C, 0x15, 0x12, 0x0D},
    // '\'' 39
    {0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00},
    // '(' 40
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08},
    // ')' 41
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02},
    // '*' 42
    {0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00},
    // '+' 43
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},
    // ',' 44
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08},
    // '-' 45
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    // '.' 46
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04},
    // '/' 47
    {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01},
    // '0' 48
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    // '1' 49
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    // '2' 50
    {0x0E, 0x11, 0x10, 0x08, 0x04, 0x02, 0x1F},
    // '3' 51
    {0x1F, 0x08, 0x04, 0x08, 0x10, 0x11, 0x0E},
    // '4' 52
    {0x08, 0x0C, 0x0A, 0x09, 0x1F, 0x08, 0x08},
    // '5' 53
    {0x1F, 0x01, 0x0F, 0x10, 0x10, 0x11, 0x0E},
    // '6' 54
    {0x0E, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0E},
    // '7' 55
    {0x1F, 0x10, 0x08, 0x04, 0x02, 0x02, 0x02},
    // '8' 56
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    // '9' 57
    {0x0E, 0x11, 0x11, 0x11, 0x1E, 0x10, 0x0E},
    // ':' 58
    {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00},
    // ';' 59
    {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x08},
    // '<' 60
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08},
    // '=' 61
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00},
    // '>' 62
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02},
    // '?' 63
    {0x0E, 0x11, 0x10, 0x08, 0x04, 0x00, 0x04},
    // '@' 64
    {0x0E, 0x11, 0x15, 0x15, 0x17, 0x01, 0x0E},
    // 'A' 65
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    // 'B' 66
    {0x0F, 0x11, 0x11, 0x0F, 0x11, 0x11, 0x0F},
    // 'C' 67
    {0x0E, 0x11, 0x01, 0x01, 0x01, 0x11, 0x0E},
    // 'D' 68
    {0x0F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0F},
    // 'E' 69
    {0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F},
    // 'F' 70
    {0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x01},
    // 'G' 71
    {0x0E, 0x11, 0x01, 0x1D, 0x11, 0x11, 0x0E},
    // 'H' 72
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    // 'I' 73
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    // 'J' 74
    {0x1C, 0x08, 0x08, 0x08, 0x08, 0x09, 0x06},
    // 'K' 75
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    // 'L' 76
    {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1F},
    // 'M' 77
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    // 'N' 78
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},
    // 'O' 79
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    // 'P' 80
    {0x0F, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x01},
    // 'Q' 81
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    // 'R' 82
    {0x0F, 0x11, 0x11, 0x0F, 0x14, 0x12, 0x11},
    // 'S' 83
    {0x0E, 0x11, 0x01, 0x0E, 0x10, 0x11, 0x0E},
    // 'T' 84
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    // 'U' 85
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    // 'V' 86
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    // 'W' 87
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
    // 'X' 88
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    // 'Y' 89
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
    // 'Z' 90
    {0x1F, 0x10, 0x08, 0x04, 0x02, 0x01, 0x1F},
    // '[' 91
    {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E},
    // '\' 92
    {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10},
    // ']' 93
    {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E},
    // '^' 94
    {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00},
    // '_' 95
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F},
    // '`' 96
    {0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 'a' 97
    {0x00, 0x00, 0x0E, 0x11, 0x1E, 0x11, 0x1E},
    // 'b' 98
    {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F},
    // 'c' 99
    {0x00, 0x00, 0x0E, 0x01, 0x01, 0x01, 0x0E},
    // 'd' 100
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E},
    // 'e' 101
    {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x01, 0x0E},
    // 'f' 102
    {0x0C, 0x12, 0x02, 0x07, 0x02, 0x02, 0x02},
    // 'g' 103
    {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x02, 0x02},
    // 'h' 104
    {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x11},
    // 'i' 105
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E},
    // 'j' 106
    {0x08, 0x00, 0x18, 0x08, 0x08, 0x09, 0x06},
    // 'k' 107
    {0x01, 0x01, 0x12, 0x14, 0x18, 0x14, 0x12},
    // 'l' 108
    {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    // 'm' 109
    {0x00, 0x00, 0x1B, 0x15, 0x15, 0x11, 0x11},
    // 'n' 110
    {0x00, 0x00, 0x0D, 0x13, 0x11, 0x11, 0x11},
    // 'o' 111
    {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E},
    // 'p' 112
    {0x00, 0x00, 0x0D, 0x13, 0x11, 0x11, 0x0F},
    // 'q' 113
    {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x1E},
    // 'r' 114
    {0x00, 0x00, 0x0D, 0x13, 0x01, 0x01, 0x01},
    // 's' 115
    {0x00, 0x00, 0x1E, 0x01, 0x0E, 0x10, 0x0F},
    // 't' 116
    {0x02, 0x02, 0x0F, 0x02, 0x02, 0x02, 0x0C},
    // 'u' 117
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D},
    // 'v' 118
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04},
    // 'w' 119
    {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A},
    // 'x' 120
    {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11},
    // 'y' 121
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x1E, 0x10},
    // 'z' 122
    {0x00, 0x00, 0x1F, 0x08, 0x04, 0x02, 0x1F},
    // '{' 123
    {0x0C, 0x02, 0x04, 0x02, 0x02, 0x02, 0x0C},
    // '|' 124
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    // '}' 125
    {0x06, 0x08, 0x04, 0x08, 0x08, 0x08, 0x06},
    // '~' 126
    {0x00, 0x00, 0x0D, 0x12, 0x00, 0x00, 0x00},
};

} // namespace

// ---------------------------------------------------------------------------
// Built-in bitmap font
// ---------------------------------------------------------------------------

bool Font::initialize(Renderer* renderer) {
    shutdown();
    if (!renderer) {
        return false;
    }
    renderer_ = renderer;
    trueType_ = false;

    constexpr uint32_t rows = 6; // 95 glyphs / 16 columns
    constexpr uint32_t width = kColumns * kPitch;
    constexpr uint32_t height = rows * kCellHeight;

    std::vector<uint8_t> pixels((size_t)width * height * 4, 0);
    for (int glyph = 0; glyph < 95; glyph++) {
        uint32_t col = (uint32_t)(glyph % kColumns);
        uint32_t row = (uint32_t)(glyph / kColumns);
        for (uint32_t gy = 0; gy < kGlyphHeight; gy++) {
            uint8_t bits = kFont5x7[glyph][gy];
            for (uint32_t gx = 0; gx < kGlyphWidth; gx++) {
                if (!(bits & (1u << gx))) {
                    continue;
                }
                uint32_t px = col * kPitch + gx;
                uint32_t py = row * kCellHeight + gy;
                size_t index = ((size_t)py * width + px) * 4;
                pixels[index + 0] = 255;
                pixels[index + 1] = 255;
                pixels[index + 2] = 255;
                pixels[index + 3] = 255;
            }
        }
    }

    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.filterLinear = false;
    atlas_ = renderer_->createTexture(desc, pixels.data());
    if (!atlas_.isValid()) {
        shutdown();
        return false;
    }
    initialized_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// TrueType font loading
// ---------------------------------------------------------------------------

bool Font::loadFromFile(Renderer* renderer, const std::string& path,
                        float fontSize) {
    shutdown();
    if (!renderer) {
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::fprintf(stderr, "Font: cannot open %s\n", path.c_str());
        return false;
    }

    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> ttfData((size_t)fileSize);
    if (!file.read(reinterpret_cast<char*>(ttfData.data()), fileSize)) {
        std::fprintf(stderr, "Font: failed to read %s\n", path.c_str());
        return false;
    }

    renderer_ = renderer;
    bool ok = initTrueTypeAtlas(renderer, ttfData.data(), fontSize);
    if (!ok) {
        shutdown();
        return false;
    }
    return true;
}

bool Font::initTrueTypeAtlas(Renderer* renderer, const uint8_t* ttfData,
                             float fontSize) {
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttfData,
                        stbtt_GetFontOffsetForIndex(ttfData, 0))) {
        std::fprintf(stderr, "Font: failed to parse TTF data\n");
        return false;
    }

    trueType_ = true;
    ttFontSize_ = fontSize;

    float scale = stbtt_ScaleForPixelHeight(&font, fontSize);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    baseline_ = (float)ascent * scale;
    lineHeight_ = ((float)ascent - (float)descent + (float)lineGap) * scale;

    // Rasterize all printable ASCII glyphs (32-126).
    constexpr int FIRST_CHAR = 32;
    constexpr int NUM_CHARS = 95;

    // First pass: measure each glyph to compute atlas dimensions.
    struct RawGlyph {
        uint32_t codepoint;
        int ix0, iy0, ix1, iy1;
        float xadvance;
        int width, height;
    };
    std::vector<RawGlyph> raw(NUM_CHARS);

    int totalWidth = 0;
    int maxHeight = 0;
    for (int i = 0; i < NUM_CHARS; i++) {
        int ch = FIRST_CHAR + i;
        raw[i].codepoint = (uint32_t)ch;
        stbtt_GetCodepointBitmapBox(&font, ch, scale, scale,
                                    &raw[i].ix0, &raw[i].iy0,
                                    &raw[i].ix1, &raw[i].iy1);
        raw[i].width = raw[i].ix1 - raw[i].ix0;
        raw[i].height = raw[i].iy1 - raw[i].iy0;
        int advance;
        stbtt_GetCodepointHMetrics(&font, ch, &advance, nullptr);
        raw[i].xadvance = (float)advance * scale;
        totalWidth += raw[i].width + 1;
        if (raw[i].height > maxHeight) {
            maxHeight = raw[i].height;
        }
    }

    // Pack into a roughly square atlas.
    int cols = 1;
    while (cols * cols < totalWidth) {
        cols++;
    }
    if (cols < 16) {
        cols = 16;
    }
    int atlasW = cols * (maxHeight + 2);
    int atlasH = ((NUM_CHARS + cols - 1) / cols) * (maxHeight + 2);
    // Round up to power-of-two for GPU.
    auto pot = [](int v) {
        int p = 1;
        while (p < v) {
            p <<= 1;
        }
        return p;
    };
    atlasW = pot(atlasW);
    atlasH = pot(atlasH);
    atlasWidth_ = (uint32_t)atlasW;
    atlasHeight_ = (uint32_t)atlasH;

    std::vector<uint8_t> pixels((size_t)atlasW * atlasH * 4, 0);

    // Second pass: rasterize and place glyphs.
    glyphs_.clear();
    int penX = 0;
    int penY = 0;
    int rowHeight = maxHeight + 2;

    for (int i = 0; i < NUM_CHARS; i++) {
        if (penX + raw[i].width + 1 > atlasW) {
            penX = 0;
            penY += rowHeight;
        }

        GlyphInfo g;
        g.x = (uint32_t)penX;
        g.y = (uint32_t)penY;
        g.width = (uint32_t)raw[i].width;
        g.height = (uint32_t)raw[i].height;
        g.xoff = (float)raw[i].ix0;
        g.yoff = (float)raw[i].iy0;
        g.xadvance = raw[i].xadvance;
        glyphs_[raw[i].codepoint] = g;

        if (raw[i].width > 0 && raw[i].height > 0) {
            // Rasterize into a temporary buffer.
            std::vector<uint8_t> bmp((size_t)raw[i].width * raw[i].height);
            stbtt_MakeCodepointBitmap(&font, bmp.data(),
                                      raw[i].width, raw[i].height,
                                      raw[i].width, scale, scale,
                                      FIRST_CHAR + i);

            // Copy into atlas (white pixels with alpha from SDF).
            for (int by = 0; by < raw[i].height; by++) {
                for (int bx = 0; bx < raw[i].width; bx++) {
                    uint8_t a = bmp[(size_t)by * raw[i].width + bx];
                    size_t idx =
                        ((size_t)(penY + by) * atlasW + (penX + bx)) * 4;
                    pixels[idx + 0] = 255;
                    pixels[idx + 1] = 255;
                    pixels[idx + 2] = 255;
                    pixels[idx + 3] = a;
                }
            }
        }

        penX += raw[i].width + 1;
    }

    TextureDesc desc;
    desc.width = atlasWidth_;
    desc.height = atlasHeight_;
    desc.filterLinear = true;
    atlas_ = renderer_->createTexture(desc, pixels.data());
    if (!atlas_.isValid()) {
        return false;
    }
    initialized_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Common
// ---------------------------------------------------------------------------

void Font::shutdown() {
    if (renderer_ && atlas_.isValid()) {
        renderer_->destroyTexture(atlas_);
    }
    renderer_ = nullptr;
    atlas_ = Texture();
    initialized_ = false;
    trueType_ = false;
    glyphs_.clear();
}

bool Font::isInitialized() const {
    return initialized_;
}

void Font::draw(SpriteBatch& batch, const std::string& text,
                const Vector2& position, float glyphHeight,
                const Color& color) {
    if (!initialized_ || text.empty()) {
        return;
    }

    if (trueType_) {
        float cursorX = position.x;
        float cursorY = position.y;

        for (char c : text) {
            if (c == '\n') {
                cursorX = position.x;
                cursorY -= lineHeight_;
                continue;
            }
            auto it = glyphs_.find((uint32_t)c);
            if (it == glyphs_.end()) {
                continue;
            }
            const GlyphInfo& g = it->second;
            if (g.width > 0 && g.height > 0) {
                SpriteRegion region;
                region.texture = atlas_;
                region.x = g.x;
                region.y = g.y;
                region.width = g.width;
                region.height = g.height;
                region.computeUV();

                batch.drawSprite(
                    region,
                    Vector2(cursorX + g.xoff, cursorY + g.yoff),
                    Vector2((float)g.width, (float)g.height),
                    0.0f, {0.0f, 0.0f}, color);
            }
            cursorX += g.xadvance;
        }
        return;
    }

    // Bitmap font path.
    float scale = glyphHeight / (float)kGlyphHeight;
    float cursorX = position.x;
    float cursorY = position.y;
    float lineHeight = (float)kCellHeight * scale;

    for (char c : text) {
        if (c == '\n') {
            cursorX = position.x;
            cursorY -= lineHeight;
            continue;
        }
        int index = c - 32;
        if (index < 0 || index >= 95) {
            continue;
        }
        SpriteRegion region;
        region.texture = atlas_;
        uint32_t col = (uint32_t)(index % kColumns);
        uint32_t row = (uint32_t)(index / kColumns);
        region.x = col * kPitch;
        region.y = row * kCellHeight;
        region.width = kGlyphWidth;
        region.height = kGlyphHeight;
        region.computeUV();

        batch.drawSprite(region, Vector2(cursorX, cursorY),
                         Vector2((float)kGlyphWidth * scale,
                                 (float)kGlyphHeight * scale),
                         0.0f, {0.0f, 0.0f}, color);
        cursorX += (float)kPitch * scale;
    }
}

Vector2 Font::measure(const std::string& text, float glyphHeight) const {
    if (!initialized_ || text.empty()) {
        return Vector2();
    }

    if (trueType_) {
        float width = 0.0f;
        float maxWidth = 0.0f;
        int lines = 1;
        for (char c : text) {
            if (c == '\n') {
                maxWidth = std::max(maxWidth, width);
                width = 0.0f;
                lines++;
                continue;
            }
            auto it = glyphs_.find((uint32_t)c);
            if (it == glyphs_.end()) {
                continue;
            }
            width += it->second.xadvance;
        }
        maxWidth = std::max(maxWidth, width);
        return Vector2(maxWidth, lineHeight_ * (float)lines);
    }

    // Bitmap font path.
    float scale = glyphHeight / (float)kGlyphHeight;
    float lineHeight = (float)kCellHeight * scale;
    float width = 0.0f;
    float maxWidth = 0.0f;
    int lines = 1;
    for (char c : text) {
        if (c == '\n') {
            maxWidth = std::max(maxWidth, width);
            width = 0.0f;
            lines++;
            continue;
        }
        int index = c - 32;
        if (index < 0 || index >= 95) {
            continue;
        }
        width += (float)kPitch * scale;
    }
    maxWidth = std::max(maxWidth, width);
    return Vector2(maxWidth, lineHeight * (float)lines);
}

Texture Font::texture() const {
    return atlas_;
}

uint32_t Font::glyphWidth() const {
    return glyphWidth_;
}

uint32_t Font::glyphHeight() const {
    return glyphHeight_;
}

} // namespace ion
