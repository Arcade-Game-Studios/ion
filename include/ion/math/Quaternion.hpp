#pragma once

#include <ion/math/MathUtils.hpp>
#include <ion/math/Matrix4.hpp>
#include <ion/math/Vector3.hpp>

#include <cmath>

namespace ion {

struct Quaternion;
inline constexpr Quaternion operator*(const Quaternion& a, const Quaternion& b);

//
// Unit quaternion for 3D rotations, stored (x, y, z, w) like glTF. Rotations
// compose like matrices: (a * b) rotates by b first, then a.
//
struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quaternion() = default;
    constexpr Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    static constexpr Quaternion identity() { return Quaternion(); }

    // Right-handed rotation of `angle` radians around `axis`.
    static Quaternion fromAxisAngle(const Vector3& axis, float angle) {
        Vector3 a = axis.normalized();
        if (a.isZero()) {
            return identity();
        }
        float half = angle * 0.5f;
        float s = std::sin(half);
        return Quaternion(a.x * s, a.y * s, a.z * s, std::cos(half));
    }

    // Euler angles in radians: pitch around X, yaw around Y, roll around Z.
    // Applied in the order roll, pitch, then yaw (yaw * pitch * roll), which
    // matches Matrix4::compose and a typical FPS camera.
    static Quaternion fromEuler(float pitch, float yaw, float roll) {
        return fromAxisAngle(Vector3::unitY(), yaw) *
               fromAxisAngle(Vector3::unitX(), pitch) *
               fromAxisAngle(Vector3::unitZ(), roll);
    }

    static Quaternion fromEuler(const Vector3& eulerRadians) {
        return fromEuler(eulerRadians.x, eulerRadians.y, eulerRadians.z);
    }

    // Shortest rotation taking unit direction `from` onto `to`.
    static Quaternion fromTo(const Vector3& from, const Vector3& to) {
        Vector3 f = from.normalized();
        Vector3 t = to.normalized();
        float d = Vector3::dot(f, t);
        if (d >= 1.0f - 1e-6f) {
            return identity();
        }
        if (d <= -1.0f + 1e-6f) {
            // Opposite: rotate PI around any axis perpendicular to `from`.
            Vector3 axis = Vector3::cross(Vector3::unitX(), f);
            if (axis.lengthSquared() < 1e-6f) {
                axis = Vector3::cross(Vector3::unitY(), f);
            }
            return fromAxisAngle(axis, PI);
        }
        Vector3 c = Vector3::cross(f, t);
        return Quaternion(c.x, c.y, c.z, 1.0f + d).normalized();
    }

    // Orientation whose forward (-Z) points along `forward`, with +Y as close
    // to `up` as possible.
    static Quaternion lookRotation(const Vector3& forward,
                                   const Vector3& up = Vector3::unitY()) {
        Vector3 f = forward.normalized();
        Vector3 r = Vector3::cross(f, up).normalized();
        if (r.isZero()) {
            return fromTo(Vector3::forward(), f);
        }
        Vector3 u = Vector3::cross(r, f);
        // Rotation matrix columns: right, up, back (-forward).
        Matrix4 basis = Matrix4::identity();
        basis(0, 0) = r.x; basis(1, 0) = r.y; basis(2, 0) = r.z;
        basis(0, 1) = u.x; basis(1, 1) = u.y; basis(2, 1) = u.z;
        basis(0, 2) = -f.x; basis(1, 2) = -f.y; basis(2, 2) = -f.z;
        return fromMatrix(basis);
    }

    // Extracts the rotation from the upper 3x3 of a pure rotation matrix.
    static Quaternion fromMatrix(const Matrix4& m) {
        float trace = m(0, 0) + m(1, 1) + m(2, 2);
        Quaternion q;
        if (trace > 0.0f) {
            float s = std::sqrt(trace + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (m(2, 1) - m(1, 2)) / s;
            q.y = (m(0, 2) - m(2, 0)) / s;
            q.z = (m(1, 0) - m(0, 1)) / s;
        } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
            float s = std::sqrt(1.0f + m(0, 0) - m(1, 1) - m(2, 2)) * 2.0f;
            q.w = (m(2, 1) - m(1, 2)) / s;
            q.x = 0.25f * s;
            q.y = (m(0, 1) + m(1, 0)) / s;
            q.z = (m(0, 2) + m(2, 0)) / s;
        } else if (m(1, 1) > m(2, 2)) {
            float s = std::sqrt(1.0f + m(1, 1) - m(0, 0) - m(2, 2)) * 2.0f;
            q.w = (m(0, 2) - m(2, 0)) / s;
            q.x = (m(0, 1) + m(1, 0)) / s;
            q.y = 0.25f * s;
            q.z = (m(1, 2) + m(2, 1)) / s;
        } else {
            float s = std::sqrt(1.0f + m(2, 2) - m(0, 0) - m(1, 1)) * 2.0f;
            q.w = (m(1, 0) - m(0, 1)) / s;
            q.x = (m(0, 2) + m(2, 0)) / s;
            q.y = (m(1, 2) + m(2, 1)) / s;
            q.z = 0.25f * s;
        }
        return q.normalized();
    }

    constexpr float lengthSquared() const { return x * x + y * y + z * z + w * w; }
    float length() const { return std::sqrt(lengthSquared()); }

    Quaternion normalized() const {
        float len = length();
        if (len <= 0.0f) {
            return identity();
        }
        return Quaternion(x / len, y / len, z / len, w / len);
    }

    constexpr Quaternion conjugate() const { return Quaternion(-x, -y, -z, w); }

    Quaternion inverse() const {
        float lenSq = lengthSquared();
        if (lenSq <= 0.0f) {
            return identity();
        }
        return Quaternion(-x / lenSq, -y / lenSq, -z / lenSq, w / lenSq);
    }

    // Rotates a vector (assumes a unit quaternion).
    constexpr Vector3 rotate(const Vector3& v) const {
        // v' = v + 2w(q x v) + 2(q x (q x v))
        Vector3 q(x, y, z);
        Vector3 t = Vector3::cross(q, v) * 2.0f;
        return v + t * w + Vector3::cross(q, t);
    }

    Vector3 forward() const { return rotate(Vector3::forward()); }
    Vector3 right() const { return rotate(Vector3::right()); }
    Vector3 up() const { return rotate(Vector3::up()); }

    // Rotation matrix (assumes a unit quaternion).
    Matrix4 toMatrix() const {
        Matrix4 mat = Matrix4::identity();
        mat(0, 0) = 1 - 2 * (y * y + z * z);
        mat(1, 0) = 2 * (x * y + z * w);
        mat(2, 0) = 2 * (x * z - y * w);
        mat(0, 1) = 2 * (x * y - z * w);
        mat(1, 1) = 1 - 2 * (x * x + z * z);
        mat(2, 1) = 2 * (y * z + x * w);
        mat(0, 2) = 2 * (x * z + y * w);
        mat(1, 2) = 2 * (y * z - x * w);
        mat(2, 2) = 1 - 2 * (x * x + y * y);
        return mat;
    }

    // Returns (pitch, yaw, roll) in radians; inverse of fromEuler away from
    // the +/-90 degree pitch singularity.
    Vector3 toEuler() const {
        Matrix4 m = toMatrix();
        float pitch = std::asin(clamp(-m(1, 2), -1.0f, 1.0f));
        float yaw, roll;
        if (std::fabs(m(1, 2)) < 0.9999f) {
            yaw = std::atan2(m(0, 2), m(2, 2));
            roll = std::atan2(m(1, 0), m(1, 1));
        } else {
            yaw = std::atan2(-m(2, 0), m(0, 0));
            roll = 0.0f;
        }
        return Vector3(pitch, yaw, roll);
    }

    // Rotation angle in radians, in [0, 2PI).
    float angle() const { return 2.0f * std::acos(clamp(w, -1.0f, 1.0f)); }

    static constexpr float dot(const Quaternion& a, const Quaternion& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    // Smallest angle in radians between two orientations.
    static float angleBetween(const Quaternion& a, const Quaternion& b) {
        float d = std::fabs(dot(a, b));
        return 2.0f * std::acos(clamp(d, 0.0f, 1.0f));
    }

    // Normalized linear interpolation along the shortest path. Cheap and
    // good for small steps.
    static Quaternion nlerp(const Quaternion& a, const Quaternion& b, float t) {
        float s = dot(a, b) < 0.0f ? -1.0f : 1.0f;
        return Quaternion(a.x + (b.x * s - a.x) * t, a.y + (b.y * s - a.y) * t,
                          a.z + (b.z * s - a.z) * t, a.w + (b.w * s - a.w) * t)
            .normalized();
    }

    // Spherical linear interpolation along the shortest path (constant
    // angular velocity).
    static Quaternion slerp(const Quaternion& a, const Quaternion& b, float t) {
        float d = dot(a, b);
        Quaternion end = b;
        if (d < 0.0f) {
            d = -d;
            end = Quaternion(-b.x, -b.y, -b.z, -b.w);
        }
        if (d > 0.9995f) {
            return nlerp(a, end, t);
        }
        float theta = std::acos(d);
        float sinTheta = std::sin(theta);
        float wa = std::sin((1.0f - t) * theta) / sinTheta;
        float wb = std::sin(t * theta) / sinTheta;
        return Quaternion(a.x * wa + end.x * wb, a.y * wa + end.y * wb,
                          a.z * wa + end.z * wb, a.w * wa + end.w * wb);
    }
};

inline constexpr Quaternion operator*(const Quaternion& a, const Quaternion& b) {
    return Quaternion(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

inline constexpr Vector3 operator*(const Quaternion& q, const Vector3& v) {
    return q.rotate(v);
}

inline constexpr bool operator==(const Quaternion& a, const Quaternion& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

inline constexpr bool operator!=(const Quaternion& a, const Quaternion& b) {
    return !(a == b);
}

// Translation * rotation * scale.
inline Matrix4 composeTransform(const Vector3& translation,
                                const Quaternion& rotation,
                                const Vector3& scale) {
    return Matrix4::translation(translation) * rotation.toMatrix() *
           Matrix4::scale(scale);
}

} // namespace ion
