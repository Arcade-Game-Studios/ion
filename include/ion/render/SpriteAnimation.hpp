#pragma once

#include <ion/render/SpriteRegion.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace ion {

enum class AnimationMode : uint8_t {
    Once,     // plays to the last frame and stops there
    Loop,     // wraps back to the first frame
    PingPong, // plays forward then backward, repeatedly
};

//
// A frame-based 2D sprite animation. Frames are SpriteRegions (usually
// pulled from a TextureAtlas sprite sheet). The animation advances by
// calling update(deltaTime) each frame.
//
class SpriteAnimation {
public:
    SpriteAnimation() = default;

    void setFrames(std::vector<SpriteRegion> frames, float frameDuration);
    void play(bool loop = true);
    void play(AnimationMode mode);
    void stop();
    void pause();
    void resume();

    // Advances the animation. Does nothing while paused or stopped.
    void update(float deltaTime);

    // Playback rate multiplier (2 = twice as fast). Negative values are
    // treated as 0.
    void setSpeed(float speed);
    float speed() const { return speed_; }

    // Jumps to a frame (clamped) without changing the play state.
    void setFrameIndex(int index);

    // Called whenever the displayed frame changes, with the new index (e.g.
    // to play a footstep sound on specific frames).
    void setFrameCallback(std::function<void(int frame)> callback);

    const SpriteRegion& currentFrame() const;
    int frameIndex() const;
    bool isPlaying() const;
    bool isFinished() const;
    float duration() const;
    int frameCount() const;
    AnimationMode mode() const { return mode_; }
    // Position within one pass of the animation, 0..1.
    float progress() const;

private:
    void updateFrame_();

    std::vector<SpriteRegion> frames_;
    std::function<void(int)> onFrame_;
    float frameDuration_ = 0.1f;
    float elapsed_ = 0.0f;
    float speed_ = 1.0f;
    int frame_ = 0;
    bool playing_ = false;
    AnimationMode mode_ = AnimationMode::Loop;
};

} // namespace ion
