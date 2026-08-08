#include <ion/render/ParticleSystem.hpp>

#include <algorithm>
#include <cmath>

namespace ion {

namespace {

inline Color lerpColor(const Color& a, const Color& b, float t) {
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                 a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

inline float lerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
}

} // namespace

void ParticleEmitter::setConfig(const ParticleEmitterConfig& config) {
    config_ = config;
    if (config_.capacity == 0) {
        config_.capacity = 1;
    }
    particles_.resize(config_.capacity);
    for (auto& particle : particles_) {
        particle.alive = false;
    }
    nextSlot_ = 0;
    accumulator_ = 0.0f;
}

const ParticleEmitterConfig& ParticleEmitter::config() const {
    return config_;
}

void ParticleEmitter::setPosition(const Vector2& position) {
    position_ = position;
}

const Vector2& ParticleEmitter::position() const {
    return position_;
}

void ParticleEmitter::burst(uint32_t count) {
    if (particles_.empty()) {
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        spawn_();
    }
}

void ParticleEmitter::start() {
    active_ = true;
}

void ParticleEmitter::stop() {
    active_ = false;
    accumulator_ = 0.0f;
}

bool ParticleEmitter::isActive() const {
    return active_;
}

void ParticleEmitter::clear() {
    for (auto& particle : particles_) {
        particle.alive = false;
    }
    nextSlot_ = 0;
    accumulator_ = 0.0f;
}

void ParticleEmitter::update(float deltaTime) {
    if (particles_.empty()) {
        return;
    }

    if (active_ && config_.rate > 0.0f) {
        accumulator_ += config_.rate * deltaTime;
        uint32_t toSpawn = (uint32_t)accumulator_;
        accumulator_ -= (float)toSpawn;
        for (uint32_t i = 0; i < toSpawn; i++) {
            spawn_();
        }
    }

    for (auto& particle : particles_) {
        if (!particle.alive) {
            continue;
        }
        particle.velocity += config_.gravity * deltaTime;
        particle.position += particle.velocity * deltaTime;
        particle.rotation += particle.rotationSpeed * deltaTime;
        particle.life -= deltaTime;
        if (particle.life <= 0.0f) {
            particle.alive = false;
        }
    }
}

void ParticleEmitter::draw(SpriteBatch& batch) {
    if (particles_.empty()) {
        return;
    }
    for (const auto& particle : particles_) {
        if (!particle.alive) {
            continue;
        }
        float t = 1.0f - std::min(1.0f, particle.life / particle.maxLife);
        float size = lerpFloat(particle.startSize, particle.endSize, t);
        if (size <= 0.0f) {
            continue;
        }
        Color color = lerpColor(particle.startColor, particle.endColor, t);
        batch.drawSprite(config_.region, particle.position,
                         Vector2(size, size), particle.rotation,
                         Vector2(size * 0.5f, size * 0.5f), color);
    }
}

uint32_t ParticleEmitter::aliveCount() const {
    uint32_t count = 0;
    for (const auto& particle : particles_) {
        if (particle.alive) {
            count++;
        }
    }
    return count;
}

void ParticleEmitter::spawn_() {
    if (particles_.empty()) {
        return;
    }
    // Reuse a dead slot when possible; otherwise recycle the particle with
    // the least remaining life.
    Particle* target = nullptr;
    float oldestLife = 1.0e30f;
    for (auto& particle : particles_) {
        if (!particle.alive) {
            target = &particle;
            break;
        }
        if (particle.life < oldestLife) {
            oldestLife = particle.life;
            target = &particle;
        }
    }
    if (!target) {
        return;
    }

    float life = config_.lifetime +
                 config_.lifetimeSpread * random_();
    float angle = config_.angle + config_.angleSpread * random_();
    float speed = config_.speed + config_.speedSpread * random_();
    float size = config_.startSize + config_.sizeSpread * random_();

    target->alive = true;
    target->position = position_;
    target->velocity = Vector2(std::cos(angle) * speed,
                               std::sin(angle) * speed);
    target->rotation = random_() * 6.2831853f;
    target->rotationSpeed =
        config_.rotationSpeed + config_.rotationSpeedSpread * random_();
    target->life = life;
    target->maxLife = life;
    target->startSize = std::max(0.0f, size);
    target->endSize = config_.endSize;
    target->startColor = config_.startColor;
    target->endColor = config_.endColor;
}

} // namespace ion
