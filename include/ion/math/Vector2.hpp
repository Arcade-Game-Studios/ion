#pragma once

#include <cmath>

namespace ion {

struct Vector2 {
    float x;
    float y;

    constexpr Vector2() : x(0.0f), y(0.0f) {}
    constexpr Vector2(float x, float y) : x(x), y(y) {}

    Vector2& operator+=(const Vector2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vector2& operator-=(const Vector2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vector2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    Vector2& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    float lengthSquared() const {
        return x * x + y * y;
    }

    float length() const {
        return std::sqrt(lengthSquared());
    }

    Vector2 normalized() const {
        float len = length();
        if (len <= 0.0f) {
            return *this;
        }
        return Vector2(x / len, y / len);
    }

    static float dot(const Vector2& a, const Vector2& b) {
        return a.x * b.x + a.y * b.y;
    }
};

inline constexpr Vector2 operator+(Vector2 lhs, const Vector2& rhs) {
    return lhs += rhs;
}

inline constexpr Vector2 operator-(Vector2 lhs, const Vector2& rhs) {
    return lhs -= rhs;
}

inline constexpr Vector2 operator*(Vector2 lhs, float scalar) {
    return lhs *= scalar;
}

inline constexpr Vector2 operator*(float scalar, Vector2 rhs) {
    return rhs *= scalar;
}

inline constexpr Vector2 operator/(Vector2 lhs, float scalar) {
    return lhs /= scalar;
}

inline constexpr bool operator==(const Vector2& a, const Vector2& b) {
    return a.x == b.x && a.y == b.y;
}

inline constexpr bool operator!=(const Vector2& a, const Vector2& b) {
    return !(a == b);
}

} // namespace ion
