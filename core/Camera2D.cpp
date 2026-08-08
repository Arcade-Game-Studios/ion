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

    Matrix4 view = Matrix4::translation(
        Vector3(-position_.x, -position_.y, 0.0f));
    if (rotation_ != 0.0f) {
        view = view * Matrix4::rotationZ(-rotation_);
    }
    return projection * view;
}

Vector2 Camera2D::screenToWorld(float screenX, float screenY) const {
    float z = std::max(zoom_, 0.0001f);
    Vector2 delta((screenX - (float)viewportWidth_ * 0.5f) * z,
                  ((float)viewportHeight_ * 0.5f - screenY) * z);
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
    float invZoom = 1.0f / std::max(zoom_, 0.0001f);
    return Vector2((float)viewportWidth_ * 0.5f + delta.x * invZoom,
                   (float)viewportHeight_ * 0.5f - delta.y * invZoom);
}

} // namespace ion
