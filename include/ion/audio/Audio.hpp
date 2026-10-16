#pragma once

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

// Fully decodes Ogg Vorbis / MP3 (mono or stereo) into memory. Best for short
// effects; use openStream() + playStream() for long music.
Sound loadOgg(const std::string& path);
Sound loadOggFromMemory(const uint8_t* data, size_t size);
Sound loadMp3(const std::string& path);
Sound loadMp3FromMemory(const uint8_t* data, size_t size);

// Picks the decoder from the file extension (.wav, .ogg, .mp3).
Sound loadSound(const std::string& path);

// A pull-based decoder for audio that is too long to hold in memory.
class AudioStream {
public:
    virtual ~AudioStream() = default;
    virtual uint32_t channels() const = 0; // 1 or 2
    virtual uint32_t sampleRate() const = 0;
    // Writes up to `frames` interleaved frames; returns the number written,
    // 0 at end of stream.
    virtual size_t read(float* out, size_t frames) = 0;
    virtual bool rewind() = 0;
};

// Opens a .wav / .ogg / .mp3 file for streaming. Returns null on failure.
std::unique_ptr<AudioStream> openStream(const std::string& path);

enum class AudioBus { Sfx = 0, Music = 1 };

struct PlayParams {
    float volume = 1.0f;
    float pan = 0.0f;   // -1 hard left .. +1 hard right
    float pitch = 1.0f; // playback speed multiplier (also shifts pitch)
    bool loop = false;
    AudioBus bus = AudioBus::Sfx;
};

// Distance falloff models for 3D sounds.
enum class Attenuation {
    None,            // no distance falloff (pan and Doppler still apply)
    InverseClamped,  // min / (min + rolloff * (clamp(d) - min)); the default
    Linear,          // 1 at minDistance down to 0 at maxDistance
    Exponential,     // (d / min) ^ -rolloff
};

struct SpatialParams {
    Vector3 position;
    Vector3 velocity;          // world units per second; drives Doppler
    float minDistance = 1.0f;  // full volume inside this radius
    float maxDistance = 50.0f; // no further attenuation beyond this
    float rolloff = 1.0f;
    Attenuation model = Attenuation::InverseClamped;
    float dopplerFactor = 1.0f; // 0 disables Doppler
};

// The point sounds are heard from. forward/up define its orientation
// (right = forward x up, so the default looks down -Z with +Y up, matching
// ion::Camera).
struct Listener {
    Vector3 position;
    Vector3 velocity;
    Vector3 forward = {0.0f, 0.0f, -1.0f};
    Vector3 up = {0.0f, 1.0f, 0.0f};
};

struct SpatialResult {
    float gain = 1.0f;  // distance attenuation
    float pan = 0.0f;   // -1 (left) .. +1 (right)
    float pitch = 1.0f; // Doppler multiplier
};

// How a source at `source` is heard by `listener`. Pure function; the mixer
// uses it each audio block, and it is exposed for tests and custom mixing.
SpatialResult computeSpatial(const Listener& listener, const SpatialParams& source,
                             float speedOfSound = 343.0f);

// 0 is never a valid voice. Ids are not reused, so a stale id is harmless.
using VoiceId = uint64_t;
constexpr VoiceId INVALID_VOICE = 0;

// Thread-safe software mixer. play()/stop()/etc. may be called from the game
// thread while render() runs on the audio thread.
struct StreamState; // internal: ring buffer + decode thread

class AudioMixer {
public:
    static constexpr size_t MAX_VOICES = 64;

    explicit AudioMixer(uint32_t outputSampleRate = 48000);

    uint32_t sampleRate() const { return sampleRate_; }

    // If all voices are busy, the oldest one is stolen.
    VoiceId play(const Sound& sound, const PlayParams& params = {});
    // Plays a sound positioned in 3D. Stereo sounds are downmixed to mono so
    // they can be placed. Update with setVoiceSpatial() as the emitter moves.
    VoiceId play3D(const Sound& sound, const SpatialParams& spatial,
                   const PlayParams& params = {});
    // Streams audio decoded on a background thread (pitch is not applied).
    // The mixer takes ownership. Use bus Music for background tracks.
    VoiceId playStream(std::unique_ptr<AudioStream> stream, const PlayParams& params = {});
    void stop(VoiceId id);
    void stopAll();
    bool isPlaying(VoiceId id) const;
    void setVoiceVolume(VoiceId id, float volume);
    void setVoicePan(VoiceId id, float pan);
    void setVoicePitch(VoiceId id, float pitch);
    // Moves a 3D voice (no-op for non-spatial voices).
    void setVoiceSpatial(VoiceId id, const Vector3& position, const Vector3& velocity = {});

    void setListener(const Listener& listener);
    Listener listener() const;

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
        std::shared_ptr<StreamState> stream; // set for streamed voices
        double position = 0.0; // in source frames
        float volume = 1.0f;
        float pan = 0.0f;
        float pitch = 1.0f;
        bool loop = false;
        AudioBus bus = AudioBus::Sfx;
        bool spatial = false;
        SpatialParams spatialParams;
        // Gains used at the end of the previous block, so changes (volume,
        // pan, movement) ramp across a block instead of clicking.
        float lastGainL = 0.0f;
        float lastGainR = 0.0f;
        bool primed = false;
    };

    VoiceId addVoice_(Voice&& v);

    uint32_t sampleRate_;
    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    VoiceId nextId_ = 1;
    Listener listener_;
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

    VoiceId play3D(const Sound& s, const SpatialParams& sp, const PlayParams& p = {}) {
        return mixer_.play3D(s, sp, p);
    }
    void setListener(const Listener& l) { mixer_.setListener(l); }

    // Streams a music file (.wav/.ogg/.mp3) on the Music bus, looping by
    // default. Returns INVALID_VOICE if the file can't be opened.
    VoiceId playMusic(const std::string& path, float volume = 1.0f, bool loop = true);

private:
    AudioMixer mixer_{48000};
    std::unique_ptr<AudioDevice> device_;
};

} // namespace ion
