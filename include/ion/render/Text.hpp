#pragma once

#include <ion/math/Vector2.hpp>
#include <ion/render/Color.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/Texture.hpp>

#include <cstdint>
#include <string>

namespace ion {

//
// Bitmap font text rendering using a built-in 5x7 pixel font. initialize()
// builds a glyph atlas texture (96 glyphs, ASCII 32-126) on the GPU;
// draw() and measure() use the given SpriteBatch. '\\n' inserts a line
// break. This needs no external assets or font rasterizer.
//
class Font {
public:
    Font() = default;

    bool initialize(Renderer* renderer);
    void shutdown();
    bool isInitialized() const;

    // Draws text with each glyph scaled to glyphHeight pixels. position is
    // the top-left of the first glyph.
    void draw(SpriteBatch& batch, const std::string& text,
              const Vector2& position, float glyphHeight,
              const Color& color = Color::white());

    // Returns the bounding box (width/height in pixels) of text at the
    // given glyph height.
    Vector2 measure(const std::string& text, float glyphHeight) const;

    Texture texture() const;
    uint32_t glyphWidth() const;
    uint32_t glyphHeight() const;

private:
    Renderer* renderer_ = nullptr;
    Texture atlas_;
    uint32_t glyphWidth_ = 5;
    uint32_t glyphHeight_ = 7;
    uint32_t pitch_ = 6;
    uint32_t columns_ = 16;
    bool initialized_ = false;
};

} // namespace ion
