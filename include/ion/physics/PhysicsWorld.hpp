#pragma once

#include <ion/math/Vector3.hpp>
#include <ion/physics/Collision.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace ion {

//
// A small rigid-body world: gravity, sphere and axis-aligned-box bodies,
// impulse-based collision response with restitution and friction, ray casts
// and overlap queries.
//
//   ion::PhysicsWorld world;
//   world.createBody({.type = BodyType::Static, .shape = ShapeType::Box,
//                     .halfExtents = {50, 0.5f, 50}, .position = {0, -0.5f, 0}});
//   ion::BodyId ball = world.createBody({.radius = 0.5f, .position = {0, 5, 0}});
//   world.step(deltaTime);                 // call once per frame
//   Vector3 p = world.position(ball);
//
// Limits: bodies do not rotate (boxes stay axis-aligned, spheres do not roll
// under friction), pair detection is O(n^2) with a bounding-box early-out
// (fine for hundreds of bodies), and there is no sleeping.
//

enum class BodyType : uint8_t {
    Static,    // never moves
    Dynamic,   // moved by forces, gravity and collisions
    Kinematic, // moved only by its velocity/setPosition; pushes dynamic bodies
};

enum class ShapeType : uint8_t { Sphere, Box };

using BodyId = uint64_t;
constexpr BodyId INVALID_BODY = 0;

struct BodyDesc {
    BodyType type = BodyType::Dynamic;
    ShapeType shape = ShapeType::Sphere;
    float radius = 0.5f;                      // Sphere
    Vector3 halfExtents = {0.5f, 0.5f, 0.5f}; // Box
    Vector3 position;
    Vector3 velocity;
    float mass = 1.0f; // Dynamic only
    float restitution = 0.3f; // bounciness 0..1
    float friction = 0.4f;
    float linearDamping = 0.01f; // fraction of velocity lost per second
    bool useGravity = true;
    uint32_t layer = 1;     // bodies collide when (a.layer & b.mask) && (b.layer & a.mask)
    uint32_t mask = ~0u;
    uint64_t userData = 0;  // free for the game (e.g. an entity id)
};

// Reported once when two bodies start touching.
struct CollisionEvent {
    BodyId a = INVALID_BODY;
    BodyId b = INVALID_BODY;
    Vector3 point;
    Vector3 normal;          // from a toward b
    float impactSpeed = 0.0f; // closing speed along the normal at impact
};

struct RaycastHit {
    BodyId body = INVALID_BODY;
    float distance = 0.0f;
    Vector3 point;
    Vector3 normal;
};

class PhysicsWorld {
public:
    Vector3 gravity = {0.0f, -9.81f, 0.0f};
    // step() advances in fixed increments for stability and determinism.
    float fixedTimeStep = 1.0f / 120.0f;
    int maxSteps = 8; // per step() call; excess time is dropped, not carried
    int solverIterations = 6;

    BodyId createBody(const BodyDesc& desc);
    void destroyBody(BodyId id);
    bool valid(BodyId id) const;
    size_t bodyCount() const { return alive_; }

    Vector3 position(BodyId id) const;
    Vector3 velocity(BodyId id) const;
    uint64_t userData(BodyId id) const;
    void setPosition(BodyId id, const Vector3& p);
    void setVelocity(BodyId id, const Vector3& v);
    void applyImpulse(BodyId id, const Vector3& impulse); // instantaneous, Dynamic only
    void applyForce(BodyId id, const Vector3& force);     // for the next step only

    void step(float deltaTime);

    // Contacts that began since the last call.
    std::vector<CollisionEvent> takeEvents();

    // Nearest body along the ray (layerMask filters by body layer).
    bool raycast(const Ray& ray, float maxDistance, RaycastHit& out,
                 uint32_t layerMask = ~0u) const;
    // Bodies overlapping a sphere.
    std::vector<BodyId> overlapSphere(const Vector3& center, float radius,
                                      uint32_t layerMask = ~0u) const;

private:
    struct Body {
        BodyDesc desc;
        Vector3 velocity;
        Vector3 force;
        float invMass = 0.0f;
        uint32_t generation = 1;
        bool alive = false;
    };

    static BodyId makeId(uint32_t index, uint32_t generation) {
        return ((uint64_t)generation << 32) | index;
    }
    Body* find(BodyId id);
    const Body* find(BodyId id) const;
    static Aabb bounds(const Body& b);
    void substep(float h);

    std::vector<Body> bodies_;
    std::vector<uint32_t> free_;
    size_t alive_ = 0;
    float accumulator_ = 0.0f;
    std::unordered_set<uint64_t> touching_; // pair keys that were in contact last substep
    std::vector<CollisionEvent> events_;
};

} // namespace ion
