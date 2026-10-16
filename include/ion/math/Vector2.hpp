#pragma once

#include <ion/math/MathUtils.hpp>

#include <cmath>

namespace ion {

struct Vector2;

// Declared up front so member functions can use them.
inline constexpr Vector2 operator+(Vector2 lhs, const Vector2& rhs);
inline constexpr Vector2 operator-(Vector2 lhs, const Vector2& rhs);
inline constexpr Vector2 operator*(Vector2 lhs, float scalar);

struct Vector2 {
    float x;
    float y;

    constexpr Vector2() : x(0.0f), y(0.0f) {}
    constexpr Vector2(float x, float y) : x(x), y(y) {}
    explicit constexpr Vector2(float scalar) : x(scalar), y(scalar) {}

    static constexpr Vector2 zero() { return Vector2(0.0f, 0.0f); }
    static constexpr Vector2 one() { return Vector2(1.0f, 1.0f); }
    static constexpr Vector2 unitX() { return Vector2(1.0f, 0.0f); }
    static constexpr Vector2 unitY() { return Vector2(0.0f, 1.0f); }

    // Unit vector pointing at `radians` (0 = +x, PI/2 = +y).
    static Vector2 fromAngle(float radians, float length = 1.0f) {
        return Vector2(std::cos(radians) * length, std::sin(radians) * length);
    }

    constexpr float& operator[](int i) { return i == 0 ? x : y; }
    constexpr float operator[](int i) const { return i == 0 ? x : y; }

    constexpr Vector2 operator-() const { return Vector2(-x, -y); }

    constexpr Vector2& operator+=(const Vector2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    constexpr Vector2& operator-=(const Vector2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    // Component-wise multiply / divide.
    constexpr Vector2& operator*=(const Vector2& other) {
        x *= other.x;
        y *= other.y;
        return *this;
    }

    constexpr Vector2& operator/=(const Vector2& other) {
        x /= other.x;
        y /= other.y;
        return *this;
    }

    constexpr Vector2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    constexpr Vector2& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    constexpr float lengthSquared() const {
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

    // Normalizes in place; returns the previous length.
    float normalize() {
        float len = length();
        if (len > 0.0f) {
            x /= len;
            y /= len;
        }
        return len;
    }

    // Angle of the vector in radians, measured from +x toward +y.
    float angle() const {
        return std::atan2(y, x);
    }

    Vector2 rotated(float radians) const {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return Vector2(x * c - y * s, x * s + y * c);
    }

    // Counter-clockwise perpendicular (rotated +90 degrees).
    constexpr Vector2 perpendicular() const {
        return Vector2(-y, x);
    }

    // Shortens the vector to at most maxLength.
    Vector2 clampedLength(float maxLength) const {
        float lenSq = lengthSquared();
        if (lenSq > maxLength * maxLength && lenSq > 0.0f) {
            return *this * (maxLength / std::sqrt(lenSq));
        }
        return *this;
    }

    constexpr bool isZero() const {
        return x == 0.0f && y == 0.0f;
    }

    static constexpr float dot(const Vector2& a, const Vector2& b) {
        return a.x * b.x + a.y * b.y;
    }

    // 2D cross product (z component of the 3D cross product). Positive when
    // b is counter-clockwise from a.
    static constexpr float cross(const Vector2& a, const Vector2& b) {
        return a.x * b.y - a.y * b.x;
    }

    static float distance(const Vector2& a, const Vector2& b) {
        return (a - b).length();
    }

    static constexpr float distanceSquared(const Vector2& a, const Vector2& b) {
        return (a - b).lengthSquared();
    }

    static constexpr Vector2 lerp(const Vector2& a, const Vector2& b, float t) {
        return Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }

    static constexpr Vector2 min(const Vector2& a, const Vector2& b) {
        return Vector2(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y);
    }

    static constexpr Vector2 max(const Vector2& a, const Vector2& b) {
        return Vector2(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y);
    }

    static Vector2 abs(const Vector2& v) {
        return Vector2(std::fabs(v.x), std::fabs(v.y));
    }

    // Reflects v about the plane with the given unit normal.
    static constexpr Vector2 reflect(const Vector2& v, const Vector2& normal) {
        return v - normal * (2.0f * dot(v, normal));
    }

    // Projects v onto the direction of onto (zero if onto is zero).
    static constexpr Vector2 project(const Vector2& v, const Vector2& onto) {
        float d = onto.lengthSquared();
        return d > 0.0f ? onto * (dot(v, onto) / d) : Vector2();
    }

    // Unsigned angle between a and b in radians, in [0, PI].
    static float angleBetween(const Vector2& a, const Vector2& b) {
        return std::fabs(std::atan2(cross(a, b), dot(a, b)));
    }

    // Moves current toward target by at most maxDistance.
    static Vector2 moveTowards(const Vector2& current, const Vector2& target,
                               float maxDistance) {
        Vector2 delta = target - current;
        float dist = delta.length();
        if (dist <= maxDistance || dist <= 0.0f) {
            return target;
        }
        return current + delta * (maxDistance / dist);
    }

    static bool approxEqual(const Vector2& a, const Vector2& b,
                            float epsilon = EPSILON) {
        return std::fabs(a.x - b.x) <= epsilon && std::fabs(a.y - b.y) <= epsilon;
    }
};

inline constexpr Vector2 operator+(Vector2 lhs, const Vector2& rhs) {
    return lhs += rhs;
}

inline constexpr Vector2 operator-(Vector2 lhs, const Vector2& rhs) {
    return lhs -= rhs;
}

inline constexpr Vector2 operator*(Vector2 lhs, const Vector2& rhs) {
    return lhs *= rhs;
}

inline constexpr Vector2 operator/(Vector2 lhs, const Vector2& rhs) {
    return lhs /= rhs;
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
