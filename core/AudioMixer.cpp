#include <ion/audio/Audio.hpp>
#include <ion/core/Log.hpp>

#include "AudioInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <fstream>
#include <thread>

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

using audio_detail::rd16;
using audio_detail::rd32;

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
    bool isFloat = false;
    if (!audio_detail::supportedPcm(format, bits, isFloat)) {
        ION_LOG_ERROR("audio: unsupported WAV encoding (format %u, %u bits)", format, bits);
        return nullptr;
    }

    size_t count = pcmSize / (bits / 8);
    count -= count % channels;
    std::vector<float> out(count);
    audio_detail::convertPcm(pcm, count, bits, isFloat, out.data());
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

// --- Spatial --------------------------------------------------------------

SpatialResult computeSpatial(const Listener& listener, const SpatialParams& src,
                             float speedOfSound) {
    SpatialResult r;
    Vector3 toSource = src.position - listener.position;
    float dist = toSource.length();

    // Distance attenuation.
    float minD = std::max(src.minDistance, 1e-4f);
    float maxD = std::max(src.maxDistance, minD);
    float d = clampf(dist, minD, maxD);
    switch (src.model) {
    case Attenuation::None:
        r.gain = 1.0f;
        break;
    case Attenuation::InverseClamped:
        r.gain = minD / (minD + src.rolloff * (d - minD));
        break;
    case Attenuation::Linear:
        r.gain = maxD > minD ? 1.0f - clampf(src.rolloff, 0.0f, 1.0f) * (d - minD) / (maxD - minD)
                             : 1.0f;
        break;
    case Attenuation::Exponential:
        r.gain = std::pow(d / minD, -src.rolloff);
        break;
    }
    r.gain = clampf(r.gain, 0.0f, 1.0f);

    if (dist < 1e-5f) {
        return r; // on top of the listener: centered, no Doppler
    }
    Vector3 dir = toSource / dist;

    // Pan from the source's bearing relative to the listener's right vector.
    // Sources inside minDistance are blended toward the center so a sound
    // passing right by the head doesn't snap between ears.
    Vector3 right = Vector3::cross(listener.forward, listener.up);
    float rl = right.length();
    if (rl > 1e-6f) {
        right = right / rl;
        r.pan = clampf(Vector3::dot(dir, right), -1.0f, 1.0f) * std::min(1.0f, dist / minD);
    }

    // Doppler: f' = f * (c + v_listener) / (c + v_source), velocities projected on
    // the listener->source axis (listener toward source, source away from listener).
    if (src.dopplerFactor > 0.0f && speedOfSound > 0.0f) {
        float limit = speedOfSound * 0.5f; // avoid singularities near/above Mach 1
        float vl = clampf(Vector3::dot(listener.velocity, dir) * src.dopplerFactor, -limit, limit);
        float vs = clampf(Vector3::dot(src.velocity, dir) * src.dopplerFactor, -limit, limit);
        r.pitch = (speedOfSound + vl) / (speedOfSound + vs);
    }
    return r;
}

// --- Streaming ------------------------------------------------------------

// Decodes on a worker thread into a ring of output-rate stereo frames so the
// audio callback never touches the disk or a decoder.
struct StreamState {
    StreamState(std::unique_ptr<AudioStream> src, uint32_t outRate, bool loop)
        : decoder(std::move(src)), loop(loop),
          step((double)decoder->sampleRate() / (double)outRate),
          ring(kRingFrames * 2) {
        worker = std::thread([this] { run(); });
    }

    ~StreamState() {
        {
            std::lock_guard<std::mutex> lock(m);
            stop = true;
        }
        cv.notify_all();
        worker.join();
    }

    // Copies up to `frames` stereo frames out of the ring; returns the count.
    size_t pull(float* dst, size_t frames) {
        size_t got;
        {
            std::lock_guard<std::mutex> lock(m);
            got = std::min(frames, count);
            for (size_t i = 0; i < got; i++) {
                dst[i * 2] = ring[readPos * 2];
                dst[i * 2 + 1] = ring[readPos * 2 + 1];
                readPos = (readPos + 1) % kRingFrames;
            }
            count -= got;
        }
        if (got) cv.notify_one();
        return got;
    }

    bool drained() {
        std::lock_guard<std::mutex> lock(m);
        return done && count == 0;
    }

    static constexpr size_t kRingFrames = 1 << 16; // ~1.4 s at 48 kHz
    static constexpr size_t kChunk = 4096;

    std::unique_ptr<AudioStream> decoder;
    bool loop;
    double step; // source frames per output frame

    std::mutex m;
    std::condition_variable cv;
    std::vector<float> ring;
    size_t readPos = 0, writePos = 0, count = 0;
    bool done = false, stop = false;
    std::thread worker;

private:
    void run() {
        const uint32_t ch = decoder->channels();
        std::vector<float> raw(kChunk * ch);
        std::vector<float> src; // converted stereo source frames awaiting resampling
        std::vector<float> out;
        double pos = 0.0;
        bool atStart = true;

        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&] { return stop || kRingFrames - count >= kChunk * 2; });
                if (stop) return;
            }

            size_t n = decoder->read(raw.data(), kChunk);
            if (n == 0) {
                if (loop && !atStart && decoder->rewind()) {
                    atStart = true;
                    continue;
                }
                std::lock_guard<std::mutex> lock(m);
                done = true;
                return;
            }
            atStart = false;

            for (size_t i = 0; i < n; i++) {
                float l = raw[i * ch];
                float r = ch == 2 ? raw[i * ch + 1] : l;
                src.push_back(l);
                src.push_back(r);
            }

            // Linear resample to the output rate (step == 1 passes through).
            out.clear();
            size_t srcFrames = src.size() / 2;
            while (pos + 1.0 < (double)srcFrames) {
                size_t i0 = (size_t)pos;
                float t = (float)(pos - (double)i0);
                out.push_back(src[i0 * 2] + (src[i0 * 2 + 2] - src[i0 * 2]) * t);
                out.push_back(src[i0 * 2 + 1] + (src[i0 * 2 + 3] - src[i0 * 2 + 1]) * t);
                pos += step;
            }
            size_t consumed = std::min((size_t)pos, srcFrames);
            src.erase(src.begin(), src.begin() + consumed * 2);
            pos -= (double)consumed;

            std::lock_guard<std::mutex> lock(m);
            for (size_t i = 0; i < out.size() / 2; i++) {
                ring[writePos * 2] = out[i * 2];
                ring[writePos * 2 + 1] = out[i * 2 + 1];
                writePos = (writePos + 1) % kRingFrames;
                count++;
            }
        }
    }
};

// --- Mixer ----------------------------------------------------------------

AudioMixer::AudioMixer(uint32_t outputSampleRate)
    : sampleRate_(outputSampleRate ? outputSampleRate : 48000) {
    voices_.reserve(MAX_VOICES);
}

VoiceId AudioMixer::addVoice_(Voice&& v) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (voices_.size() >= MAX_VOICES) {
        voices_.erase(voices_.begin()); // oldest
    }
    v.id = nextId_++;
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

VoiceId AudioMixer::play(const Sound& sound, const PlayParams& params) {
    if (!sound || sound->frames() == 0) {
        return INVALID_VOICE;
    }
    Voice v;
    v.sound = sound;
    v.volume = std::max(0.0f, params.volume);
    v.pan = clampf(params.pan, -1.0f, 1.0f);
    v.pitch = std::max(0.01f, params.pitch);
    v.loop = params.loop;
    v.bus = params.bus;
    return addVoice_(std::move(v));
}

VoiceId AudioMixer::play3D(const Sound& sound, const SpatialParams& spatial,
                           const PlayParams& params) {
    if (!sound || sound->frames() == 0) {
        return INVALID_VOICE;
    }
    Voice v;
    v.sound = sound;
    v.volume = std::max(0.0f, params.volume);
    v.pitch = std::max(0.01f, params.pitch);
    v.loop = params.loop;
    v.bus = params.bus;
    v.spatial = true;
    v.spatialParams = spatial;
    return addVoice_(std::move(v));
}

VoiceId AudioMixer::playStream(std::unique_ptr<AudioStream> stream, const PlayParams& params) {
    if (!stream || stream->channels() < 1 || stream->sampleRate() == 0) {
        return INVALID_VOICE;
    }
    Voice v;
    v.stream = std::make_shared<StreamState>(std::move(stream), sampleRate_, params.loop);
    v.volume = std::max(0.0f, params.volume);
    v.pan = clampf(params.pan, -1.0f, 1.0f);
    v.loop = params.loop;
    v.bus = params.bus;
    return addVoice_(std::move(v));
}

void AudioMixer::stop(VoiceId id) {
    // Removed voices (and their stream threads) are destroyed outside the lock.
    std::vector<Voice> dead;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = voices_.begin(); it != voices_.end();) {
            if (it->id == id) {
                dead.push_back(std::move(*it));
                it = voices_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void AudioMixer::stopAll() {
    std::vector<Voice> dead;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dead.swap(voices_);
        voices_.reserve(MAX_VOICES);
    }
}

bool AudioMixer::isPlaying(VoiceId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(voices_.begin(), voices_.end(),
                       [&](const Voice& v) { return v.id == id; });
}

#define ION_WITH_VOICE(id, v, body)                                 \
    do {                                                            \
        std::lock_guard<std::mutex> lock(mutex_);                   \
        for (Voice& v : voices_) {                                  \
            if (v.id == (id)) {                                     \
                body;                                               \
                break;                                              \
            }                                                       \
        }                                                           \
    } while (0)

void AudioMixer::setVoiceVolume(VoiceId id, float volume) {
    ION_WITH_VOICE(id, v, v.volume = std::max(0.0f, volume));
}
void AudioMixer::setVoicePan(VoiceId id, float pan) {
    ION_WITH_VOICE(id, v, v.pan = clampf(pan, -1.0f, 1.0f));
}
void AudioMixer::setVoicePitch(VoiceId id, float pitch) {
    ION_WITH_VOICE(id, v, v.pitch = std::max(0.01f, pitch));
}
void AudioMixer::setVoiceSpatial(VoiceId id, const Vector3& position, const Vector3& velocity) {
    ION_WITH_VOICE(id, v, if (v.spatial) {
        v.spatialParams.position = position;
        v.spatialParams.velocity = velocity;
    });
}
#undef ION_WITH_VOICE

void AudioMixer::setListener(const Listener& listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listener_ = listener;
}
Listener AudioMixer::listener() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return listener_;
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

    std::vector<Voice> dead; // destroyed after unlocking (joins stream threads)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const float inv = 1.0f / (float)frames;
        for (Voice& v : voices_) {
            // Target gains for the end of this block.
            float pan = v.pan;
            float gain = v.volume * master_ * bus_[(int)v.bus];
            float pitch = v.pitch;
            if (v.spatial) {
                SpatialResult sr = computeSpatial(listener_, v.spatialParams);
                pan = sr.pan;
                gain *= sr.gain;
                pitch *= sr.pitch;
            }
            float gl, gr;
            panGains(pan, gl, gr);
            // Mono sources are split by the pan law. Stereo sources (and
            // streams, which are always stereo) are balanced with the same
            // gains, normalised so center is unity.
            const bool stereoSource = v.stream || (!v.spatial && v.sound->channels == 2);
            if (stereoSource) {
                gl *= 1.41421356f;
                gr *= 1.41421356f;
            }
            gl *= gain;
            gr *= gain;
            if (!v.primed) {
                v.lastGainL = gl;
                v.lastGainR = gr;
                v.primed = true;
            }
            const float dl = (gl - v.lastGainL) * inv;
            const float dr = (gr - v.lastGainR) * inv;
            float curL = v.lastGainL, curR = v.lastGainR;

            bool finished = false;
            if (v.stream) {
                float buf[512 * 2];
                uint32_t done = 0;
                while (done < frames) {
                    uint32_t chunk = std::min<uint32_t>(512, frames - done);
                    size_t got = v.stream->pull(buf, chunk);
                    for (size_t i = 0; i < got; i++) {
                        curL += dl;
                        curR += dr;
                        out[(done + i) * 2] += buf[i * 2] * curL;
                        out[(done + i) * 2 + 1] += buf[i * 2 + 1] * curR;
                    }
                    if (got < chunk) {
                        // Underrun (or end). Keep ramping so gains stay consistent.
                        curL += dl * (float)(frames - done - got);
                        curR += dr * (float)(frames - done - got);
                        break;
                    }
                    done += chunk;
                }
                finished = v.stream->drained();
            } else {
                const SoundData& s = *v.sound;
                const size_t total = s.frames();
                const double step = (double)pitch * (double)s.sampleRate / (double)sampleRate_;
                const bool mono = s.channels == 1;
                const bool downmix = v.spatial && s.channels == 2;

                double pos = v.position;
                uint32_t i = 0;
                for (; i < frames; i++) {
                    size_t i0 = (size_t)pos;
                    if (i0 >= total) {
                        if (!v.loop) break;
                        pos -= (double)total;
                        i0 = (size_t)pos;
                    }
                    size_t i1 = i0 + 1;
                    if (i1 >= total) {
                        i1 = v.loop ? 0 : i0;
                    }
                    float t = (float)(pos - (double)i0);
                    float l, r;
                    if (mono) {
                        l = r = s.samples[i0] + (s.samples[i1] - s.samples[i0]) * t;
                    } else {
                        l = s.samples[i0 * 2] + (s.samples[i1 * 2] - s.samples[i0 * 2]) * t;
                        r = s.samples[i0 * 2 + 1] +
                            (s.samples[i1 * 2 + 1] - s.samples[i0 * 2 + 1]) * t;
                        if (downmix) l = r = 0.5f * (l + r);
                    }
                    curL += dl;
                    curR += dr;
                    out[i * 2] += l * curL;
                    out[i * 2 + 1] += r * curR;
                    pos += step;
                }
                v.position = pos;
                finished = (i < frames) || (!v.loop && pos >= (double)total);
            }

            v.lastGainL = gl;
            v.lastGainR = gr;
            if (finished) {
                v.id = INVALID_VOICE; // reaped below
            }
        }

        for (auto it = voices_.begin(); it != voices_.end();) {
            if (it->id == INVALID_VOICE) {
                dead.push_back(std::move(*it));
                it = voices_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (uint32_t i = 0; i < frames * 2; i++) {
        out[i] = clampf(out[i], -1.0f, 1.0f);
    }
}

} // namespace ion
