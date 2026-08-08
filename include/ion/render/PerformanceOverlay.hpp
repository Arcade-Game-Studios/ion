#pragma once

#include <ion/core/Timer.hpp>
#include <ion/render/Camera2D.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/Text.hpp>

#include <cstdint>
#include <string>

namespace ion {

//
// Performance overlay: draws live FPS, frame time, draw call and triangle
// counts in the top-left corner of the window. The overlay is enabled by
// default; the game can disable it with setEnabled(false). It owns its own
// Font/SpriteBatch, so call render() between renderer.beginFrame() and
// renderer.endFrame() after the game's own drawing.
//
// Usage:
//   ion::PerformanceOverlay overlay;
//   overlay.initialize(&renderer, window.width(), window.height());
//   while (...) {
//       ...
//       renderer.beginFrame();
//       ...
//       overlay.render();
//       renderer.endFrame();
//   }
//
class PerformanceOverlay {
public:
    PerformanceOverlay() = default;

    bool initialize(Renderer* renderer, uint32_t viewportWidth,
                    uint32_t viewportHeight);
    void shutdown();
    bool isInitialized() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Call when the window is resized so the overlay stays in the corner.
    void setViewport(uint32_t width, uint32_t height);

    // Glyph height of the overlay text (default 14 pixels).
    void setFontSize(float glyphHeight);

    // Draws the overlay using its own sprite batch. Must be called while the
    // renderer is inside a frame (after beginFrame, before endFrame).
    void render();

private:
    std::string statsText_() const;

    Renderer* renderer_ = nullptr;
    SpriteBatch batch_;
    Font font_;
    Camera2D camera_;
    Timer frameTimer_;
    float fontSize_ = 14.0f;
    float lastFrameMs_ = 0.0f;
    float fps_ = 0.0f;
    bool hasPreviousFrame_ = false;
    bool enabled_ = true;
    bool initialized_ = false;
};

} // namespace ion
