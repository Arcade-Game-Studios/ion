#pragma once

#include <ion/math/Vector2.hpp>
#include <ion/render/Color.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/SpriteRegion.hpp>

#include <cstdint>
#include <random>
#include <vector>

namespace ion {

struct Particle {
    Vector2 position;
    Vector2 velocity;
    float rotation = 0.0f;
    float rotationSpeed = 0.0f;
    float life = 0.0f;
    float maxLife = 1.0f;
    float startSize = 1.0f;
    float endSize = 1.0f;
    Color startColor = Color::white();
    Color endColor = Color::transparent();
    bool alive = false;
};

struct ParticleEmitterConfig {
    // Texture region drawn for each particle (may be invalid for tint-only
    // particles; the batch uses its white texture in that case).
    SpriteRegion region;
    uint32_t capacity = 256;
    // Continuous emission rate (particles per second). Use 0 for burst-only.
    float rate = 0.0f;
    float lifetime = 1.0f;
    float lifetimeSpread = 0.3f;
    // Initial velocity direction/strength. angle is in radians, 0 = +x,
    // pi/2 = +y. Particles spread uniformly within angle +/- angleSpread.
    float speed = 40.0f;
    float speedSpread = 15.0f;
    float angle = -1.5707963f;
    float angleSpread = 0.6f;
    // Constant acceleration applied to particle velocity each second.
    Vector2 gravity;
    float startSize = 6.0f;
    float endSize = 2.0f;
    float sizeSpread = 0.0f;
    Color startColor = Color::white();
    Color endColor = Color::transparent();
    float rotationSpeed = 0.0f;
    float rotationSpeedSpread = 0.0f;
};

//
// A 2D particle emitter with a fixed-capacity pool. Supports both
// continuous emission (rate) and instantaneous bursts. Particles are drawn
// as tinted quads and are alpha-blended; draw() must be called between
// SpriteBatch::begin() and SpriteBatch::end().
//
class ParticleEmitter {
public:
    ParticleEmitter() = default;

    void setConfig(const ParticleEmitterConfig& config);
    const ParticleEmitterConfig& config() const;

    void setPosition(const Vector2& position);
    const Vector2& position() const;

    // Spawns count particles immediately.
    void burst(uint32_t count);

    // Resumes/stops continuous emission.
    void start();
    void stop();
    bool isActive() const;

    void clear();

    void update(float deltaTime);
    void draw(SpriteBatch& batch);

    uint32_t aliveCount() const;

private:
    void spawn_();
    float random_() {
        return dist_(rng_);
    }

    ParticleEmitterConfig config_;
    std::vector<Particle> particles_;
    Vector2 position_;
    float accumulator_ = 0.0f;
    uint32_t nextSlot_ = 0;
    bool active_ = false;
    std::mt19937 rng_{std::random_device{}()};
    std::uniform_real_distribution<float> dist_{-1.0f, 1.0f};
};

} // namespace ion
