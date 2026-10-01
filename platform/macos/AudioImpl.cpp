#include <ion/audio/Audio.hpp>
#include <ion/core/Log.hpp>

#include <AudioToolbox/AudioToolbox.h>

namespace ion {

class AudioDevice::Impl {
public:
    AudioUnit unit = nullptr;
    AudioMixer* mixer = nullptr;
    bool running = false;

    static OSStatus render(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                           UInt32, UInt32 frames, AudioBufferList* io) {
        auto* self = static_cast<Impl*>(ref);
        float* out = static_cast<float*>(io->mBuffers[0].mData);
        self->mixer->render(out, frames);
        return noErr;
    }
};

AudioDevice::AudioDevice() : impl_(new Impl) {}

AudioDevice::~AudioDevice() {
    stop();
    delete impl_;
}

bool AudioDevice::start(AudioMixer* mixer) {
    stop();
    if (!mixer) return false;

    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp || AudioComponentInstanceNew(comp, &impl_->unit) != noErr) {
        return false;
    }

    // Interleaved stereo float at the mixer's rate; CoreAudio converts to the
    // hardware format for us.
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = mixer->sampleRate();
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;
    fmt.mBytesPerFrame = 8;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerPacket = 8;

    AURenderCallbackStruct cb{&Impl::render, impl_};
    impl_->mixer = mixer;
    bool ok =
        AudioUnitSetProperty(impl_->unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &fmt, sizeof(fmt)) == noErr &&
        AudioUnitSetProperty(impl_->unit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &cb, sizeof(cb)) == noErr &&
        AudioUnitInitialize(impl_->unit) == noErr && AudioOutputUnitStart(impl_->unit) == noErr;
    if (!ok) {
        AudioComponentInstanceDispose(impl_->unit);
        impl_->unit = nullptr;
        impl_->mixer = nullptr;
        return false;
    }
    impl_->running = true;
    ION_LOG_INFO("audio: CoreAudio output started (%u Hz stereo)", mixer->sampleRate());
    return true;
}

void AudioDevice::stop() {
    if (!impl_->unit) return;
    AudioOutputUnitStop(impl_->unit);
    AudioUnitUninitialize(impl_->unit);
    AudioComponentInstanceDispose(impl_->unit);
    impl_->unit = nullptr;
    impl_->mixer = nullptr;
    impl_->running = false;
}

bool AudioDevice::isRunning() const { return impl_->running; }

} // namespace ion
