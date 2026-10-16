#include <ion/audio/Audio.hpp>

#include <ion/math/Vector3.hpp>

#include <chrono>
#include <cmath>
#include <thread>
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
    m.render(out, 4); // new voices start at their target gain
    CHECK_NEAR(out[0], 1.0f, 1e-4f);
    m.setVoiceVolume(v, 0.25f);
    m.render(out, 4);
    // Changes ramp across the block (no clicks) and land on the target at its end.
    CHECK(out[0] > out[6] && out[0] < 1.0f);
    CHECK_NEAR(out[6], 0.25f, 1e-4f);
    m.setVoicePan(v, 1.0f);
    m.render(out, 4);
    CHECK_NEAR(out[6], 0.0f, 1e-4f);
    CHECK_NEAR(out[7], 0.25f, 1e-4f);
    m.setVoiceVolume(INVALID_VOICE, 1.0f); // no-op
    CHECK(m.play(nullptr) == INVALID_VOICE);
}

static const std::string DATA = ION_TEST_DATA;

// Estimates frequency by counting rising zero crossings.
static float estimateFrequency(const std::vector<float>& mono, float rate) {
    int crossings = 0;
    for (size_t i = 1; i < mono.size(); i++) {
        if (mono[i - 1] < 0.0f && mono[i] >= 0.0f) crossings++;
    }
    return (float)crossings * rate / (float)mono.size();
}

static void checkTone(const Sound& s, const char* name) {
    if (!s) {
        std::printf("FAIL: %s did not decode\n", name);
        failures++;
        return;
    }
    CHECK(s->channels == 1);
    CHECK(s->sampleRate == 44100);
    CHECK(s->durationSeconds() > 0.45f && s->durationSeconds() < 0.6f);
    // Skip the codec's start-up padding when measuring pitch.
    std::vector<float> body(s->samples.begin() + 4000, s->samples.end() - 4000);
    float f = estimateFrequency(body, (float)s->sampleRate);
    if (std::fabs(f - 440.0f) > 12.0f) {
        std::printf("FAIL: %s frequency %f, expected ~440\n", name, f);
        failures++;
    }
}

static void testDecoders() {
    checkTone(loadWav(DATA + "/tone.wav"), "wav");
    checkTone(loadOgg(DATA + "/tone.ogg"), "ogg");
    checkTone(loadMp3(DATA + "/tone.mp3"), "mp3");
    checkTone(loadSound(DATA + "/tone.OGG"), "loadSound(.OGG)");
    CHECK(loadSound(DATA + "/tone.xyz") == nullptr);
    CHECK(loadOgg(DATA + "/tone.wav") == nullptr); // wrong container
    CHECK(loadMp3("/nonexistent.mp3") == nullptr);
    const uint8_t junk[16] = {0};
    CHECK(loadOggFromMemory(junk, sizeof(junk)) == nullptr);
    CHECK(loadMp3FromMemory(junk, sizeof(junk)) == nullptr);
}

static void testStreams() {
    for (const char* name : {"tone.wav", "tone.ogg", "tone.mp3"}) {
        auto st = openStream(DATA + "/" + name);
        CHECK(st != nullptr);
        if (!st) continue;
        size_t total = 0, n;
        std::vector<float> buf(1024 * 2);
        while ((n = st->read(buf.data(), 1024)) > 0) total += n;
        CHECK(total > 21000 && total < 26000);
        CHECK(st->rewind());
        CHECK(st->read(buf.data(), 1024) == 1024); // readable again after rewind
    }
    CHECK(openStream("/nonexistent.ogg") == nullptr);
    CHECK(openStream(DATA + "/tone.txt") == nullptr);
}

static bool waitUntil(auto pred, int ms = 2000) {
    for (int i = 0; i < ms / 5; i++) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

// Pulls audio like the device callback would and returns peak amplitude.
static float renderPeak(AudioMixer& m, int blocks) {
    std::vector<float> out(2 * 512);
    float peak = 0.0f;
    for (int i = 0; i < blocks; i++) {
        m.render(out.data(), 512);
        for (float v : out) peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

static void testPlayStream() {
    AudioMixer m(48000); // differs from the 44.1 kHz file: exercises resampling
    VoiceId v = m.playStream(openStream(DATA + "/tone.ogg"), {.bus = AudioBus::Music});
    CHECK(v != INVALID_VOICE);
    // The decoder thread needs a moment to fill the ring; poll until audible.
    float peak = 0.0f;
    waitUntil([&] { peak = std::max(peak, renderPeak(m, 4)); return peak > 0.05f; });
    CHECK(peak > 0.05f && peak < 0.2f); // fixture is a 0.125-amplitude sine
    // A non-looping stream ends on its own and is reaped.
    CHECK(waitUntil([&] { renderPeak(m, 4); return !m.isPlaying(v); }));

    // Looping streams keep going well past the 0.5 s clip length.
    VoiceId l = m.playStream(openStream(DATA + "/tone.wav"), {.loop = true});
    int blocks = 0;
    waitUntil([&] { renderPeak(m, 1); return ++blocks > 150; }); // ~1.6 s of audio
    CHECK(m.isPlaying(l));
    m.stop(l);
    CHECK(!m.isPlaying(l));
    CHECK(m.playStream(nullptr) == INVALID_VOICE);
}

static void testComputeSpatialDistance() {
    Listener L; // at origin, looking down -Z
    SpatialParams p;
    p.minDistance = 1.0f;
    p.maxDistance = 11.0f;

    p.position = {0, 0, -1};
    CHECK_NEAR(computeSpatial(L, p).gain, 1.0f, 1e-5f);
    p.position = {0, 0, -3};
    CHECK_NEAR(computeSpatial(L, p).gain, 1.0f / 3.0f, 1e-5f); // inverse: 1/(1+(3-1))
    p.position = {0, 0, -100};
    CHECK_NEAR(computeSpatial(L, p).gain, 1.0f / 11.0f, 1e-5f); // clamped at maxDistance
    p.position = {0, 0, -0.2f};
    CHECK_NEAR(computeSpatial(L, p).gain, 1.0f, 1e-5f); // inside minDistance

    p.model = Attenuation::Linear;
    p.position = {0, 0, -6};
    CHECK_NEAR(computeSpatial(L, p).gain, 0.5f, 1e-5f);
    p.position = {0, 0, -50};
    CHECK_NEAR(computeSpatial(L, p).gain, 0.0f, 1e-5f);

    p.model = Attenuation::Exponential;
    p.rolloff = 2.0f;
    p.position = {0, 0, -2};
    CHECK_NEAR(computeSpatial(L, p).gain, 0.25f, 1e-5f);

    p.model = Attenuation::None;
    p.position = {0, 0, -40};
    CHECK_NEAR(computeSpatial(L, p).gain, 1.0f, 1e-5f);
}

static void testComputeSpatialPan() {
    Listener L;
    SpatialParams p;
    p.minDistance = 1.0f;
    p.position = {5, 0, 0};
    CHECK_NEAR(computeSpatial(L, p).pan, 1.0f, 1e-5f);
    p.position = {-5, 0, 0};
    CHECK_NEAR(computeSpatial(L, p).pan, -1.0f, 1e-5f);
    p.position = {0, 0, -5};
    CHECK_NEAR(computeSpatial(L, p).pan, 0.0f, 1e-5f);
    p.position = {0, 0, 5}; // behind: centered
    CHECK_NEAR(computeSpatial(L, p).pan, 0.0f, 1e-5f);
    p.position = {0, 5, 0}; // above: centered
    CHECK_NEAR(computeSpatial(L, p).pan, 0.0f, 1e-5f);
    p.position = {0.5f, 0, 0}; // inside minDistance: blended toward center
    CHECK_NEAR(computeSpatial(L, p).pan, 0.5f, 1e-5f);
    p.position = {};
    CHECK_NEAR(computeSpatial(L, p).pan, 0.0f, 1e-5f); // exactly at the listener

    // Turning the listener 90 degrees to the right (facing +X) moves a source
    // at +X to dead center and one at -Z to the left.
    L.forward = {1, 0, 0};
    p.position = {5, 0, 0};
    CHECK_NEAR(computeSpatial(L, p).pan, 0.0f, 1e-5f);
    p.position = {0, 0, -5};
    CHECK_NEAR(computeSpatial(L, p).pan, -1.0f, 1e-5f);
}

static void testComputeSpatialDoppler() {
    Listener L;
    SpatialParams p;
    p.position = {0, 0, -100};

    CHECK_NEAR(computeSpatial(L, p).pitch, 1.0f, 1e-6f); // stationary

    p.velocity = {0, 0, 34.3f}; // toward the listener at 0.1 c
    CHECK_NEAR(computeSpatial(L, p).pitch, 343.0f / (343.0f - 34.3f), 1e-4f);
    CHECK(computeSpatial(L, p).pitch > 1.0f);

    p.velocity = {0, 0, -34.3f}; // receding
    CHECK(computeSpatial(L, p).pitch < 1.0f);

    p.velocity = {0, 0, 34.3f};
    p.dopplerFactor = 0.0f;
    CHECK_NEAR(computeSpatial(L, p).pitch, 1.0f, 1e-6f);

    // Listener moving toward a stationary source also raises pitch.
    p.dopplerFactor = 1.0f;
    p.velocity = {};
    L.velocity = {0, 0, -34.3f};
    CHECK_NEAR(computeSpatial(L, p).pitch, (343.0f + 34.3f) / 343.0f, 1e-4f);

    // Supersonic speeds are clamped rather than producing inf/negative pitch.
    p.velocity = {0, 0, 5000.0f};
    L.velocity = {};
    float pitch = computeSpatial(L, p).pitch;
    CHECK(pitch > 1.0f && pitch < 4.0f);
}

static void testSpatialMixing() {
    AudioMixer m(48000);
    std::vector<float> out(2 * 64);
    Sound s = constant(1.0f, 4000);

    // Source hard right at distance 1: right channel only.
    m.setListener({});
    m.play3D(s, {.position = {1, 0, 0}});
    m.render(out.data(), 64);
    CHECK_NEAR(out[2 * 63], 0.0f, 1e-4f);
    CHECK_NEAR(out[2 * 63 + 1], 1.0f, 1e-4f);
    m.stopAll();

    // Farther away is quieter: inverse falloff at d=5 -> 1/5.
    m.play3D(s, {.position = {0, 0, -5}});
    m.render(out.data(), 64);
    float centered = 0.70710678f / 5.0f;
    CHECK_NEAR(out[2 * 63], centered, 1e-3f);
    CHECK_NEAR(out[2 * 63 + 1], centered, 1e-3f);
    m.stopAll();

    // Stereo clips are downmixed to mono for placement.
    m.play3D(makeSound(std::vector<float>(2 * 4000, 0.5f), 2, 48000), {.position = {1, 0, 0}});
    m.render(out.data(), 64);
    CHECK_NEAR(out[2 * 63], 0.0f, 1e-4f);
    CHECK_NEAR(out[2 * 63 + 1], 0.5f, 1e-3f);
    m.stopAll();

    // Moving the emitter or listener updates the mix.
    VoiceId v = m.play3D(s, {.position = {1, 0, 0}}, {.loop = true});
    m.render(out.data(), 64);
    m.setVoiceSpatial(v, {-1, 0, 0});
    m.render(out.data(), 64); // ramp block
    m.render(out.data(), 64);
    CHECK_NEAR(out[2 * 63], 1.0f, 1e-3f);
    CHECK_NEAR(out[2 * 63 + 1], 0.0f, 1e-3f);
    Listener turned;
    turned.forward = {-1, 0, 0}; // now facing the source: centered
    m.setListener(turned);
    CHECK_NEAR(m.listener().forward.x, -1.0f, 1e-6f);
    m.render(out.data(), 64);
    m.render(out.data(), 64);
    CHECK_NEAR(out[2 * 63], 0.70710678f, 1e-3f);
    m.setVoiceSpatial(INVALID_VOICE, {}); // no-op
    CHECK(m.play3D(nullptr, {}) == INVALID_VOICE);

    // setVoiceSpatial on a non-spatial voice does nothing.
    m.stopAll();
    VoiceId plain = m.play(s, {.pan = -1.0f});
    m.setVoiceSpatial(plain, {100, 0, 0});
    m.render(out.data(), 64);
    CHECK_NEAR(out[2 * 63], 1.0f, 1e-4f);
}

static void testSpatialDopplerChangesPlaybackRate() {
    AudioMixer m(48000);
    std::vector<float> out(2 * 400);
    // Receding fast: the 100-frame clip should take longer than 100 frames.
    Sound s = constant(1.0f, 100);
    m.play3D(s, {.position = {0, 0, -200}, .velocity = {0, 0, -100}, .model = Attenuation::None});
    m.render(out.data(), 110);
    CHECK(m.activeVoices() == 1); // would be done by now without Doppler
    m.stopAll();
    // Same sound approaching finishes sooner than its natural length.
    m.play3D(s, {.position = {0, 0, -200}, .velocity = {0, 0, 100}, .model = Attenuation::None});
    m.render(out.data(), 90);
    CHECK(m.activeVoices() == 0);
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
    testDecoders();
    testStreams();
    testPlayStream();
    testComputeSpatialDistance();
    testComputeSpatialPan();
    testComputeSpatialDoppler();
    testSpatialMixing();
    testSpatialDopplerChangesPlaybackRate();
    if (failures == 0) std::printf("audio_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
