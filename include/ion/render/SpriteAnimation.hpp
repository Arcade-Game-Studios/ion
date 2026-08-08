#pragma once

#include <ion/render/SpriteRegion.hpp>

#include <cstdint>
#include <vector>

namespace ion {

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
    void stop();
    void pause();
    void resume();

    // Advances the animation. Does nothing while paused or stopped.
    void update(float deltaTime);

    const SpriteRegion& currentFrame() const;
    int frameIndex() const;
    bool isPlaying() const;
    bool isFinished() const;
    float duration() const;
    int frameCount() const;

private:
    std::vector<SpriteRegion> frames_;
    float frameDuration_ = 0.1f;
    float elapsed_ = 0.0f;
    int frame_ = 0;
    bool playing_ = false;
    bool loop_ = true;
};

} // namespace ion
