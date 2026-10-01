#pragma once

// Asteroids game logic built on ion::Registry. No rendering or platform
// dependencies, so it can be unit-tested headlessly (tests/asteroids_test.cpp).
// World is WORLD_W x WORLD_H with +y up and wrap-around edges.

#include <ion/ecs/Registry.hpp>
#include <ion/math/Vector2.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace asteroids {

constexpr float WORLD_W = 1280.0f;
constexpr float WORLD_H = 720.0f;
constexpr float PI = 3.14159265f;
constexpr int SHAPE_POINTS = 12;

struct Transform { ion::Vector2 pos; float angle = 0.0f; };
struct Velocity { ion::Vector2 v; float spin = 0.0f; };
struct Collider { float radius = 1.0f; };
struct Lifetime { float remaining = 1.0f; };
struct Bullet {};
struct Asteroid {
    int size = 3; // 3 large, 2 medium, 1 small
    float shape[SHAPE_POINTS] = {};
};
struct Player {
    float fireCooldown = 0.0f;
    float invulnerable = 0.0f;
    bool thrusting = false;
};

struct Input {
    float turn = 0.0f; // -1 (clockwise) .. +1 (counter-clockwise)
    bool thrust = false;
    bool fire = false;
    bool restart = false;
};

struct Explosion {
    ion::Vector2 pos;
    float radius;
    bool ship;
};

enum class State { Playing, GameOver };

inline float radiusForSize(int size) { return size == 3 ? 44.0f : size == 2 ? 26.0f : 14.0f; }
inline int scoreForSize(int size) { return size == 3 ? 20 : size == 2 ? 50 : 100; }

class Game {
public:
    explicit Game(unsigned seed = std::random_device{}()) : rng_(seed) { reset(); }

    void reset() {
        reg_.clear();
        score_ = 0;
        lives_ = 3;
        wave_ = 0;
        state_ = State::Playing;
        spawnPlayer_();
        spawnWave_();
    }

    void update(float dt, const Input& in) {
        if (state_ == State::GameOver) {
            if (in.restart) {
                reset();
            }
            moveAll_(dt);
            return;
        }

        controlPlayer_(dt, in);
        moveAll_(dt);
        expireLifetimes_(dt);
        resolveCollisions_();

        if (state_ == State::Playing && countAsteroids() == 0) {
            spawnWave_();
        }
    }

    ion::Registry& registry() { return reg_; }
    const ion::Registry& registry() const { return reg_; }
    ion::EntityId player() const { return player_; }
    int score() const { return score_; }
    int lives() const { return lives_; }
    int wave() const { return wave_; }
    State state() const { return state_; }
    int highScore() const { return std::max(highScore_, score_); }
    // Total bullets fired since construction (frontends diff this for sound).
    int shotsFired() const { return shotsFired_; }

    // Explosions since the last call (for particles/sound in the frontend).
    std::vector<Explosion> takeExplosions() {
        std::vector<Explosion> out;
        out.swap(explosions_);
        return out;
    }

    static ion::Vector2 forward(float angle) { return {std::cos(angle), std::sin(angle)}; }

    int countAsteroids() {
        int n = 0;
        reg_.view<Asteroid>().each([&](ion::EntityId, Asteroid&) { ++n; });
        return n;
    }

private:
    float rand_(float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng_);
    }

    void spawnPlayer_() {
        player_ = reg_.create();
        reg_.emplace<Transform>(player_, ion::Vector2(WORLD_W * 0.5f, WORLD_H * 0.5f), PI * 0.5f);
        reg_.emplace<Velocity>(player_);
        reg_.emplace<Collider>(player_, 12.0f);
        reg_.emplace<Player>(player_, 0.0f, 2.0f, false);
    }

    ion::EntityId spawnAsteroid_(int size, ion::Vector2 pos, ion::Vector2 vel) {
        ion::EntityId e = reg_.create();
        float radius = radiusForSize(size);
        reg_.emplace<Transform>(e, pos, rand_(0.0f, 2.0f * PI));
        reg_.emplace<Velocity>(e, vel, rand_(-1.2f, 1.2f));
        reg_.emplace<Collider>(e, radius * 0.9f);
        Asteroid& a = reg_.emplace<Asteroid>(e);
        a.size = size;
        for (float& s : a.shape) {
            s = rand_(0.75f, 1.15f);
        }
        return e;
    }

    void spawnWave_() {
        ++wave_;
        ion::Vector2 center = {WORLD_W * 0.5f, WORLD_H * 0.5f};
        if (auto* t = reg_.tryGet<Transform>(player_)) {
            center = t->pos;
        }
        int count = 3 + wave_;
        for (int i = 0; i < count; ++i) {
            ion::Vector2 pos;
            do {
                pos = {rand_(0.0f, WORLD_W), rand_(0.0f, WORLD_H)};
            } while ((pos - center).length() < 220.0f);
            float speed = rand_(30.0f, 60.0f) + 6.0f * wave_;
            float dir = rand_(0.0f, 2.0f * PI);
            spawnAsteroid_(3, pos, forward(dir) * speed);
        }
    }

    void controlPlayer_(float dt, const Input& in) {
        auto* p = reg_.tryGet<Player>(player_);
        if (!p) {
            return;
        }
        auto& t = reg_.get<Transform>(player_);
        auto& v = reg_.get<Velocity>(player_);

        t.angle += in.turn * 4.2f * dt;
        p->thrusting = in.thrust;
        if (in.thrust) {
            v.v += forward(t.angle) * 300.0f * dt;
        }
        v.v *= std::pow(0.55f, dt); // drag
        float speed = v.v.length();
        if (speed > 420.0f) {
            v.v *= 420.0f / speed;
        }

        p->invulnerable = std::max(0.0f, p->invulnerable - dt);
        p->fireCooldown = std::max(0.0f, p->fireCooldown - dt);
        if (in.fire && p->fireCooldown <= 0.0f) {
            p->fireCooldown = 0.2f;
            ion::EntityId b = reg_.create();
            ion::Vector2 dir = forward(t.angle);
            reg_.emplace<Transform>(b, t.pos + dir * 18.0f, t.angle);
            reg_.emplace<Velocity>(b, dir * 640.0f + v.v * 0.3f, 0.0f);
            reg_.emplace<Collider>(b, 3.0f);
            reg_.emplace<Lifetime>(b, 1.0f);
            reg_.emplace<Bullet>(b);
            ++shotsFired_;
        }
    }

    void moveAll_(float dt) {
        reg_.view<Transform, Velocity>().each([&](ion::EntityId, Transform& t, Velocity& v) {
            t.pos += v.v * dt;
            t.angle += v.spin * dt;
            if (t.pos.x < 0.0f) t.pos.x += WORLD_W;
            if (t.pos.x >= WORLD_W) t.pos.x -= WORLD_W;
            if (t.pos.y < 0.0f) t.pos.y += WORLD_H;
            if (t.pos.y >= WORLD_H) t.pos.y -= WORLD_H;
        });
    }

    void expireLifetimes_(float dt) {
        reg_.view<Lifetime>().each([&](ion::EntityId id, Lifetime& l) {
            l.remaining -= dt;
            if (l.remaining <= 0.0f) {
                reg_.destroy(id);
            }
        });
    }

    struct Body { ion::EntityId id; ion::Vector2 pos; float radius; };

    static bool overlaps_(const Body& a, const Body& b) {
        float r = a.radius + b.radius;
        return (a.pos - b.pos).lengthSquared() < r * r;
    }

    void resolveCollisions_() {
        std::vector<Body> bullets, rocks;
        reg_.view<Bullet, Transform, Collider>().each(
            [&](ion::EntityId id, Bullet&, Transform& t, Collider& c) {
                bullets.push_back({id, t.pos, c.radius});
            });
        reg_.view<Asteroid, Transform, Collider>().each(
            [&](ion::EntityId id, Asteroid&, Transform& t, Collider& c) {
                rocks.push_back({id, t.pos, c.radius});
            });

        for (const Body& b : bullets) {
            for (Body& r : rocks) {
                if (r.id == ion::INVALID_ENTITY || !overlaps_(b, r)) {
                    continue;
                }
                reg_.destroy(b.id);
                breakAsteroid_(r);
                r.id = ion::INVALID_ENTITY; // consumed this frame
                break;
            }
        }

        auto* p = reg_.tryGet<Player>(player_);
        if (!p || p->invulnerable > 0.0f) {
            return;
        }
        Body ship{player_, reg_.get<Transform>(player_).pos, reg_.get<Collider>(player_).radius};
        for (const Body& r : rocks) {
            if (r.id != ion::INVALID_ENTITY && overlaps_(ship, r)) {
                killPlayer_(ship.pos);
                return;
            }
        }
    }

    void breakAsteroid_(const Body& rock) {
        int size = reg_.get<Asteroid>(rock.id).size;
        ion::Vector2 vel = reg_.get<Velocity>(rock.id).v;
        score_ += scoreForSize(size);
        highScore_ = std::max(highScore_, score_);
        explosions_.push_back({rock.pos, radiusForSize(size), false});
        reg_.destroy(rock.id);
        if (size > 1) {
            for (int i = 0; i < 2; ++i) {
                float dir = rand_(0.0f, 2.0f * PI);
                float speed = rand_(50.0f, 90.0f) + 25.0f * (3 - size);
                spawnAsteroid_(size - 1, rock.pos, vel * 0.4f + forward(dir) * speed);
            }
        }
    }

    void killPlayer_(ion::Vector2 pos) {
        explosions_.push_back({pos, 40.0f, true});
        --lives_;
        reg_.destroy(player_);
        player_ = ion::INVALID_ENTITY;
        if (lives_ <= 0) {
            state_ = State::GameOver;
            return;
        }
        spawnPlayer_();
    }

    ion::Registry reg_;
    ion::EntityId player_ = ion::INVALID_ENTITY;
    std::mt19937 rng_;
    std::vector<Explosion> explosions_;
    int score_ = 0;
    int highScore_ = 0;
    int shotsFired_ = 0;
    int lives_ = 3;
    int wave_ = 0;
    State state_ = State::Playing;
};

} // namespace asteroids
