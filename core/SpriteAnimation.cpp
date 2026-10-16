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
    mode_ = AnimationMode::Loop;
}

void SpriteAnimation::play(bool loop) {
    play(loop ? AnimationMode::Loop : AnimationMode::Once);
}

void SpriteAnimation::play(AnimationMode mode) {
    mode_ = mode;
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

void SpriteAnimation::setSpeed(float speed) {
    speed_ = std::max(0.0f, speed);
}

void SpriteAnimation::setFrameIndex(int index) {
    if (frames_.empty()) {
        return;
    }
    index = std::clamp(index, 0, (int)frames_.size() - 1);
    elapsed_ = (float)index * frameDuration_;
    updateFrame_();
}

void SpriteAnimation::setFrameCallback(std::function<void(int frame)> callback) {
    onFrame_ = std::move(callback);
}

void SpriteAnimation::update(float deltaTime) {
    if (!playing_ || frames_.empty()) {
        return;
    }
    elapsed_ += deltaTime * speed_;
    float total = frameDuration_ * (float)frames_.size();
    switch (mode_) {
    case AnimationMode::Loop:
        elapsed_ = std::fmod(elapsed_, total);
        break;
    case AnimationMode::Once:
        if (elapsed_ >= total) {
            elapsed_ = total;
            playing_ = false;
        }
        break;
    case AnimationMode::PingPong: {
        // One cycle visits 0..n-1 then n-2..1.
        float cycle = frameDuration_ * (float)std::max<size_t>(1, 2 * frames_.size() - 2);
        elapsed_ = std::fmod(elapsed_, cycle);
        break;
    }
    }
    updateFrame_();
}

void SpriteAnimation::updateFrame_() {
    int count = (int)frames_.size();
    int step = (int)(elapsed_ / frameDuration_);
    int frame;
    if (mode_ == AnimationMode::PingPong && count > 1) {
        int period = 2 * count - 2;
        step %= period;
        frame = step < count ? step : period - step;
    } else {
        frame = std::min(step, count - 1);
    }
    frame = std::max(0, frame);
    if (frame != frame_) {
        frame_ = frame;
        if (onFrame_) {
            onFrame_(frame_);
        }
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
    return mode_ == AnimationMode::Once && !playing_ && !frames_.empty() &&
           elapsed_ >= frameDuration_ * (float)frames_.size();
}

float SpriteAnimation::duration() const {
    return frameDuration_ * (float)frames_.size();
}

int SpriteAnimation::frameCount() const {
    return (int)frames_.size();
}

float SpriteAnimation::progress() const {
    float total = duration();
    return total > 0.0f ? std::min(1.0f, elapsed_ / total) : 0.0f;
}

} // namespace ion
