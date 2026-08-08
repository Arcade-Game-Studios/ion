#pragma once

#include <ion/math/Matrix4.hpp>
#include <ion/math/Vector2.hpp>

#include <cstdint>

namespace ion {

//
// Simple 2D camera. World coordinates are in pixels with +y pointing up.
// position() is the world point at the center of the screen. zoom() scales
// world units per pixel: zoom 1.0 means one world unit maps to one pixel.
//
class Camera2D {
public:
    Camera2D() = default;

    void setPosition(const Vector2& position);
    void setZoom(float zoom);
    void setRotation(float radians);
    void setViewport(uint32_t width, uint32_t height);

    const Vector2& position() const;
    float zoom() const;
    float rotation() const;
    uint32_t viewportWidth() const;
    uint32_t viewportHeight() const;

    // Converts a point in the orthographic camera space.
    Matrix4 viewProjection() const;

    // Screen (pixel) position -> world position. Screen origin is the
    // top-left of the window, +y points down.
    Vector2 screenToWorld(float screenX, float screenY) const;

    // World position -> screen (pixel) position. Screen origin is the
    // top-left of the window, +y points down.
    Vector2 worldToScreen(const Vector2& world) const;

private:
    Vector2 position_;
    float zoom_ = 1.0f;
    float rotation_ = 0.0f;
    uint32_t viewportWidth_ = 1;
    uint32_t viewportHeight_ = 1;
};

} // namespace ion
