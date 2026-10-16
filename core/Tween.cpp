#include <ion/core/Tween.hpp>

#include <cmath>

namespace ion {

namespace {

constexpr float kPi = 3.14159265358979f;

float bounceOut(float t) {
    const float n = 7.5625f;
    const float d = 2.75f;
    if (t < 1.0f / d) {
        return n * t * t;
    }
    if (t < 2.0f / d) {
        t -= 1.5f / d;
        return n * t * t + 0.75f;
    }
    if (t < 2.5f / d) {
        t -= 2.25f / d;
        return n * t * t + 0.9375f;
    }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

} // namespace

float ease(Ease curve, float t) {
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    const float c1 = 1.70158f;      // back overshoot
    const float c2 = c1 * 1.525f;
    const float c3 = c1 + 1.0f;
    const float c4 = (2.0f * kPi) / 3.0f;   // elastic period
    const float c5 = (2.0f * kPi) / 4.5f;
    switch (curve) {
    case Ease::Linear: return t;
    case Ease::QuadIn: return t * t;
    case Ease::QuadOut: return 1.0f - (1.0f - t) * (1.0f - t);
    case Ease::QuadInOut:
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    case Ease::CubicIn: return t * t * t;
    case Ease::CubicOut: return 1.0f - std::pow(1.0f - t, 3.0f);
    case Ease::CubicInOut:
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case Ease::QuartIn: return t * t * t * t;
    case Ease::QuartOut: return 1.0f - std::pow(1.0f - t, 4.0f);
    case Ease::QuartInOut:
        return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;
    case Ease::QuintIn: return t * t * t * t * t;
    case Ease::QuintOut: return 1.0f - std::pow(1.0f - t, 5.0f);
    case Ease::QuintInOut:
        return t < 0.5f ? 16.0f * t * t * t * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
    case Ease::SineIn: return 1.0f - std::cos(t * kPi / 2.0f);
    case Ease::SineOut: return std::sin(t * kPi / 2.0f);
    case Ease::SineInOut: return -(std::cos(kPi * t) - 1.0f) / 2.0f;
    case Ease::ExpoIn: return std::pow(2.0f, 10.0f * t - 10.0f);
    case Ease::ExpoOut: return 1.0f - std::pow(2.0f, -10.0f * t);
    case Ease::ExpoInOut:
        return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f
                        : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
    case Ease::CircIn: return 1.0f - std::sqrt(1.0f - t * t);
    case Ease::CircOut: return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
    case Ease::CircInOut:
        return t < 0.5f ? (1.0f - std::sqrt(1.0f - 4.0f * t * t)) / 2.0f
                        : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
    case Ease::BackIn: return c3 * t * t * t - c1 * t * t;
    case Ease::BackOut:
        return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
    case Ease::BackInOut:
        return t < 0.5f
                   ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
                   : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) +
                      2.0f) / 2.0f;
    case Ease::ElasticIn:
        return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
    case Ease::ElasticOut:
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    case Ease::ElasticInOut:
        return t < 0.5f
                   ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f
                   : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f +
                         1.0f;
    case Ease::BounceIn: return 1.0f - bounceOut(1.0f - t);
    case Ease::BounceOut: return bounceOut(t);
    case Ease::BounceInOut:
        return t < 0.5f ? (1.0f - bounceOut(1.0f - 2.0f * t)) / 2.0f
                        : (1.0f + bounceOut(2.0f * t - 1.0f)) / 2.0f;
    case Ease::Count: break;
    }
    return t;
}

const char* easeName(Ease curve) {
    static const char* names[] = {
        "Linear",    "QuadIn",     "QuadOut",      "QuadInOut", "CubicIn",   "CubicOut",
        "CubicInOut", "QuartIn",   "QuartOut",     "QuartInOut", "QuintIn",  "QuintOut",
        "QuintInOut", "SineIn",    "SineOut",      "SineInOut", "ExpoIn",    "ExpoOut",
        "ExpoInOut", "CircIn",     "CircOut",      "CircInOut", "BackIn",    "BackOut",
        "BackInOut", "ElasticIn",  "ElasticOut",   "ElasticInOut", "BounceIn", "BounceOut",
        "BounceInOut",
    };
    int index = (int)curve;
    return index >= 0 && index < (int)Ease::Count ? names[index] : "Unknown";
}

} // namespace ion
