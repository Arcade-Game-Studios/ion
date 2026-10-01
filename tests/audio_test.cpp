#include <ion/audio/Audio.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            failures++;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
    do {                                                                             \
        float _a = (a), _b = (b);                                                    \
        if (std::fabs(_a - _b) > (eps)) {                                            \
            std::printf("FAIL %s:%d: %s = %f, expected %f\n", __FILE__, __LINE__,    \
                        #a, _a, _b);                                                 \
            failures++;                                                              \
        }                                                                            \
    } while (0)

using namespace ion;

static Sound constant(float v, size_t frames, uint32_t rate = 48000, uint32_t ch = 1) {
    return makeSound(std::vector<float>(frames * ch, v), ch, rate);
}

static void testMakeSound() {
    CHECK(makeSound({}, 1, 48000) != nullptr);
    CHECK(makeSound({0.0f}, 3, 48000) == nullptr);        // bad channel count
    CHECK(makeSound({0.0f, 0.0f, 0.0f}, 2, 48000) == nullptr); // not whole frames
    CHECK(makeSound({0.0f}, 1, 0) == nullptr);
    CHECK_NEAR(constant(0.0f, 24000)->durationSeconds(), 0.5f, 1e-4f);
}

static void testWavRoundTrip() {
    std::vector<float> samples;
    for (int i = 0; i < 100; i++) samples.push_back(std::sin(i * 0.2f) * 0.8f);
    Sound s = makeSound(samples, 1, 22050);
    auto bytes = encodeWav(*s);
    Sound back = loadWavFromMemory(bytes.data(), bytes.size());
    CHECK(back != nullptr);
    CHECK(back->channels == 1 && back->sampleRate == 22050);
    CHECK(back->frames() == 100);
    for (size_t i = 0; i < 100 && back; i++) CHECK_NEAR(back->samples[i], samples[i], 1e-3f);

    // Stereo.
    Sound st = makeSound({0.5f, -0.5f, 0.25f, -0.25f}, 2, 44100);
    auto b2 = encodeWav(*st);
    Sound back2 = loadWavFromMemory(b2.data(), b2.size());
    CHECK(back2 && back2->channels == 2 && back2->frames() == 2);
}

static void testWavRejectsGarbage() {
    const uint8_t junk[] = {1, 2, 3, 4, 5};
    CHECK(loadWavFromMemory(junk, sizeof(junk)) == nullptr);
    CHECK(loadWavFromMemory(nullptr, 0) == nullptr);
    CHECK(loadWav("/nonexistent/file.wav") == nullptr);
    // Truncated header.
    auto bytes = encodeWav(*constant(0.1f, 10));
    CHECK(loadWavFromMemory(bytes.data(), 20) == nullptr);
    // Truncated data still decodes the available samples.
    auto t = loadWavFromMemory(bytes.data(), bytes.size() - 6);
    CHECK(t && t->frames() == 7);
}

static void testMonoCenterAndPan() {
    AudioMixer m(48000);
    float out[2 * 8];
    Sound s = constant(1.0f, 100);

    m.play(s);
    m.render(out, 8);
    CHECK_NEAR(out[0], 0.70710678f, 1e-4f);
    CHECK_NEAR(out[1], 0.70710678f, 1e-4f);

    m.stopAll();
    m.play(s, {.pan = -1.0f});
    m.render(out, 8);
    CHECK_NEAR(out[0], 1.0f, 1e-4f);
    CHECK_NEAR(out[1], 0.0f, 1e-4f);

    m.stopAll();
    m.play(s, {.pan = 1.0f});
    m.render(out, 8);
    CHECK_NEAR(out[0], 0.0f, 1e-4f);
    CHECK_NEAR(out[1], 1.0f, 1e-4f);
}

static void testStereoSourceCenterIsUnity() {
    AudioMixer m(48000);
    float out[2 * 4];
    m.play(makeSound({0.5f, -0.25f, 0.5f, -0.25f, 0.5f, -0.25f, 0.5f, -0.25f}, 2, 48000));
    m.render(out, 4);
    CHECK_NEAR(out[0], 0.5f, 1e-4f);
    CHECK_NEAR(out[1], -0.25f, 1e-4f);
}

static void testVolumeBusesAndMaster() {
    AudioMixer m(48000);
    float out[2 * 4];
    Sound s = constant(1.0f, 100);
    PlayParams p{.volume = 0.5f, .pan = -1.0f};

    m.play(s, p);
    m.render(out, 4);
    CHECK_NEAR(out[0], 0.5f, 1e-4f);

    m.stopAll();
    m.setMasterVolume(0.5f);
    m.setBusVolume(AudioBus::Music, 0.5f);
    m.play(s, {.volume = 1.0f, .pan = -1.0f, .bus = AudioBus::Music});
    m.render(out, 4);
    CHECK_NEAR(out[0], 0.25f, 1e-4f);
    CHECK_NEAR(m.masterVolume(), 0.5f, 1e-6f);
    CHECK_NEAR(m.busVolume(AudioBus::Sfx), 1.0f, 1e-6f);
}

static void testFinishAndReap() {
    AudioMixer m(48000);
    std::vector<float> out(2 * 64);
    VoiceId v = m.play(constant(1.0f, 10));
    CHECK(v != INVALID_VOICE && m.isPlaying(v));
    m.render(out.data(), 64);
    CHECK(!m.isPlaying(v));
    CHECK(m.activeVoices() == 0);
    // Frames after the clip end are silent.
    CHECK_NEAR(out[2 * 20], 0.0f, 1e-6f);
    CHECK(out[2 * 5] > 0.1f);
    // Exact-fit clip is also reaped.
    VoiceId w = m.play(constant(1.0f, 64));
    m.render(out.data(), 64);
    CHECK(!m.isPlaying(w));
}

static void testLoopAndStop() {
    AudioMixer m(48000);
    std::vector<float> out(2 * 1000);
    VoiceId v = m.play(constant(1.0f, 7), {.loop = true});
    m.render(out.data(), 1000);
    CHECK(m.isPlaying(v));
    CHECK(out[2 * 999] > 0.1f); // still audible after many wraps
    m.stop(v);
    CHECK(!m.isPlaying(v));
    m.stop(v); // idempotent
    m.stop(INVALID_VOICE);
}

static void testPitchAndResample() {
    AudioMixer m(48000);
    std::vector<float> out(2 * 400);

    // 2x pitch: a 100-frame clip finishes in 50 output frames.
    m.play(constant(1.0f, 100), {.pitch = 2.0f});
    m.render(out.data(), 100);
    CHECK(out[2 * 45] > 0.1f);
    CHECK_NEAR(out[2 * 60], 0.0f, 1e-6f);

    // A 24 kHz clip plays at half speed on a 48 kHz mixer (twice as long).
    m.play(constant(1.0f, 100, 24000));
    m.render(out.data(), 400);
    CHECK(out[2 * 190] > 0.1f);
    CHECK_NEAR(out[2 * 210], 0.0f, 1e-6f);
}

static void testInterpolation() {
    AudioMixer m(48000);
    float out[2 * 4];
    // Half-rate ramp: output frames land between source frames.
    m.play(makeSound({0.0f, 1.0f, 0.0f, 1.0f}, 1, 24000), {.pan = -1.0f});
    m.render(out, 4);
    CHECK_NEAR(out[0], 0.0f, 1e-4f);
    CHECK_NEAR(out[2], 0.5f, 1e-4f);
    CHECK_NEAR(out[4], 1.0f, 1e-4f);
}

static void testClippingAndVoiceCap() {
    AudioMixer m(48000);
    std::vector<float> out(2 * 16);
    Sound loud = constant(1.0f, 1000);
    for (int i = 0; i < 10; i++) m.play(loud, {.pan = -1.0f});
    m.render(out.data(), 16);
    CHECK_NEAR(out[0], 1.0f, 1e-6f); // clamped, not 10.0

    m.stopAll();
    VoiceId first = m.play(loud);
    for (size_t i = 0; i < AudioMixer::MAX_VOICES; i++) m.play(loud);
    CHECK(m.activeVoices() == AudioMixer::MAX_VOICES);
    CHECK(!m.isPlaying(first)); // oldest was stolen
}

static void testLiveVoiceControls() {
    AudioMixer m(48000);
    float out[2 * 4];
    VoiceId v = m.play(constant(1.0f, 1000), {.pan = -1.0f, .loop = true});
    m.setVoiceVolume(v, 0.25f);
    m.render(out, 4);
    CHECK_NEAR(out[0], 0.25f, 1e-4f);
    m.setVoicePan(v, 1.0f);
    m.render(out, 4);
    CHECK_NEAR(out[0], 0.0f, 1e-4f);
    CHECK_NEAR(out[1], 0.25f, 1e-4f);
    m.setVoiceVolume(INVALID_VOICE, 1.0f); // no-op
    CHECK(m.play(nullptr) == INVALID_VOICE);
}

int main() {
    testMakeSound();
    testWavRoundTrip();
    testWavRejectsGarbage();
    testMonoCenterAndPan();
    testStereoSourceCenterIsUnity();
    testVolumeBusesAndMaster();
    testFinishAndReap();
    testLoopAndStop();
    testPitchAndResample();
    testInterpolation();
    testClippingAndVoiceCap();
    testLiveVoiceControls();
    if (failures == 0) std::printf("audio_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
