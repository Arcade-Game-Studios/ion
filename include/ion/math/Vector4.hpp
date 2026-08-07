#pragma once

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

    Vector4& operator+=(const Vector4& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this;
    }

    Vector4& operator-=(const Vector4& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this;
    }

    Vector4& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    Vector4& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;
        return *this;
    }

    float lengthSquared() const {
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
};

inline constexpr Vector4 operator+(Vector4 lhs, const Vector4& rhs) {
    return lhs += rhs;
}

inline constexpr Vector4 operator-(Vector4 lhs, const Vector4& rhs) {
    return lhs -= rhs;
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
