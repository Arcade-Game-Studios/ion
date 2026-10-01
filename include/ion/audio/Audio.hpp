#pragma once

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
};

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

    // If all voices are busy, the oldest one is stolen.
    VoiceId play(const Sound& sound, const PlayParams& params = {});
    void stop(VoiceId id);
    void stopAll();
    bool isPlaying(VoiceId id) const;
    void setVoiceVolume(VoiceId id, float volume);
    void setVoicePan(VoiceId id, float pan);
    void setVoicePitch(VoiceId id, float pitch);

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
        AudioBus bus = AudioBus::Sfx;
    };

    uint32_t sampleRate_;
    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    VoiceId nextId_ = 1;
    float master_ = 1.0f;
    float bus_[2] = {1.0f, 1.0f};
};

// Platform output device (CoreAudio on macOS). On platforms without a
// backend, start() returns false and the mixer simply isn't pulled.
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
