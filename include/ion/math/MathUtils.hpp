#pragma once

#include <cmath>
#include <cstdint>

namespace ion {

//
// Scalar math helpers shared by the vector, matrix and quaternion types.
//

inline constexpr float PI = 3.14159265358979323846f;
inline constexpr float TAU = 6.28318530717958647692f;
inline constexpr float HALF_PI = 1.57079632679489661923f;
inline constexpr float DEG_TO_RAD = PI / 180.0f;
inline constexpr float RAD_TO_DEG = 180.0f / PI;
inline constexpr float EPSILON = 1e-6f;

inline constexpr float radians(float degrees) { return degrees * DEG_TO_RAD; }
inline constexpr float degrees(float radians) { return radians * RAD_TO_DEG; }

template <typename T>
inline constexpr T clamp(T value, T lo, T hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

inline constexpr float saturate(float value) { return clamp(value, 0.0f, 1.0f); }

inline constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Returns t such that lerp(a, b, t) == value (0 when a == b).
inline constexpr float inverseLerp(float a, float b, float value) {
    return a == b ? 0.0f : (value - a) / (b - a);
}

// Maps value from [inMin, inMax] to [outMin, outMax] (unclamped).
inline constexpr float remap(float value, float inMin, float inMax,
                             float outMin, float outMax) {
    return lerp(outMin, outMax, inverseLerp(inMin, inMax, value));
}

// Hermite smoothing between edge0 and edge1 (GLSL smoothstep semantics).
inline constexpr float smoothstep(float edge0, float edge1, float x) {
    float t = saturate(inverseLerp(edge0, edge1, x));
    return t * t * (3.0f - 2.0f * t);
}

// Ken Perlin's smootherstep (zero first and second derivatives at edges).
inline constexpr float smootherstep(float edge0, float edge1, float x) {
    float t = saturate(inverseLerp(edge0, edge1, x));
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline constexpr float sign(float value) {
    return value > 0.0f ? 1.0f : (value < 0.0f ? -1.0f : 0.0f);
}

inline bool approxEqual(float a, float b, float epsilon = EPSILON) {
    return std::fabs(a - b) <= epsilon;
}

// Moves current toward target by at most maxDelta, never overshooting.
inline float moveTowards(float current, float target, float maxDelta) {
    if (std::fabs(target - current) <= maxDelta) {
        return target;
    }
    return current + sign(target - current) * maxDelta;
}

// Wraps an angle in radians into [-PI, PI).
inline float wrapAngle(float radians) {
    float wrapped = std::fmod(radians + PI, TAU);
    if (wrapped < 0.0f) {
        wrapped += TAU;
    }
    return wrapped - PI;
}

// Shortest signed angular difference from `from` to `to`, in [-PI, PI).
inline float angleDifference(float from, float to) {
    return wrapAngle(to - from);
}

// Interpolates between angles along the shortest arc.
inline float lerpAngle(float from, float to, float t) {
    return from + angleDifference(from, to) * t;
}

// Frame-rate independent exponential smoothing: moves `current` toward
// `target`, closing `1 - exp(-rate * dt)` of the remaining gap per call.
inline float damp(float current, float target, float rate, float deltaTime) {
    return lerp(current, target, 1.0f - std::exp(-rate * deltaTime));
}

// Wraps value into [0, length) (works for negative values, unlike fmod).
inline float repeat(float value, float length) {
    if (length <= 0.0f) {
        return 0.0f;
    }
    float r = std::fmod(value, length);
    return r < 0.0f ? r + length : r;
}

// Bounces value back and forth between 0 and length.
inline float pingPong(float value, float length) {
    if (length <= 0.0f) {
        return 0.0f;
    }
    float t = repeat(value, length * 2.0f);
    return length - std::fabs(t - length);
}

inline constexpr uint32_t nextPowerOfTwo(uint32_t value) {
    if (value <= 1) {
        return 1;
    }
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

} // namespace ion
