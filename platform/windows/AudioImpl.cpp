#include <ion/audio/Audio.hpp>
#include <ion/core/Log.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <thread>
#include <vector>

//
// waveOut output (winmm, available on every Windows version). A worker
// thread keeps a small ring of 16-bit buffers queued, refilling each one
// from the mixer as the device finishes playing it.
//

namespace ion {

namespace {

constexpr int kBufferCount = 4;
constexpr uint32_t kBufferFrames = 1024; // ~21 ms at 48 kHz per buffer

} // namespace

class AudioDevice::Impl {
public:
    HWAVEOUT device = nullptr;
    HANDLE event = nullptr;
    WAVEHDR headers[kBufferCount] = {};
    std::vector<int16_t> buffers[kBufferCount];
    std::vector<float> mix;
    AudioMixer* mixer = nullptr;
    std::thread thread;
    std::atomic<bool> running{false};

    void fill(int index) {
        mixer->render(mix.data(), kBufferFrames);
        std::vector<int16_t>& out = buffers[index];
        for (size_t i = 0; i < mix.size(); i++) {
            out[i] = (int16_t)(mix[i] * 32767.0f); // mixer output is clamped
        }
        headers[index].dwFlags &= ~WHDR_DONE;
        waveOutWrite(device, &headers[index], sizeof(WAVEHDR));
    }

    void run() {
        while (running.load(std::memory_order_acquire)) {
            WaitForSingleObject(event, 50);
            for (int i = 0; i < kBufferCount && running.load(std::memory_order_acquire); i++) {
                if (headers[i].dwFlags & WHDR_DONE) {
                    fill(i);
                }
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

    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = mixer->sampleRate();
    format.wBitsPerSample = 16;
    format.nBlockAlign = (WORD)(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    impl_->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!impl_->event) {
        return false;
    }
    MMRESULT result = waveOutOpen(&impl_->device, WAVE_MAPPER, &format,
                                  (DWORD_PTR)impl_->event, 0, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
        ION_LOG_WARN("audio: waveOutOpen failed (%u)", (unsigned)result);
        CloseHandle(impl_->event);
        impl_->event = nullptr;
        impl_->device = nullptr;
        return false;
    }

    impl_->mixer = mixer;
    impl_->mix.assign((size_t)kBufferFrames * 2, 0.0f);
    for (int i = 0; i < kBufferCount; i++) {
        impl_->buffers[i].assign((size_t)kBufferFrames * 2, 0);
        WAVEHDR& header = impl_->headers[i];
        header = {};
        header.lpData = reinterpret_cast<LPSTR>(impl_->buffers[i].data());
        header.dwBufferLength = kBufferFrames * format.nBlockAlign;
        waveOutPrepareHeader(impl_->device, &header, sizeof(WAVEHDR));
        header.dwFlags |= WHDR_DONE; // mark free so run() fills it
    }
    impl_->running.store(true, std::memory_order_release);
    for (int i = 0; i < kBufferCount; i++) {
        impl_->fill(i); // prime the queue
    }
    impl_->thread = std::thread([this] { impl_->run(); });
    ION_LOG_INFO("audio: waveOut output (%u Hz)", mixer->sampleRate());
    return true;
}

void AudioDevice::stop() {
    if (!impl_) {
        return;
    }
    impl_->running.store(false, std::memory_order_release);
    if (impl_->event) {
        SetEvent(impl_->event);
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    if (impl_->device) {
        waveOutReset(impl_->device); // returns queued buffers as done
        for (int i = 0; i < kBufferCount; i++) {
            waveOutUnprepareHeader(impl_->device, &impl_->headers[i], sizeof(WAVEHDR));
        }
        waveOutClose(impl_->device);
        impl_->device = nullptr;
    }
    if (impl_->event) {
        CloseHandle(impl_->event);
        impl_->event = nullptr;
    }
    impl_->mixer = nullptr;
}

bool AudioDevice::isRunning() const {
    return impl_ && impl_->running.load(std::memory_order_acquire);
}

} // namespace ion
