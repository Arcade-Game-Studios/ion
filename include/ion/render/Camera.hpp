#pragma once

#include <ion/math/Matrix4.hpp>
#include <ion/math/Vector3.hpp>

namespace ion {

class Camera {
public:
    Camera() = default;

    void setPosition(const Vector3& position);
    void lookAt(const Vector3& target, const Vector3& up);

    void setOrthographic(float left, float right, float bottom, float top,
                         float near, float far);
    void setPerspective(float fovRadians, float aspect, float near, float far);

    const Vector3& position() const;
    const Matrix4& view() const;
    const Matrix4& projection() const;

private:
    Vector3 position_;
    Matrix4 view_;
    Matrix4 projection_;
};

} // namespace ion
