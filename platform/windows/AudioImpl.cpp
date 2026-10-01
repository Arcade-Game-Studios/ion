#include <ion/audio/Audio.hpp>

// No audio backend on this platform yet: start() fails and ion::Audio runs
// silent (the mixer still works and can be rendered manually).

namespace ion {

class AudioDevice::Impl {};

AudioDevice::AudioDevice() : impl_(nullptr) {}
AudioDevice::~AudioDevice() = default;
bool AudioDevice::start(AudioMixer*) { return false; }
void AudioDevice::stop() {}
bool AudioDevice::isRunning() const { return false; }

} // namespace ion
