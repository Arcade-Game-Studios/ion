#pragma once

#include <ion/math/Vector2.hpp>
#include <ion/math/Vector3.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ion {

//
// Ion audio: a software mixer plus a platform output device.
//
//   ion::Audio audio;
//   audio.initialize();
//   ion::Sound boom = ion::loadWav("boom.wav");        // or ion::makeSound(...)
//   ion::VoiceId v = audio.play(boom, {.volume = 0.8f, .pan = -0.3f});
//   ...
//   audio.shutdown();
//
// The mixer (AudioMixer) is platform independent and can be driven directly
// (render() into your own buffer), which is how the unit tests exercise it.
// Output is always interleaved stereo float.
//

// Decoded, immutable audio. Samples are interleaved float in [-1, 1].
struct SoundData {
    std::vector<float> samples;
    uint32_t channels = 1; // 1 or 2
    uint32_t sampleRate = 44100;

    size_t frames() const { return channels ? samples.size() / channels : 0; }
    float durationSeconds() const {
        return sampleRate ? (float)frames() / (float)sampleRate : 0.0f;
    }
};

using Sound = std::shared_ptr<const SoundData>;

// Wraps samples as a Sound. Returns null for an invalid layout.
Sound makeSound(std::vector<float> samples, uint32_t channels, uint32_t sampleRate);

// Decodes RIFF/WAVE: 8/16/24/32-bit PCM and 32-bit float, mono or stereo.
// Returns null (and logs) on unsupported or malformed data.
Sound loadWav(const std::string& path);
Sound loadWavFromMemory(const uint8_t* data, size_t size);

// Encodes as 16-bit PCM WAV.
std::vector<uint8_t> encodeWav(const SoundData& sound);

enum class AudioBus { Sfx = 0, Music = 1 };

struct PlayParams {
    float volume = 1.0f;
    float pan = 0.0f;   // -1 hard left .. +1 hard right
    float pitch = 1.0f; // playback speed multiplier (also shifts pitch)
    bool loop = false;
    AudioBus bus = AudioBus::Sfx;
    float fadeIn = 0.0f;    // seconds to ramp up from silence
    float startTime = 0.0f; // seconds into the sound to start from
};

// Distance attenuation and panning for a positioned sound. Full volume
// within minDistance, silent beyond maxDistance, linear in between. Pan
// follows the source's side relative to the listener and narrows toward the
// center as the source gets close.
struct SpatialGains {
    float volume = 1.0f;
    float pan = 0.0f;
};

SpatialGains computeSpatial(const Vector2& listener, const Vector2& source,
                            float minDistance, float maxDistance);
// listenerRight is the listener's unit right vector (e.g. Camera::right()).
SpatialGains computeSpatial(const Vector3& listener, const Vector3& listenerRight,
                            const Vector3& source, float minDistance, float maxDistance);

// Returns `base` with volume scaled and pan replaced for a 2D source.
PlayParams spatialize(PlayParams base, const Vector2& listener, const Vector2& source,
                      float minDistance, float maxDistance);

// 0 is never a valid voice. Ids are not reused, so a stale id is harmless.
using VoiceId = uint64_t;
constexpr VoiceId INVALID_VOICE = 0;

// Thread-safe software mixer. play()/stop()/etc. may be called from the game
// thread while render() runs on the audio thread.
class AudioMixer {
public:
    static constexpr size_t MAX_VOICES = 64;

    explicit AudioMixer(uint32_t outputSampleRate = 48000);

    uint32_t sampleRate() const { return sampleRate_; }

    // If all voices are busy, a voice is stolen: the oldest non-looping
    // sound effect if there is one (so music keeps playing), else the oldest.
    VoiceId play(const Sound& sound, const PlayParams& params = {});
    void stop(VoiceId id);
    void stopAll();
    void stopBus(AudioBus bus);
    // True while the voice exists (including while paused).
    bool isPlaying(VoiceId id) const;
    void setVoiceVolume(VoiceId id, float volume);
    void setVoicePan(VoiceId id, float pan);
    void setVoicePitch(VoiceId id, float pitch);
    void setVoiceLoop(VoiceId id, bool loop);

    // Paused voices keep their position and resume where they left off.
    void pause(VoiceId id);
    void resume(VoiceId id);
    bool isPaused(VoiceId id) const;
    void pauseAll();
    void resumeAll();

    // Ramps the voice to silence over `seconds`, then stops it.
    void fadeOut(VoiceId id, float seconds);

    // Playback position in seconds (0 for unknown voices).
    float voiceTime(VoiceId id) const;

    void setMasterVolume(float volume);
    float masterVolume() const;
    void setBusVolume(AudioBus bus, float volume);
    float busVolume(AudioBus bus) const;

    size_t activeVoices() const;

    // Mixes `frames` stereo frames (frames * 2 floats) into out, overwriting it.
    void render(float* out, uint32_t frames);

private:
    struct Voice {
        VoiceId id = INVALID_VOICE;
        Sound sound;
        double position = 0.0; // in source frames
        float volume = 1.0f;
        float pan = 0.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool paused = false;
        AudioBus bus = AudioBus::Sfx;
        // Fade multiplier, moved toward fadeTarget by fadeStep per frame.
        float fade = 1.0f;
        float fadeTarget = 1.0f;
        float fadeStep = 0.0f;
        bool stopAfterFade = false;
        // Channel gains applied at the end of the last buffer; the next
        // buffer ramps from these so volume/pan changes don't click.
        float lastLeft = 0.0f;
        float lastRight = 0.0f;
        bool hasLastGain = false;
    };

    Voice* find_(VoiceId id);
    const Voice* find_(VoiceId id) const;

    uint32_t sampleRate_;
    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    VoiceId nextId_ = 1;
    float master_ = 1.0f;
    float bus_[2] = {1.0f, 1.0f};
};

// Platform output device: CoreAudio on macOS, ALSA on Linux (loaded at
// runtime; ION_AUDIO_DEVICE selects the PCM device), waveOut on Windows. If
// no device can be opened, start() returns false and the mixer simply isn't
// pulled.
class AudioDevice {
public:
    AudioDevice();
    ~AudioDevice();
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // Starts pulling audio from mixer; the mixer must outlive stop().
    bool start(AudioMixer* mixer);
    void stop();
    bool isRunning() const;

private:
    class Impl;
    Impl* impl_ = nullptr;
};

// Convenience: a mixer wired to the default output device.
class Audio {
public:
    Audio() = default;
    ~Audio() { shutdown(); }
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Returns false if no output device could be opened. The mixer still
    // works (silently) so game code doesn't need to special-case it.
    bool initialize();
    void shutdown();
    bool hasDevice() const { return device_ && device_->isRunning(); }

    AudioMixer& mixer() { return mixer_; }

    VoiceId play(const Sound& s, const PlayParams& p = {}) { return mixer_.play(s, p); }
    void stop(VoiceId id) { mixer_.stop(id); }

private:
    AudioMixer mixer_{48000};
    std::unique_ptr<AudioDevice> device_;
};

} // namespace ion
