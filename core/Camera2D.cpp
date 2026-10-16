#include <ion/render/Camera2D.hpp>

#include <algorithm>
#include <cmath>

namespace ion {

void Camera2D::setPosition(const Vector2& position) {
    position_ = position;
}

void Camera2D::setZoom(float zoom) {
    zoom_ = std::max(0.0001f, zoom);
}

void Camera2D::setRotation(float radians) {
    rotation_ = radians;
}

void Camera2D::setViewport(uint32_t width, uint32_t height) {
    viewportWidth_ = std::max(1u, width);
    viewportHeight_ = std::max(1u, height);
}

const Vector2& Camera2D::position() const {
    return position_;
}

float Camera2D::zoom() const {
    return zoom_;
}

float Camera2D::rotation() const {
    return rotation_;
}

uint32_t Camera2D::viewportWidth() const {
    return viewportWidth_;
}

uint32_t Camera2D::viewportHeight() const {
    return viewportHeight_;
}

Matrix4 Camera2D::viewProjection() const {
    float w = (float)viewportWidth_;
    float h = (float)viewportHeight_;
    float invZoom = 1.0f / std::max(zoom_, 0.0001f);

    Matrix4 projection = Matrix4::orthographic(
        -w * 0.5f * invZoom, w * 0.5f * invZoom, -h * 0.5f * invZoom,
        h * 0.5f * invZoom, -1.0f, 1.0f);

    // Translate the camera to the origin first, then rotate about it. (The
    // reverse order rotated the world about the world origin instead.)
    Matrix4 view = Matrix4::translation(
        Vector3(-position_.x, -position_.y, 0.0f));
    if (rotation_ != 0.0f) {
        view = Matrix4::rotationZ(-rotation_) * view;
    }
    return projection * view;
}

Vector2 Camera2D::screenToWorld(float screenX, float screenY) const {
    // Must invert viewProjection(): zoom z shows z pixels per world unit.
    float invZoom = 1.0f / std::max(zoom_, 0.0001f);
    Vector2 delta((screenX - (float)viewportWidth_ * 0.5f) * invZoom,
                  ((float)viewportHeight_ * 0.5f - screenY) * invZoom);
    if (rotation_ != 0.0f) {
        float c = std::cos(rotation_);
        float s = std::sin(rotation_);
        delta = Vector2(delta.x * c - delta.y * s,
                        delta.x * s + delta.y * c);
    }
    return position_ + delta;
}

Vector2 Camera2D::worldToScreen(const Vector2& world) const {
    Vector2 delta = world - position_;
    if (rotation_ != 0.0f) {
        float c = std::cos(rotation_);
        float s = std::sin(rotation_);
        delta = Vector2(delta.x * c + delta.y * s,
                        -delta.x * s + delta.y * c);
    }
    float z = std::max(zoom_, 0.0001f);
    return Vector2((float)viewportWidth_ * 0.5f + delta.x * z,
                   (float)viewportHeight_ * 0.5f - delta.y * z);
}

Rect Camera2D::visibleBounds() const {
    float halfW = (float)viewportWidth_ * 0.5f / std::max(zoom_, 0.0001f);
    float halfH = (float)viewportHeight_ * 0.5f / std::max(zoom_, 0.0001f);
    if (rotation_ != 0.0f) {
        // Axis-aligned bounds of the rotated view rectangle.
        float c = std::fabs(std::cos(rotation_));
        float s = std::fabs(std::sin(rotation_));
        float w = halfW * c + halfH * s;
        float h = halfW * s + halfH * c;
        halfW = w;
        halfH = h;
    }
    return Rect(position_.x - halfW, position_.y - halfH, halfW * 2.0f, halfH * 2.0f);
}

void Camera2D::move(const Vector2& delta) {
    position_ += delta;
}

void Camera2D::follow(const Vector2& target, float rate, float deltaTime) {
    float t = 1.0f - std::exp(-std::max(rate, 0.0f) * deltaTime);
    position_ = Vector2::lerp(position_, target, t);
}

void Camera2D::clampTo(const Rect& worldBounds) {
    Rect view = visibleBounds();
    float halfW = view.width * 0.5f;
    float halfH = view.height * 0.5f;
    // If the bounds are smaller than the view, center on them.
    position_.x = worldBounds.width <= view.width
                      ? worldBounds.center().x
                      : clamp(position_.x, worldBounds.left() + halfW, worldBounds.right() - halfW);
    position_.y = worldBounds.height <= view.height
                      ? worldBounds.center().y
                      : clamp(position_.y, worldBounds.bottom() + halfH, worldBounds.top() - halfH);
}

} // namespace ion
