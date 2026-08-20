#pragma once

#include <ion/math/Vector2.hpp>
#include <ion/render/Color.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/Texture.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ion {

//
// Text rendering. Two modes:
//
//  1. Built-in bitmap font: call initialize() — a 5x7 pixel font, no assets.
//  2. TrueType font: call loadFromFile() with a .ttf path and desired pixel
//     size. Proportional spacing, smooth rendering via stb_truetype.
//
// draw() and measure() work identically in both modes.
//
class Font {
public:
    Font() = default;

    // Built-in 5x7 bitmap font (no external files needed).
    bool initialize(Renderer* renderer);

    // Load a TrueType font from a .ttf file. fontSize is the pixel height
    // of uppercase letters. Returns false if the file can't be loaded.
    bool loadFromFile(Renderer* renderer, const std::string& path,
                      float fontSize = 16.0f);

    void shutdown();
    bool isInitialized() const;

    // Draws text at position (top-left of first glyph). glyphHeight
    // controls the scale for bitmap fonts; for TTF fonts it's ignored
    // (the size from loadFromFile is used).
    void draw(SpriteBatch& batch, const std::string& text,
              const Vector2& position, float glyphHeight,
              const Color& color = Color::white());

    // Returns the bounding box of text. For bitmap fonts, glyphHeight
    // scales the result; for TTF fonts it's ignored.
    Vector2 measure(const std::string& text, float glyphHeight) const;

    Texture texture() const;
    uint32_t glyphWidth() const;
    uint32_t glyphHeight() const;

    bool isTrueType() const { return trueType_; }

private:
    bool initTrueTypeAtlas(Renderer* renderer, const uint8_t* ttfData,
                           float fontSize);

    Renderer* renderer_ = nullptr;
    Texture atlas_;
    bool initialized_ = false;
    bool trueType_ = false;

    // Bitmap font metrics (5x7 built-in).
    uint32_t glyphWidth_ = 5;
    uint32_t glyphHeight_ = 7;
    uint32_t pitch_ = 6;
    uint32_t columns_ = 16;

    // TTF glyph metrics.
    struct GlyphInfo {
        uint32_t x = 0;       // atlas x (pixels)
        uint32_t y = 0;       // atlas y (pixels)
        uint32_t width = 0;   // glyph bitmap width
        uint32_t height = 0;  // glyph bitmap height
        float xoff = 0.0f;    // offset from cursor to left edge
        float yoff = 0.0f;    // offset from baseline to top edge
        float xadvance = 0.0f;// horizontal advance to next glyph
    };
    std::unordered_map<uint32_t, GlyphInfo> glyphs_;
    float ttFontSize_ = 0.0f;
    float lineHeight_ = 0.0f;
    float baseline_ = 0.0f;
    uint32_t atlasWidth_ = 0;
    uint32_t atlasHeight_ = 0;
};

} // namespace ion
