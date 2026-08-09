#include <ion/render/Camera.hpp>

#include <algorithm>
#include <cmath>

namespace ion {

namespace {
constexpr float kPitchLimit = 3.14159265f * 0.5f - 0.01f;
} // namespace

void Camera::setPosition(const Vector3& position) {
    position_ = position;
}

const Vector3& Camera::position() const {
    return position_;
}

void Camera::lookAt(const Vector3& target) {
    Vector3 d = (target - position_).normalized();
    pitch_ = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    yaw_ = std::atan2(-d.x, -d.z);
}

void Camera::lookAt(const Vector3& target, const Vector3& up) {
    (void)up;
    lookAt(target);
}

void Camera::setYaw(float radians) {
    yaw_ = radians;
}

void Camera::setPitch(float radians) {
    pitch_ = std::clamp(radians, -kPitchLimit, kPitchLimit);
}

float Camera::yaw() const {
    return yaw_;
}

float Camera::pitch() const {
    return pitch_;
}

Vector3 Camera::forward() const {
    float cp = std::cos(pitch_);
    return Vector3(-std::sin(yaw_) * cp, std::sin(pitch_),
                   -std::cos(yaw_) * cp)
        .normalized();
}

Vector3 Camera::right() const {
    return Vector3::cross(forward(), Vector3(0.0f, 1.0f, 0.0f)).normalized();
}

Vector3 Camera::up() const {
    return Vector3::cross(right(), forward());
}

void Camera::move(const Vector3& localOffset) {
    position_ += right() * localOffset.x + up() * localOffset.y +
                 forward() * localOffset.z;
}

void Camera::setFov(float fovRadians) {
    fov_ = std::max(fovRadians, 0.001f);
}

void Camera::setViewport(uint32_t width, uint32_t height) {
    viewportWidth_ = std::max(1u, width);
    viewportHeight_ = std::max(1u, height);
    aspect_ = (float)viewportWidth_ / (float)viewportHeight_;
}

void Camera::setNear(float near) {
    near_ = std::max(near, 0.0001f);
}

void Camera::setFar(float far) {
    far_ = std::max(far, near_);
}

float Camera::fov() const {
    return fov_;
}

float Camera::aspect() const {
    return aspect_;
}

float Camera::near() const {
    return near_;
}

float Camera::far() const {
    return far_;
}

uint32_t Camera::viewportWidth() const {
    return viewportWidth_;
}

uint32_t Camera::viewportHeight() const {
    return viewportHeight_;
}

void Camera::setPerspective(float fovRadians, float aspect, float near,
                            float far) {
    orthographic_ = false;
    fov_ = std::max(fovRadians, 0.001f);
    aspect_ = std::max(aspect, 0.001f);
    near_ = std::max(near, 0.0001f);
    far_ = std::max(far, near_);
}

void Camera::setOrthographic(float left, float right, float bottom, float top,
                             float near, float far) {
    orthographic_ = true;
    orthoLeft_ = left;
    orthoRight_ = right;
    orthoBottom_ = bottom;
    orthoTop_ = top;
    orthoNear_ = near;
    orthoFar_ = far;
}

const Matrix4& Camera::view() const {
    view_ = Matrix4::lookAt(position_, position_ + forward(), up());
    return view_;
}

const Matrix4& Camera::projection() const {
    if (orthographic_) {
        projection_ = Matrix4::orthographic(
            orthoLeft_, orthoRight_, orthoBottom_, orthoTop_, orthoNear_,
            orthoFar_);
    } else {
        projection_ = Matrix4::perspective(fov_, aspect_, near_, far_);
    }
    return projection_;
}

Matrix4 Camera::viewProjection() const {
    return projection() * view();
}

} // namespace ion
