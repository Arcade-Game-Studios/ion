#pragma once

#include <ion/math/MathUtils.hpp>
#include <ion/math/Vector2.hpp>

#include <cmath>

namespace ion {

struct Vector3;

// Declared up front so member functions can use them.
inline constexpr Vector3 operator+(Vector3 lhs, const Vector3& rhs);
inline constexpr Vector3 operator-(Vector3 lhs, const Vector3& rhs);
inline constexpr Vector3 operator*(Vector3 lhs, float scalar);

struct Vector3 {
    float x;
    float y;
    float z;

    constexpr Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
    constexpr Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    explicit constexpr Vector3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    constexpr Vector3(const Vector2& xy, float z) : x(xy.x), y(xy.y), z(z) {}

    static constexpr Vector3 zero() { return Vector3(0.0f, 0.0f, 0.0f); }
    static constexpr Vector3 one() { return Vector3(1.0f, 1.0f, 1.0f); }
    static constexpr Vector3 unitX() { return Vector3(1.0f, 0.0f, 0.0f); }
    static constexpr Vector3 unitY() { return Vector3(0.0f, 1.0f, 0.0f); }
    static constexpr Vector3 unitZ() { return Vector3(0.0f, 0.0f, 1.0f); }
    // Engine conventions: right-handed, +Y up, cameras look down -Z.
    static constexpr Vector3 up() { return Vector3(0.0f, 1.0f, 0.0f); }
    static constexpr Vector3 forward() { return Vector3(0.0f, 0.0f, -1.0f); }
    static constexpr Vector3 right() { return Vector3(1.0f, 0.0f, 0.0f); }

    constexpr float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    constexpr float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

    constexpr Vector2 xy() const { return Vector2(x, y); }

    constexpr Vector3 operator-() const { return Vector3(-x, -y, -z); }

    constexpr Vector3& operator+=(const Vector3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    constexpr Vector3& operator-=(const Vector3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    // Component-wise multiply / divide.
    constexpr Vector3& operator*=(const Vector3& other) {
        x *= other.x;
        y *= other.y;
        z *= other.z;
        return *this;
    }

    constexpr Vector3& operator/=(const Vector3& other) {
        x /= other.x;
        y /= other.y;
        z /= other.z;
        return *this;
    }

    constexpr Vector3& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    constexpr Vector3& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }

    constexpr float lengthSquared() const {
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

    // Normalizes in place; returns the previous length.
    float normalize() {
        float len = length();
        if (len > 0.0f) {
            x /= len;
            y /= len;
            z /= len;
        }
        return len;
    }

    Vector3 clampedLength(float maxLength) const {
        float lenSq = lengthSquared();
        if (lenSq > maxLength * maxLength && lenSq > 0.0f) {
            return *this * (maxLength / std::sqrt(lenSq));
        }
        return *this;
    }

    constexpr bool isZero() const {
        return x == 0.0f && y == 0.0f && z == 0.0f;
    }

    static constexpr float dot(const Vector3& a, const Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static constexpr Vector3 cross(const Vector3& a, const Vector3& b) {
        return Vector3(a.y * b.z - a.z * b.y,
                       a.z * b.x - a.x * b.z,
                       a.x * b.y - a.y * b.x);
    }

    static float distance(const Vector3& a, const Vector3& b) {
        return (a - b).length();
    }

    static constexpr float distanceSquared(const Vector3& a, const Vector3& b) {
        return (a - b).lengthSquared();
    }

    static constexpr Vector3 lerp(const Vector3& a, const Vector3& b, float t) {
        return Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                       a.z + (b.z - a.z) * t);
    }

    static constexpr Vector3 min(const Vector3& a, const Vector3& b) {
        return Vector3(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y,
                       a.z < b.z ? a.z : b.z);
    }

    static constexpr Vector3 max(const Vector3& a, const Vector3& b) {
        return Vector3(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y,
                       a.z > b.z ? a.z : b.z);
    }

    static Vector3 abs(const Vector3& v) {
        return Vector3(std::fabs(v.x), std::fabs(v.y), std::fabs(v.z));
    }

    // Reflects v about the plane with the given unit normal.
    static constexpr Vector3 reflect(const Vector3& v, const Vector3& normal) {
        return v - normal * (2.0f * dot(v, normal));
    }

    static constexpr Vector3 project(const Vector3& v, const Vector3& onto) {
        float d = onto.lengthSquared();
        return d > 0.0f ? onto * (dot(v, onto) / d) : Vector3();
    }

    // Removes the component of v along the unit normal.
    static constexpr Vector3 projectOnPlane(const Vector3& v,
                                            const Vector3& normal) {
        return v - normal * dot(v, normal);
    }

    // Unsigned angle between a and b in radians, in [0, PI].
    static float angleBetween(const Vector3& a, const Vector3& b) {
        return std::atan2(cross(a, b).length(), dot(a, b));
    }

    static Vector3 moveTowards(const Vector3& current, const Vector3& target,
                               float maxDistance) {
        Vector3 delta = target - current;
        float dist = delta.length();
        if (dist <= maxDistance || dist <= 0.0f) {
            return target;
        }
        return current + delta * (maxDistance / dist);
    }

    static bool approxEqual(const Vector3& a, const Vector3& b,
                            float epsilon = EPSILON) {
        return std::fabs(a.x - b.x) <= epsilon &&
               std::fabs(a.y - b.y) <= epsilon &&
               std::fabs(a.z - b.z) <= epsilon;
    }
};

inline constexpr Vector3 operator+(Vector3 lhs, const Vector3& rhs) {
    return lhs += rhs;
}

inline constexpr Vector3 operator-(Vector3 lhs, const Vector3& rhs) {
    return lhs -= rhs;
}

inline constexpr Vector3 operator*(Vector3 lhs, const Vector3& rhs) {
    return lhs *= rhs;
}

inline constexpr Vector3 operator/(Vector3 lhs, const Vector3& rhs) {
    return lhs /= rhs;
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
