#include <ion/render/Camera.hpp>

namespace ion {

void Camera::setPosition(const Vector3& position) {
    position_ = position;
}

void Camera::lookAt(const Vector3& target, const Vector3& up) {
    view_ = Matrix4::lookAt(position_, target, up);
}

void Camera::setOrthographic(float left, float right, float bottom, float top,
                             float near, float far) {
    projection_ = Matrix4::orthographic(left, right, bottom, top, near, far);
}

void Camera::setPerspective(float fovRadians, float aspect, float near,
                            float far) {
    projection_ = Matrix4::perspective(fovRadians, aspect, near, far);
}

const Vector3& Camera::position() const {
    return position_;
}

const Matrix4& Camera::view() const {
    return view_;
}

const Matrix4& Camera::projection() const {
    return projection_;
}

} // namespace ion
