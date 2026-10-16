#pragma once

// Procedurally synthesised sound effects, so the example needs no audio
// assets. Everything is built with ion::makeSound.

#include <ion/audio/Audio.hpp>
#include <ion/audio/Synth.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace asteroids {

struct Sfx {
    ion::Sound shoot, boomLarge, boomMedium, boomSmall, shipDown, thrust, wave;
};

namespace synth {

using namespace ion::synth;

// Rising arpeggio jingle for a new wave.
inline ion::Sound jingle() {
    const float notes[] = {392.0f, 523.25f, 659.25f};
    size_t per = (size_t)(0.09f * ion::synth::RATE);
    std::vector<float> out(per * 3);
    for (int k = 0; k < 3; k++) {
        for (size_t i = 0; i < per; i++) {
            float t = (float)i / (float)per;
            float ph = ion::synth::TAU * notes[k] * (float)i / ion::synth::RATE;
            out[k * per + i] = (std::sin(ph) > 0 ? 0.5f : -0.5f) * 0.25f * (1.0f - t);
        }
    }
    return ion::synth::make(std::move(out));
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
