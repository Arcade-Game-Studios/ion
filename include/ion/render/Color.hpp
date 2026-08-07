#pragma once

#include <cstdint>

namespace ion {

struct Color {
    float r;
    float g;
    float b;
    float a;

    constexpr Color() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
    constexpr Color(float r, float g, float b, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}

    static constexpr Color fromRGB(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }

    static constexpr Color black() { return Color(0.0f, 0.0f, 0.0f); }
    static constexpr Color white() { return Color(1.0f, 1.0f, 1.0f); }
    static constexpr Color red() { return Color(1.0f, 0.0f, 0.0f); }
    static constexpr Color green() { return Color(0.0f, 1.0f, 0.0f); }
    static constexpr Color blue() { return Color(0.0f, 0.0f, 1.0f); }
    static constexpr Color yellow() { return Color(1.0f, 1.0f, 0.0f); }
    static constexpr Color cyan() { return Color(0.0f, 1.0f, 1.0f); }
    static constexpr Color magenta() { return Color(1.0f, 0.0f, 1.0f); }
    static constexpr Color transparent() { return Color(0.0f, 0.0f, 0.0f, 0.0f); }
};

} // namespace ion
