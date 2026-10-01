#include <ion/audio/Audio.hpp>
#include <ion/core/Log.hpp>

namespace ion {

bool Audio::initialize() {
    shutdown();
    device_ = std::make_unique<AudioDevice>();
    if (!device_->start(&mixer_)) {
        ION_LOG_WARN("audio: no output device available; running silent");
        device_.reset();
        return false;
    }
    return true;
}

void Audio::shutdown() {
    if (device_) {
        device_->stop();
        device_.reset();
    }
    mixer_.stopAll();
}

} // namespace ion
