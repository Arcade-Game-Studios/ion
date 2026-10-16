#pragma once

#include <ion/math/Geometry.hpp>
#include <ion/math/Matrix4.hpp>
#include <ion/math/Vector2.hpp>

#include <cstdint>

namespace ion {

//
// Simple 2D camera. World coordinates are in pixels with +y pointing up.
// position() is the world point at the center of the screen. zoom() is
// pixels per world unit: zoom 1.0 maps one world unit to one pixel, zoom 2.0
// magnifies so each world unit covers two pixels.
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

    // World-space axis-aligned rectangle covering everything on screen
    // (accounts for zoom and rotation). Useful for culling.
    Rect visibleBounds() const;

    void move(const Vector2& delta);

    // Smoothly moves toward target; rate is how quickly (higher = snappier),
    // independent of frame rate.
    void follow(const Vector2& target, float rate, float deltaTime);

    // Keeps the view inside worldBounds (centers on it if it's smaller than
    // the view). Rotation is accounted for via visibleBounds().
    void clampTo(const Rect& worldBounds);

private:
    Vector2 position_;
    float zoom_ = 1.0f;
    float rotation_ = 0.0f;
    uint32_t viewportWidth_ = 1;
    uint32_t viewportHeight_ = 1;
};

} // namespace ion
