#include <ion/audio/Audio.hpp>
#include <ion/core/Log.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace ion {

namespace {

constexpr float kPi = 3.14159265358979f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Equal-power pan; center gives ~0.707 per channel.
void panGains(float pan, float& left, float& right) {
    float angle = (clampf(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    left = std::cos(angle);
    right = std::sin(angle);
}

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void wr16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back((uint8_t)(v & 0xFF));
    o.push_back((uint8_t)(v >> 8));
}
void wr32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; i++) o.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}

} // namespace

// --- Sound / WAV ----------------------------------------------------------

Sound makeSound(std::vector<float> samples, uint32_t channels, uint32_t sampleRate) {
    if ((channels != 1 && channels != 2) || sampleRate == 0 ||
        samples.size() % channels != 0) {
        return nullptr;
    }
    auto data = std::make_shared<SoundData>();
    data->samples = std::move(samples);
    data->channels = channels;
    data->sampleRate = sampleRate;
    return data;
}

Sound loadWavFromMemory(const uint8_t* data, size_t size) {
    if (!data || size < 12 || std::memcmp(data, "RIFF", 4) != 0 ||
        std::memcmp(data + 8, "WAVE", 4) != 0) {
        ION_LOG_ERROR("audio: not a RIFF/WAVE file");
        return nullptr;
    }

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmSize = 0;
    bool haveFmt = false;

    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t* chunk = data + pos;
        size_t chunkSize = rd32(chunk + 4);
        const uint8_t* body = chunk + 8;
        size_t avail = size - (pos + 8);
        size_t usable = std::min(chunkSize, avail); // tolerate truncated files
        if (std::memcmp(chunk, "fmt ", 4) == 0 && usable >= 16) {
            format = rd16(body);
            channels = rd16(body + 2);
            rate = rd32(body + 4);
            bits = rd16(body + 14);
            if (format == 0xFFFE && usable >= 26) {
                format = rd16(body + 24); // WAVE_FORMAT_EXTENSIBLE sub-format
            }
            haveFmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            pcm = body;
            pcmSize = usable;
        }
        pos += 8 + chunkSize + (chunkSize & 1); // chunks are word-aligned
    }

    if (!haveFmt || !pcm) {
        ION_LOG_ERROR("audio: WAV is missing fmt or data chunk");
        return nullptr;
    }
    if (channels < 1 || channels > 2 || rate == 0) {
        ION_LOG_ERROR("audio: unsupported WAV layout (%u channels, %u Hz)", channels, rate);
        return nullptr;
    }
    bool isFloat = (format == 3 && bits == 32);
    bool isPcm = (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32));
    if (!isFloat && !isPcm) {
        ION_LOG_ERROR("audio: unsupported WAV encoding (format %u, %u bits)", format, bits);
        return nullptr;
    }

    size_t bytes = bits / 8;
    size_t count = pcmSize / bytes;
    count -= count % channels;
    std::vector<float> out(count);
    for (size_t i = 0; i < count; i++) {
        const uint8_t* p = pcm + i * bytes;
        float v;
        if (isFloat) {
            float f;
            uint32_t u = rd32(p);
            std::memcpy(&f, &u, 4);
            v = f;
        } else if (bits == 8) {
            v = ((int)p[0] - 128) / 128.0f;
        } else if (bits == 16) {
            v = (int16_t)rd16(p) / 32768.0f;
        } else if (bits == 24) {
            int32_t s = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24);
            v = (float)(s >> 8) / 8388608.0f;
        } else {
            v = (float)((int32_t)rd32(p)) / 2147483648.0f;
        }
        out[i] = v;
    }
    return makeSound(std::move(out), channels, rate);
}

Sound loadWav(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ION_LOG_ERROR("audio: cannot open '%s'", path.c_str());
        return nullptr;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    return loadWavFromMemory(bytes.data(), bytes.size());
}

std::vector<uint8_t> encodeWav(const SoundData& sound) {
    uint32_t dataBytes = (uint32_t)(sound.samples.size() * 2);
    std::vector<uint8_t> o;
    o.reserve(44 + dataBytes);
    o.insert(o.end(), {'R', 'I', 'F', 'F'});
    wr32(o, 36 + dataBytes);
    o.insert(o.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    wr32(o, 16);
    wr16(o, 1);
    wr16(o, (uint16_t)sound.channels);
    wr32(o, sound.sampleRate);
    wr32(o, sound.sampleRate * sound.channels * 2);
    wr16(o, (uint16_t)(sound.channels * 2));
    wr16(o, 16);
    o.insert(o.end(), {'d', 'a', 't', 'a'});
    wr32(o, dataBytes);
    for (float s : sound.samples) {
        wr16(o, (uint16_t)(int16_t)std::lrintf(clampf(s, -1.0f, 1.0f) * 32767.0f));
    }
    return o;
}

// --- Spatial helpers --------------------------------------------------------

namespace {

float distanceGain(float distance, float minDistance, float maxDistance) {
    if (distance <= minDistance) {
        return 1.0f;
    }
    if (distance >= maxDistance || maxDistance <= minDistance) {
        return 0.0f;
    }
    return 1.0f - (distance - minDistance) / (maxDistance - minDistance);
}

// Sources inside minDistance pan toward the center so a sound right on top
// of the listener doesn't jump between ears.
float spatialPan(float side, float distance, float minDistance) {
    float width = minDistance > 0.0f ? std::min(1.0f, distance / minDistance) : 1.0f;
    return clampf(side * width, -1.0f, 1.0f);
}

} // namespace

SpatialGains computeSpatial(const Vector2& listener, const Vector2& source,
                            float minDistance, float maxDistance) {
    Vector2 delta = source - listener;
    float distance = delta.length();
    SpatialGains g;
    g.volume = distanceGain(distance, minDistance, maxDistance);
    g.pan = distance > 0.0f ? spatialPan(delta.x / distance, distance, minDistance) : 0.0f;
    return g;
}

SpatialGains computeSpatial(const Vector3& listener, const Vector3& listenerRight,
                            const Vector3& source, float minDistance, float maxDistance) {
    Vector3 delta = source - listener;
    float distance = delta.length();
    SpatialGains g;
    g.volume = distanceGain(distance, minDistance, maxDistance);
    g.pan = distance > 0.0f
                ? spatialPan(Vector3::dot(delta / distance, listenerRight), distance, minDistance)
                : 0.0f;
    return g;
}

PlayParams spatialize(PlayParams base, const Vector2& listener, const Vector2& source,
                      float minDistance, float maxDistance) {
    SpatialGains g = computeSpatial(listener, source, minDistance, maxDistance);
    base.volume *= g.volume;
    base.pan = g.pan;
    return base;
}

// --- Mixer ----------------------------------------------------------------

AudioMixer::AudioMixer(uint32_t outputSampleRate)
    : sampleRate_(outputSampleRate ? outputSampleRate : 48000) {
    voices_.reserve(MAX_VOICES);
}

AudioMixer::Voice* AudioMixer::find_(VoiceId id) {
    for (Voice& v : voices_) {
        if (v.id == id) {
            return &v;
        }
    }
    return nullptr;
}

const AudioMixer::Voice* AudioMixer::find_(VoiceId id) const {
    for (const Voice& v : voices_) {
        if (v.id == id) {
            return &v;
        }
    }
    return nullptr;
}

VoiceId AudioMixer::play(const Sound& sound, const PlayParams& params) {
    if (!sound || sound->frames() == 0) {
        return INVALID_VOICE;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (voices_.size() >= MAX_VOICES) {
        // Steal the oldest one-shot sound effect; fall back to the oldest.
        auto victim = std::find_if(voices_.begin(), voices_.end(), [](const Voice& v) {
            return !v.loop && v.bus == AudioBus::Sfx;
        });
        voices_.erase(victim != voices_.end() ? victim : voices_.begin());
    }
    Voice v;
    v.id = nextId_++;
    v.sound = sound;
    v.volume = std::max(0.0f, params.volume);
    v.pan = clampf(params.pan, -1.0f, 1.0f);
    v.pitch = std::max(0.01f, params.pitch);
    v.loop = params.loop;
    v.bus = params.bus;
    double start = std::max(0.0f, params.startTime) * (double)sound->sampleRate;
    double total = (double)sound->frames();
    v.position = v.loop ? std::fmod(start, total) : std::min(start, total);
    if (params.fadeIn > 0.0f) {
        v.fade = 0.0f;
        v.fadeTarget = 1.0f;
        v.fadeStep = 1.0f / (params.fadeIn * (float)sampleRate_);
    }
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

void AudioMixer::stop(VoiceId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [&](const Voice& v) { return v.id == id; }),
                  voices_.end());
}

void AudioMixer::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.clear();
}

void AudioMixer::stopBus(AudioBus bus) {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [&](const Voice& v) { return v.bus == bus; }),
                  voices_.end());
}

bool AudioMixer::isPlaying(VoiceId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find_(id) != nullptr;
}

void AudioMixer::setVoiceVolume(VoiceId id, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find_(id)) {
        v->volume = std::max(0.0f, volume);
    }
}

void AudioMixer::setVoicePan(VoiceId id, float pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find_(id)) {
        v->pan = clampf(pan, -1.0f, 1.0f);
    }
}

void AudioMixer::setVoicePitch(VoiceId id, float pitch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find_(id)) {
        v->pitch = std::max(0.01f, pitch);
    }
}

void AudioMixer::setVoiceLoop(VoiceId id, bool loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find_(id)) {
        v->loop = loop;
    }
}

void AudioMixer::pause(VoiceId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find_(id)) {
        v->paused = true;
    }
}

void AudioMixer::resume(VoiceId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = find_(id)) {
        v->paused = false;
    }
}

bool AudioMixer::isPaused(VoiceId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Voice* v = find_(id);
    return v && v->paused;
}

void AudioMixer::pauseAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_) {
        v.paused = true;
    }
}

void AudioMixer::resumeAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_) {
        v.paused = false;
    }
}

void AudioMixer::fadeOut(VoiceId id, float seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    Voice* v = find_(id);
    if (!v) {
        return;
    }
    v->stopAfterFade = true;
    v->fadeTarget = 0.0f;
    if (seconds <= 0.0f) {
        v->fade = 0.0f;
        v->fadeStep = 1.0f;
    } else {
        v->fadeStep = std::max(v->fade, 1e-6f) / (seconds * (float)sampleRate_);
    }
}

float AudioMixer::voiceTime(VoiceId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Voice* v = find_(id);
    if (!v) {
        return 0.0f;
    }
    return (float)(v->position / (double)v->sound->sampleRate);
}

void AudioMixer::setMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    master_ = std::max(0.0f, volume);
}
float AudioMixer::masterVolume() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return master_;
}
void AudioMixer::setBusVolume(AudioBus bus, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    bus_[(int)bus] = std::max(0.0f, volume);
}
float AudioMixer::busVolume(AudioBus bus) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bus_[(int)bus];
}

size_t AudioMixer::activeVoices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return voices_.size();
}

void AudioMixer::render(float* out, uint32_t frames) {
    std::fill(out, out + (size_t)frames * 2, 0.0f);
    if (frames == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (Voice& v : voices_) {
        if (v.paused) {
            // Resume from silence rather than jumping straight to full gain.
            v.lastLeft = v.lastRight = 0.0f;
            v.hasLastGain = true;
            continue;
        }
        const SoundData& s = *v.sound;
        const size_t total = s.frames();
        const double totalD = (double)total;
        const double step = (double)v.pitch * (double)s.sampleRate / (double)sampleRate_;
        const float gain = v.volume * master_ * bus_[(int)v.bus];
        float gl, gr;
        panGains(v.pan, gl, gr);
        // Mono is split across both channels by the pan law; stereo material
        // is balanced with the same gains, normalised so center is unity.
        if (s.channels == 2) {
            gl *= 1.41421356f;
            gr *= 1.41421356f;
        }
        gl *= gain;
        gr *= gain;
        if (!v.hasLastGain) {
            v.lastLeft = gl;
            v.lastRight = gr;
            v.hasLastGain = true;
        }
        // Linear ramp from the previous buffer's gains to the new ones.
        const float rampL = (gl - v.lastLeft) / (float)frames;
        const float rampR = (gr - v.lastRight) / (float)frames;
        float curL = v.lastLeft;
        float curR = v.lastRight;

        double pos = v.position;
        bool finished = false;
        for (uint32_t i = 0; i < frames; i++) {
            if (pos >= totalD) {
                if (!v.loop) {
                    finished = true;
                    break;
                }
                // fmod, not a single subtraction: with a high pitch on a
                // short sound one step can span several loop lengths.
                pos = std::fmod(pos, totalD);
            }
            size_t i0 = (size_t)pos;
            if (i0 >= total) { // guards float rounding at the boundary
                i0 = total - 1;
            }
            size_t i1 = i0 + 1;
            if (i1 >= total) {
                i1 = v.loop ? 0 : i0;
            }
            float t = (float)(pos - (double)i0);
            float l, r;
            if (s.channels == 1) {
                l = r = s.samples[i0] + (s.samples[i1] - s.samples[i0]) * t;
            } else {
                l = s.samples[i0 * 2] + (s.samples[i1 * 2] - s.samples[i0 * 2]) * t;
                r = s.samples[i0 * 2 + 1] +
                    (s.samples[i1 * 2 + 1] - s.samples[i0 * 2 + 1]) * t;
            }
            curL += rampL;
            curR += rampR;
            if (v.fade != v.fadeTarget) {
                v.fade = v.fade < v.fadeTarget ? std::min(v.fadeTarget, v.fade + v.fadeStep)
                                               : std::max(v.fadeTarget, v.fade - v.fadeStep);
            }
            out[i * 2] += l * curL * v.fade;
            out[i * 2 + 1] += r * curR * v.fade;
            pos += step;
            if (v.stopAfterFade && v.fade <= 0.0f) {
                finished = true;
                break;
            }
        }
        v.position = pos;
        v.lastLeft = gl;
        v.lastRight = gr;
        if (finished || (!v.loop && pos >= totalD)) {
            v.id = INVALID_VOICE; // reaped below
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return v.id == INVALID_VOICE; }),
                  voices_.end());

    for (uint32_t i = 0; i < frames * 2; i++) {
        out[i] = clampf(out[i], -1.0f, 1.0f);
    }
}

} // namespace ion
