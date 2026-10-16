#pragma once

//
// Tiny procedural sound generators, handy for prototypes and for examples
// that ship without audio assets. Everything returns a mono ion::Sound.
//

#include <ion/audio/Audio.hpp>

#include <cmath>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace ion::synth {

constexpr uint32_t RATE = 48000;
constexpr float TAU = 6.28318531f;

inline Sound make(std::vector<float> samples) {
    return makeSound(std::move(samples), 1, RATE);
}

// Pitch sweep f0 -> f1 Hz with exponential decay (lasers, thumps, blips).
inline Sound sweep(float f0, float f1, float seconds, float volume, bool square = false) {
    size_t n = (size_t)(seconds * RATE);
    std::vector<float> out(n);
    float phase = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / (float)n;
        phase += TAU * (f0 + (f1 - f0) * t) / RATE;
        float wave = square ? (std::sin(phase) > 0 ? 1.0f : -1.0f) : std::sin(phase);
        out[i] = wave * volume * std::exp(-4.0f * t);
    }
    return make(std::move(out));
}

// Low-passed noise with an exponential decay (explosions, impacts, rumble).
// smoothing is the one-pole filter coefficient: small = dull, 1 = white noise.
inline Sound noiseBurst(float seconds, float volume, float smoothing, float decay,
                        unsigned seed = 1) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    size_t n = (size_t)(seconds * RATE);
    std::vector<float> out(n);
    float lp = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / (float)n;
        lp += (dist(rng) - lp) * smoothing;
        out[i] = lp * volume * std::exp(-decay * t);
    }
    return make(std::move(out));
}

// Seamlessly loopable rumble: noise made periodic by crossfading its tail
// into its head, then low-passed.
inline Sound rumbleLoop(float seconds, float volume) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    size_t n = (size_t)(seconds * RATE);
    size_t fade = n / 8;
    std::vector<float> raw(n + fade);
    for (float& v : raw) v = dist(rng);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; i++) {
        float v = raw[i];
        if (i < fade) {
            float w = (float)i / (float)fade;
            v = raw[i] * w + raw[n + i] * (1.0f - w);
        }
        out[i] = v;
    }
    float lp = 0.0f;
    for (int pass = 0; pass < 2; pass++) { // 2nd pass settles filter state across the seam
        for (float& v : out) {
            lp += (v - lp) * 0.06f;
            v = lp;
        }
    }
    for (float& v : out) v *= volume * 3.0f;
    return make(std::move(out));
}

// A steady tone whose pitch wobbles between lo and hi Hz; loops seamlessly
// (a whole number of wobble cycles fits in `seconds`). Good for sirens/hums.
inline Sound wobbleLoop(float lo, float hi, float wobbleHz, float seconds, float volume) {
    int cycles = std::max(1, (int)std::lround(wobbleHz * seconds));
    size_t n = (size_t)(seconds * RATE);
    std::vector<float> out(n);
    float phase = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float lfo = 0.5f - 0.5f * std::cos(TAU * (float)cycles * (float)i / (float)n);
        phase += TAU * (lo + (hi - lo) * lfo) / RATE;
        // sine plus a little 2nd harmonic for a less sterile tone
        out[i] = volume * (0.8f * std::sin(phase) + 0.2f * std::sin(2.0f * phase));
    }
    return make(std::move(out));
}

} // namespace ion::synth
