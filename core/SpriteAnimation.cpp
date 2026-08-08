#include <ion/render/SpriteAnimation.hpp>

#include <algorithm>
#include <cmath>

namespace ion {

void SpriteAnimation::setFrames(std::vector<SpriteRegion> frames,
                                float frameDuration) {
    frames_ = std::move(frames);
    frameDuration_ = std::max(0.0001f, frameDuration);
    elapsed_ = 0.0f;
    frame_ = 0;
    playing_ = false;
    loop_ = true;
}

void SpriteAnimation::play(bool loop) {
    loop_ = loop;
    elapsed_ = 0.0f;
    frame_ = 0;
    playing_ = !frames_.empty();
}

void SpriteAnimation::stop() {
    playing_ = false;
    elapsed_ = 0.0f;
    frame_ = 0;
}

void SpriteAnimation::pause() {
    playing_ = false;
}

void SpriteAnimation::resume() {
    if (!frames_.empty()) {
        playing_ = true;
    }
}

void SpriteAnimation::update(float deltaTime) {
    if (!playing_ || frames_.empty()) {
        return;
    }
    elapsed_ += deltaTime;
    float total = frameDuration_ * (float)frames_.size();
    if (loop_) {
        elapsed_ = std::fmod(elapsed_, total);
    } else if (elapsed_ >= total) {
        elapsed_ = total;
        playing_ = false;
    }
    frame_ = (int)(elapsed_ / frameDuration_);
    if (frame_ >= (int)frames_.size()) {
        frame_ = (int)frames_.size() - 1;
    }
}

const SpriteRegion& SpriteAnimation::currentFrame() const {
    static const SpriteRegion empty;
    if (frames_.empty()) {
        return empty;
    }
    int index = frame_;
    if (index < 0 || index >= (int)frames_.size()) {
        index = 0;
    }
    return frames_[index];
}

int SpriteAnimation::frameIndex() const {
    return frame_;
}

bool SpriteAnimation::isPlaying() const {
    return playing_;
}

bool SpriteAnimation::isFinished() const {
    return !playing_ && !frames_.empty() &&
           elapsed_ >= frameDuration_ * (float)frames_.size();
}

float SpriteAnimation::duration() const {
    return frameDuration_ * (float)frames_.size();
}

int SpriteAnimation::frameCount() const {
    return (int)frames_.size();
}

} // namespace ion
