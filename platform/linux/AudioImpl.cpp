#include <ion/audio/Audio.hpp>
#include <ion/core/Log.hpp>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <thread>
#include <vector>

#include <dlfcn.h>

//
// ALSA output, loaded at runtime with dlopen so the engine has no build-time
// dependency on libasound and still runs (silently) where it's missing.
//
// The device defaults to "default"; set ION_AUDIO_DEVICE to override it
// (e.g. "null" for headless testing, "hw:1,0" for a specific card).
//

namespace ion {

namespace {

// Minimal slice of the ALSA C API (values from <alsa/pcm.h>).
struct snd_pcm_t;
using snd_pcm_sframes_t = long;
using snd_pcm_uframes_t = unsigned long;
constexpr int SND_PCM_STREAM_PLAYBACK = 0;
constexpr int SND_PCM_FORMAT_S16_LE = 2;
constexpr int SND_PCM_FORMAT_FLOAT_LE = 14;
constexpr int SND_PCM_ACCESS_RW_INTERLEAVED = 3;

struct AlsaApi {
    void* handle = nullptr;
    int (*open)(snd_pcm_t**, const char*, int, int) = nullptr;
    int (*setParams)(snd_pcm_t*, int, int, unsigned, unsigned, int, unsigned) = nullptr;
    snd_pcm_sframes_t (*writei)(snd_pcm_t*, const void*, snd_pcm_uframes_t) = nullptr;
    int (*recover)(snd_pcm_t*, int, int) = nullptr;
    int (*drop)(snd_pcm_t*) = nullptr;
    int (*close)(snd_pcm_t*) = nullptr;
    const char* (*strerror)(int) = nullptr;

    bool load() {
        if (handle) {
            return true;
        }
        handle = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            return false;
        }
        bool ok = sym(open, "snd_pcm_open") && sym(setParams, "snd_pcm_set_params") &&
                  sym(writei, "snd_pcm_writei") && sym(recover, "snd_pcm_recover") &&
                  sym(drop, "snd_pcm_drop") && sym(close, "snd_pcm_close") &&
                  sym(strerror, "snd_strerror");
        if (!ok) {
            dlclose(handle);
            handle = nullptr;
        }
        return ok;
    }

    template <typename Fn>
    bool sym(Fn& fn, const char* name) {
        fn = reinterpret_cast<Fn>(dlsym(handle, name));
        return fn != nullptr;
    }
};

AlsaApi& alsa() {
    static AlsaApi api;
    return api;
}

constexpr unsigned kLatencyMicros = 40000; // ~40 ms: low lag, few underruns
constexpr uint32_t kChunkFrames = 512;

} // namespace

class AudioDevice::Impl {
public:
    snd_pcm_t* pcm = nullptr;
    AudioMixer* mixer = nullptr;
    std::thread thread;
    std::atomic<bool> running{false};
    bool floatFormat = true;

    void run() {
        std::vector<float> mix(kChunkFrames * 2);
        std::vector<int16_t> pcm16(floatFormat ? 0 : kChunkFrames * 2);
        while (running.load(std::memory_order_acquire)) {
            mixer->render(mix.data(), kChunkFrames);
            const void* data = mix.data();
            if (!floatFormat) {
                for (size_t i = 0; i < mix.size(); i++) {
                    pcm16[i] = (int16_t)(mix[i] * 32767.0f); // mixer output is clamped
                }
                data = pcm16.data();
            }
            const uint8_t* cursor = static_cast<const uint8_t*>(data);
            size_t frameBytes = floatFormat ? sizeof(float) * 2 : sizeof(int16_t) * 2;
            snd_pcm_uframes_t remaining = kChunkFrames;
            while (remaining > 0 && running.load(std::memory_order_acquire)) {
                // Blocks until the device has room, which paces this loop.
                snd_pcm_sframes_t written = alsa().writei(pcm, cursor, remaining);
                if (written < 0) {
                    // Underrun (-EPIPE) or suspend: recover and retry.
                    if (alsa().recover(pcm, (int)written, 1) < 0) {
                        ION_LOG_ERROR("audio: ALSA write failed: %s",
                                      alsa().strerror((int)written));
                        running.store(false);
                        return;
                    }
                    continue;
                }
                remaining -= (snd_pcm_uframes_t)written;
                cursor += (size_t)written * frameBytes;
            }
        }
    }
};

AudioDevice::AudioDevice() : impl_(new Impl()) {}

AudioDevice::~AudioDevice() {
    stop();
    delete impl_;
}

bool AudioDevice::start(AudioMixer* mixer) {
    if (!impl_ || !mixer) {
        return false;
    }
    stop();
    if (!alsa().load()) {
        ION_LOG_WARN("audio: libasound.so.2 not available");
        return false;
    }
    const char* env = std::getenv("ION_AUDIO_DEVICE");
    const char* device = (env && *env) ? env : "default";
    int err = alsa().open(&impl_->pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        ION_LOG_WARN("audio: cannot open ALSA device '%s': %s", device, alsa().strerror(err));
        impl_->pcm = nullptr;
        return false;
    }
    // Prefer float (no conversion); fall back to 16-bit for devices without
    // a conversion plugin in front of them.
    impl_->floatFormat = true;
    err = alsa().setParams(impl_->pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                           2, mixer->sampleRate(), 1, kLatencyMicros);
    if (err < 0) {
        impl_->floatFormat = false;
        err = alsa().setParams(impl_->pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                               2, mixer->sampleRate(), 1, kLatencyMicros);
    }
    if (err < 0) {
        ION_LOG_WARN("audio: ALSA device '%s' rejected stereo %u Hz: %s", device,
                     mixer->sampleRate(), alsa().strerror(err));
        alsa().close(impl_->pcm);
        impl_->pcm = nullptr;
        return false;
    }
    impl_->mixer = mixer;
    impl_->running.store(true, std::memory_order_release);
    impl_->thread = std::thread([this] { impl_->run(); });
    ION_LOG_INFO("audio: ALSA output on '%s' (%u Hz, %s)", device, mixer->sampleRate(),
                 impl_->floatFormat ? "float" : "s16");
    return true;
}

void AudioDevice::stop() {
    if (!impl_) {
        return;
    }
    impl_->running.store(false, std::memory_order_release);
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    if (impl_->pcm) {
        alsa().drop(impl_->pcm);
        alsa().close(impl_->pcm);
        impl_->pcm = nullptr;
    }
    impl_->mixer = nullptr;
}

bool AudioDevice::isRunning() const {
    return impl_ && impl_->running.load(std::memory_order_acquire);
}

} // namespace ion
