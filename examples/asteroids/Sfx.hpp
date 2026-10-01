#pragma once

// Procedurally synthesised sound effects, so the example needs no audio
// assets. Everything is built with ion::makeSound.

#include <ion/audio/Audio.hpp>

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace asteroids {

struct Sfx {
    ion::Sound shoot, boomLarge, boomMedium, boomSmall, shipDown, thrust, wave;
};

namespace synth {

constexpr uint32_t RATE = 48000;
constexpr float TAU = 6.28318531f;

inline ion::Sound make(std::vector<float> s) { return ion::makeSound(std::move(s), 1, RATE); }

// Pitch sweep with exponential decay (laser / pew).
inline ion::Sound sweep(float f0, float f1, float seconds, float volume, bool square) {
    size_t n = (size_t)(seconds * RATE);
    std::vector<float> out(n);
    float phase = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / (float)n;
        float f = f0 + (f1 - f0) * t;
        phase += TAU * f / RATE;
        float wave = square ? (std::sin(phase) > 0 ? 1.0f : -1.0f) : std::sin(phase);
        out[i] = wave * volume * std::exp(-4.0f * t);
    }
    return make(std::move(out));
}

// Low-passed noise burst with a decay envelope (explosions, rumble).
inline ion::Sound noiseBurst(float seconds, float volume, float smoothing, float decay,
                             unsigned seed) {
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

// Seamless engine rumble: noise made periodic by crossfading its tail into
// its head, then low-passed.
inline ion::Sound rumbleLoop(float seconds, float volume) {
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

// Rising arpeggio jingle for a new wave.
inline ion::Sound jingle() {
    const float notes[] = {392.0f, 523.25f, 659.25f};
    size_t per = (size_t)(0.09f * RATE);
    std::vector<float> out(per * 3);
    for (int k = 0; k < 3; k++) {
        for (size_t i = 0; i < per; i++) {
            float t = (float)i / (float)per;
            float ph = TAU * notes[k] * (float)i / RATE;
            out[k * per + i] = (std::sin(ph) > 0 ? 0.5f : -0.5f) * 0.25f * (1.0f - t);
        }
    }
    return make(std::move(out));
}

} // namespace synth

inline Sfx makeSfx() {
    Sfx s;
    s.shoot = synth::sweep(1100.0f, 220.0f, 0.16f, 0.22f, true);
    s.boomLarge = synth::noiseBurst(0.7f, 1.4f, 0.10f, 5.0f, 1);
    s.boomMedium = synth::noiseBurst(0.45f, 1.3f, 0.16f, 6.0f, 2);
    s.boomSmall = synth::noiseBurst(0.28f, 1.1f, 0.30f, 7.0f, 3);
    s.shipDown = synth::noiseBurst(1.1f, 1.6f, 0.07f, 3.5f, 4);
    s.thrust = synth::rumbleLoop(0.5f, 0.35f);
    s.wave = synth::jingle();
    return s;
}

} // namespace asteroids
