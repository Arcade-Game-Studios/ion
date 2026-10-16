#pragma once

#include <cmath>
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

    // 0xRRGGBB (opaque).
    static constexpr Color fromHex(uint32_t rgb) {
        return fromRGB((uint8_t)((rgb >> 16) & 0xFF), (uint8_t)((rgb >> 8) & 0xFF),
                       (uint8_t)(rgb & 0xFF));
    }

    // 0xRRGGBBAA.
    static constexpr Color fromHexRGBA(uint32_t rgba) {
        return fromRGB((uint8_t)((rgba >> 24) & 0xFF), (uint8_t)((rgba >> 16) & 0xFF),
                       (uint8_t)((rgba >> 8) & 0xFF), (uint8_t)(rgba & 0xFF));
    }

    // Hue in degrees [0, 360), saturation and value in [0, 1].
    static Color fromHSV(float hue, float saturation, float value, float alpha = 1.0f) {
        float h = std::fmod(hue, 360.0f);
        if (h < 0.0f) {
            h += 360.0f;
        }
        float c = value * saturation;
        float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
        float m = value - c;
        float rr = 0.0f, gg = 0.0f, bb = 0.0f;
        switch ((int)(h / 60.0f)) {
            case 0: rr = c; gg = x; break;
            case 1: rr = x; gg = c; break;
            case 2: gg = c; bb = x; break;
            case 3: gg = x; bb = c; break;
            case 4: rr = x; bb = c; break;
            default: rr = c; bb = x; break;
        }
        return Color(rr + m, gg + m, bb + m, alpha);
    }

    // Writes hue (degrees), saturation and value.
    void toHSV(float& hue, float& saturation, float& value) const {
        float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        float d = mx - mn;
        value = mx;
        saturation = mx > 0.0f ? d / mx : 0.0f;
        if (d <= 0.0f) {
            hue = 0.0f;
        } else if (mx == r) {
            hue = 60.0f * std::fmod((g - b) / d, 6.0f);
        } else if (mx == g) {
            hue = 60.0f * ((b - r) / d + 2.0f);
        } else {
            hue = 60.0f * ((r - g) / d + 4.0f);
        }
        if (hue < 0.0f) {
            hue += 360.0f;
        }
    }

    // Packs to 0xAABBGGRR (byte order R, G, B, A in memory on little endian),
    // matching RGBA8 texture data.
    constexpr uint32_t toRGBA8() const {
        auto q = [](float v) -> uint32_t {
            v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            return (uint32_t)(v * 255.0f + 0.5f);
        };
        return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
    }

    constexpr Color withAlpha(float alpha) const { return Color(r, g, b, alpha); }

    // Multiplies RGB by alpha (for premultiplied-alpha blending).
    constexpr Color premultiplied() const { return Color(r * a, g * a, b * a, a); }

    constexpr Color clamped() const {
        auto c = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        return Color(c(r), c(g), c(b), c(a));
    }

    // Perceived brightness (Rec. 709 luma).
    constexpr float luminance() const { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

    constexpr Color grayscale() const {
        float l = luminance();
        return Color(l, l, l, a);
    }

    // Moves toward white (amount > 0) or black (amount < 0).
    constexpr Color lighten(float amount) const {
        return amount >= 0.0f
                   ? Color(r + (1.0f - r) * amount, g + (1.0f - g) * amount,
                           b + (1.0f - b) * amount, a)
                   : Color(r * (1.0f + amount), g * (1.0f + amount),
                           b * (1.0f + amount), a);
    }

    static constexpr Color lerp(const Color& from, const Color& to, float t) {
        return Color(from.r + (to.r - from.r) * t, from.g + (to.g - from.g) * t,
                     from.b + (to.b - from.b) * t, from.a + (to.a - from.a) * t);
    }

    static constexpr Color black() { return Color(0.0f, 0.0f, 0.0f); }
    static constexpr Color white() { return Color(1.0f, 1.0f, 1.0f); }
    static constexpr Color gray() { return Color(0.5f, 0.5f, 0.5f); }
    static constexpr Color lightGray() { return Color(0.75f, 0.75f, 0.75f); }
    static constexpr Color darkGray() { return Color(0.25f, 0.25f, 0.25f); }
    static constexpr Color red() { return Color(1.0f, 0.0f, 0.0f); }
    static constexpr Color green() { return Color(0.0f, 1.0f, 0.0f); }
    static constexpr Color blue() { return Color(0.0f, 0.0f, 1.0f); }
    static constexpr Color yellow() { return Color(1.0f, 1.0f, 0.0f); }
    static constexpr Color cyan() { return Color(0.0f, 1.0f, 1.0f); }
    static constexpr Color magenta() { return Color(1.0f, 0.0f, 1.0f); }
    static constexpr Color orange() { return Color(1.0f, 0.5f, 0.0f); }
    static constexpr Color purple() { return Color(0.5f, 0.0f, 0.5f); }
    static constexpr Color pink() { return Color(1.0f, 0.75f, 0.8f); }
    static constexpr Color brown() { return Color(0.6f, 0.4f, 0.2f); }
    static constexpr Color cornflowerBlue() { return Color(0.392f, 0.584f, 0.929f); }
    static constexpr Color transparent() { return Color(0.0f, 0.0f, 0.0f, 0.0f); }
};

// Component-wise modulate (tinting).
inline constexpr Color operator*(const Color& a, const Color& b) {
    return Color(a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a);
}

inline constexpr Color operator*(const Color& c, float s) {
    return Color(c.r * s, c.g * s, c.b * s, c.a * s);
}

inline constexpr Color operator*(float s, const Color& c) { return c * s; }

inline constexpr Color operator+(const Color& a, const Color& b) {
    return Color(a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a);
}

inline constexpr Color operator-(const Color& a, const Color& b) {
    return Color(a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a);
}

inline constexpr bool operator==(const Color& a, const Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

inline constexpr bool operator!=(const Color& a, const Color& b) { return !(a == b); }

} // namespace ion
