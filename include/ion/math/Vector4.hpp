#pragma once

#include <ion/math/MathUtils.hpp>
#include <ion/math/Vector3.hpp>

#include <cmath>

namespace ion {

struct Vector4 {
    float x;
    float y;
    float z;
    float w;

    constexpr Vector4() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    constexpr Vector4(float x, float y, float z, float w = 1.0f)
        : x(x), y(y), z(z), w(w) {}
    constexpr Vector4(const Vector3& xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}

    constexpr float& operator[](int i) {
        return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w));
    }
    constexpr float operator[](int i) const {
        return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w));
    }

    constexpr Vector3 xyz() const { return Vector3(x, y, z); }
    constexpr Vector2 xy() const { return Vector2(x, y); }

    constexpr Vector4 operator-() const { return Vector4(-x, -y, -z, -w); }

    constexpr Vector4& operator+=(const Vector4& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this;
    }

    constexpr Vector4& operator-=(const Vector4& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this;
    }

    constexpr Vector4& operator*=(const Vector4& other) {
        x *= other.x;
        y *= other.y;
        z *= other.z;
        w *= other.w;
        return *this;
    }

    constexpr Vector4& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    constexpr Vector4& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;
        return *this;
    }

    constexpr float lengthSquared() const {
        return x * x + y * y + z * z + w * w;
    }

    float length() const {
        return std::sqrt(lengthSquared());
    }

    Vector4 normalized() const {
        float len = length();
        if (len <= 0.0f) {
            return *this;
        }
        return Vector4(x / len, y / len, z / len, w / len);
    }

    static constexpr float dot(const Vector4& a, const Vector4& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    static constexpr Vector4 lerp(const Vector4& a, const Vector4& b, float t) {
        return Vector4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                       a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    static bool approxEqual(const Vector4& a, const Vector4& b,
                            float epsilon = EPSILON) {
        return std::fabs(a.x - b.x) <= epsilon &&
               std::fabs(a.y - b.y) <= epsilon &&
               std::fabs(a.z - b.z) <= epsilon &&
               std::fabs(a.w - b.w) <= epsilon;
    }
};

inline constexpr Vector4 operator+(Vector4 lhs, const Vector4& rhs) {
    return lhs += rhs;
}

inline constexpr Vector4 operator-(Vector4 lhs, const Vector4& rhs) {
    return lhs -= rhs;
}

inline constexpr Vector4 operator*(Vector4 lhs, const Vector4& rhs) {
    return lhs *= rhs;
}

inline constexpr Vector4 operator*(Vector4 lhs, float scalar) {
    return lhs *= scalar;
}

inline constexpr Vector4 operator*(float scalar, Vector4 rhs) {
    return rhs *= scalar;
}

inline constexpr Vector4 operator/(Vector4 lhs, float scalar) {
    return lhs /= scalar;
}

inline constexpr bool operator==(const Vector4& a, const Vector4& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

inline constexpr bool operator!=(const Vector4& a, const Vector4& b) {
    return !(a == b);
}

} // namespace ion
