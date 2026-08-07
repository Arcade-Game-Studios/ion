#pragma once

#include <cmath>

namespace ion {

struct Vector3 {
    float x;
    float y;
    float z;

    constexpr Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
    constexpr Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector3& operator+=(const Vector3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vector3& operator-=(const Vector3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    Vector3& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    Vector3& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }

    float lengthSquared() const {
        return x * x + y * y + z * z;
    }

    float length() const {
        return std::sqrt(lengthSquared());
    }

    Vector3 normalized() const {
        float len = length();
        if (len <= 0.0f) {
            return *this;
        }
        return Vector3(x / len, y / len, z / len);
    }

    static float dot(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static Vector3 cross(const Vector3& a, const Vector3& b) {
        return Vector3(a.y * b.z - a.z * b.y,
                       a.z * b.x - a.x * b.z,
                       a.x * b.y - a.y * b.x);
    }
};

inline constexpr Vector3 operator+(Vector3 lhs, const Vector3& rhs) {
    return lhs += rhs;
}

inline constexpr Vector3 operator-(Vector3 lhs, const Vector3& rhs) {
    return lhs -= rhs;
}

inline constexpr Vector3 operator*(Vector3 lhs, float scalar) {
    return lhs *= scalar;
}

inline constexpr Vector3 operator*(float scalar, Vector3 rhs) {
    return rhs *= scalar;
}

inline constexpr Vector3 operator/(Vector3 lhs, float scalar) {
    return lhs /= scalar;
}

inline constexpr bool operator==(const Vector3& a, const Vector3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

inline constexpr bool operator!=(const Vector3& a, const Vector3& b) {
    return !(a == b);
}

} // namespace ion
