#pragma once

#include <ion/math/Matrix4.hpp>
#include <ion/math/Vector3.hpp>

#include <cstdint>

namespace ion {

//
// First-person 3D camera. Orientation is stored as yaw (rotation around the
// world +Y axis) and pitch (rotation around the camera's right axis), so the
// camera is always upright (roll == 0). At yaw == pitch == 0 the camera looks
// down the -Z axis.
//
// view() and projection() are recomputed from the current parameters, so the
// camera can be moved and aimed freely between frames. The default projection
// is perspective with a 60 degree fov, aspect 1.0, near plane 0.1 and far
// plane 1000.0.
//
class Camera {
public:
    Camera() = default;

    // World-space position.
    void setPosition(const Vector3& position);
    const Vector3& position() const;

    // Orients the camera so it looks at the given world-space point. The
    // camera stays upright (world +Y up). The two-argument form is kept for
    // backwards compatibility; the up vector is ignored because a yaw/pitch
    // camera cannot roll.
    void lookAt(const Vector3& target);
    void lookAt(const Vector3& target, const Vector3& up);

    // Orientation in radians. Pitch is clamped to +/- ~89 degrees to avoid
    // gimbal lock when computing the view basis.
    void setYaw(float radians);
    void setPitch(float radians);
    float yaw() const;
    float pitch() const;

    // Local-space unit axes. forward() is the look direction, right() points
    // to the camera's right, up() completes the right-handed basis.
    Vector3 forward() const;
    Vector3 right() const;
    Vector3 up() const;

    // Moves the camera in its local space: +x strafes right, +y moves up,
    // +z moves forward (toward the look direction).
    void move(const Vector3& localOffset);

    // Perspective projection parameters. setViewport derives the aspect ratio
    // from the window size.
    void setFov(float fovRadians);
    void setViewport(uint32_t width, uint32_t height);
    void setNear(float near);
    void setFar(float far);
    float fov() const;
    float aspect() const;
    float near() const;
    float far() const;
    uint32_t viewportWidth() const;
    uint32_t viewportHeight() const;

    // Perspective projection from fov/aspect/near/far. Replaces the current
    // projection mode.
    void setPerspective(float fovRadians, float aspect, float near, float far);

    // Orthographic projection. Replaces the current projection mode.
    void setOrthographic(float left, float right, float bottom, float top,
                         float near, float far);

    const Matrix4& view() const;
    const Matrix4& projection() const;
    Matrix4 viewProjection() const;

private:
    Vector3 position_;
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;

    bool orthographic_ = false;
    float fov_ = 3.14159265f / 3.0f;
    float aspect_ = 1.0f;
    float near_ = 0.1f;
    float far_ = 1000.0f;
    float orthoLeft_ = -1.0f;
    float orthoRight_ = 1.0f;
    float orthoBottom_ = -1.0f;
    float orthoTop_ = 1.0f;
    float orthoNear_ = -1.0f;
    float orthoFar_ = 1.0f;
    uint32_t viewportWidth_ = 1;
    uint32_t viewportHeight_ = 1;

    mutable Matrix4 view_;
    mutable Matrix4 projection_;
};

} // namespace ion
