#pragma once

#include <ion/math/Geometry.hpp>
#include <ion/math/Vector2.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace ion {

//
// Simple 2D rigid-body physics for arcade games: axis-aligned boxes and
// circles (bodies don't rotate), gravity, impulse-based collision response
// with restitution and friction, trigger volumes and collision layers.
//
//   PhysicsWorld2D world;
//   world.setGravity({0, -980});
//   BodyId ground = world.createBody(BodyDef::box({0, -10}, {500, 10}, BodyType::Static));
//   BodyId ball = world.createBody(BodyDef::circle({0, 200}, 16));
//   world.step(dt);
//   Vector2 p = world.body(ball)->position;
//
// Units are up to you (pixels or meters); gravity must match.
//
enum class BodyType : uint8_t {
    Static,    // never moves (walls, ground)
    Dynamic,   // moved by gravity, velocity and collisions
    Kinematic, // moved only by its velocity; pushes dynamic bodies
};

enum class ShapeType : uint8_t { Box, Circle };

using BodyId = uint32_t;
constexpr BodyId INVALID_BODY = 0;

struct BodyDef {
    BodyType type = BodyType::Dynamic;
    ShapeType shape = ShapeType::Box;
    Vector2 position;            // center
    Vector2 halfExtents{0.5f, 0.5f}; // boxes
    float radius = 0.5f;         // circles
    Vector2 velocity;
    float mass = 1.0f;           // dynamic bodies; <= 0 is treated as 1
    float restitution = 0.0f;    // bounciness 0..1
    float friction = 0.3f;       // Coulomb coefficient
    float linearDamping = 0.0f;  // velocity loss per second (air drag)
    float gravityScale = 1.0f;
    bool isTrigger = false;      // reports overlaps but doesn't collide
    uint32_t layer = 1;          // which layer this body is on (bit mask)
    uint32_t mask = 0xFFFFFFFFu; // which layers it collides with
    void* userData = nullptr;

    static BodyDef box(const Vector2& center, const Vector2& halfExtents,
                       BodyType type = BodyType::Dynamic) {
        BodyDef def;
        def.type = type;
        def.shape = ShapeType::Box;
        def.position = center;
        def.halfExtents = halfExtents;
        return def;
    }

    static BodyDef circle(const Vector2& center, float radius,
                          BodyType type = BodyType::Dynamic) {
        BodyDef def;
        def.type = type;
        def.shape = ShapeType::Circle;
        def.position = center;
        def.radius = radius;
        return def;
    }
};

struct Body : BodyDef {
    BodyId id = INVALID_BODY;
    Vector2 force;        // accumulated until the next step
    bool grounded = false; // touched something below during the last step

    float inverseMass() const {
        return type == BodyType::Dynamic ? 1.0f / (mass > 0.0f ? mass : 1.0f) : 0.0f;
    }
    Rect bounds() const;
};

struct Contact2D {
    BodyId a = INVALID_BODY;
    BodyId b = INVALID_BODY;
    Vector2 normal; // from a toward b
    float depth = 0.0f;
    bool trigger = false;
};

struct PhysicsRayHit {
    BodyId body = INVALID_BODY;
    float distance = 0.0f;
    Vector2 point;
    Vector2 normal;
};

class PhysicsWorld2D {
public:
    BodyId createBody(const BodyDef& def);
    void destroyBody(BodyId id);
    // Pointer is valid until the next createBody/destroyBody.
    Body* body(BodyId id);
    const Body* body(BodyId id) const;
    size_t bodyCount() const { return bodies_.size(); }

    void setGravity(const Vector2& gravity) { gravity_ = gravity; }
    const Vector2& gravity() const { return gravity_; }

    // Velocity iterations per substep (default 8) and substeps per step
    // (default 2). More = stiffer stacking, more CPU.
    void setIterations(int velocityIterations) { iterations_ = velocityIterations < 1 ? 1 : velocityIterations; }
    void setSubsteps(int substeps) { substeps_ = substeps < 1 ? 1 : substeps; }

    void applyForce(BodyId id, const Vector2& force);
    void applyImpulse(BodyId id, const Vector2& impulse);
    void setVelocity(BodyId id, const Vector2& velocity);
    void setPosition(BodyId id, const Vector2& position);

    // Advances the simulation. Call with a fixed timestep (see
    // Engine::setFixedUpdateCallback) for stable, deterministic results.
    void step(float deltaTime);

    // Contacts (including trigger overlaps) found in the last step.
    const std::vector<Contact2D>& contacts() const { return contacts_; }

    // Called for each contact during step(), after resolution.
    void setContactCallback(std::function<void(const Contact2D&)> callback) {
        onContact_ = std::move(callback);
    }

    // Closest body hit by the ray (triggers ignored), within maxDistance.
    bool raycast(const Vector2& origin, const Vector2& direction, float maxDistance,
                 PhysicsRayHit& hit, uint32_t mask = 0xFFFFFFFFu) const;

    // Bodies whose bounds overlap the rect.
    std::vector<BodyId> queryRect(const Rect& rect, uint32_t mask = 0xFFFFFFFFu) const;
    // Bodies containing the point.
    std::vector<BodyId> queryPoint(const Vector2& point, uint32_t mask = 0xFFFFFFFFu) const;

    void clear();

private:
    Body* find_(BodyId id);
    bool collide_(const Body& a, const Body& b, Contact2D& contact) const;
    void solve_(float dt);

    std::vector<Body> bodies_;
    std::vector<Contact2D> contacts_;
    std::function<void(const Contact2D&)> onContact_;
    Vector2 gravity_{0.0f, -9.81f};
    BodyId nextId_ = 1;
    int iterations_ = 8;
    int substeps_ = 2;
};

} // namespace ion
