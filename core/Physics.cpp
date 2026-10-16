#include <ion/physics/PhysicsWorld.hpp>

#include <algorithm>
#include <cmath>

namespace ion {

namespace {

constexpr float kSlop = 0.005f;            // penetration tolerated before correcting
constexpr float kCorrectionPercent = 0.8f; // fraction of penetration fixed per substep
constexpr float kRestitutionThreshold = 1.0f; // below this closing speed, no bounce

uint64_t pairKey(uint64_t a, uint64_t b) {
    if (a > b) std::swap(a, b);
    uint64_t h = a * 0x9E3779B97F4A7C15ull;
    h ^= (b + 0x7F4A7C15ull + (h << 6) + (h >> 2)) * 0xC2B2AE3D27D4EB4Full;
    return h;
}

} // namespace

PhysicsWorld::Body* PhysicsWorld::find(BodyId id) {
    uint32_t index = (uint32_t)(id & 0xFFFFFFFFull);
    if (id == INVALID_BODY || index >= bodies_.size()) return nullptr;
    Body& b = bodies_[index];
    return (b.alive && b.generation == (uint32_t)(id >> 32)) ? &b : nullptr;
}

const PhysicsWorld::Body* PhysicsWorld::find(BodyId id) const {
    return const_cast<PhysicsWorld*>(this)->find(id);
}

Aabb PhysicsWorld::bounds(const Body& b) {
    if (b.desc.shape == ShapeType::Sphere) {
        return Aabb::fromCenter(b.desc.position, {b.desc.radius, b.desc.radius, b.desc.radius});
    }
    return Aabb::fromCenter(b.desc.position, b.desc.halfExtents);
}

BodyId PhysicsWorld::createBody(const BodyDesc& desc) {
    uint32_t index;
    if (!free_.empty()) {
        index = free_.back();
        free_.pop_back();
    } else {
        index = (uint32_t)bodies_.size();
        bodies_.emplace_back();
    }
    Body& b = bodies_[index];
    b.desc = desc;
    b.desc.mass = std::max(desc.mass, 1e-4f);
    b.velocity = desc.type == BodyType::Static ? Vector3() : desc.velocity;
    b.force = {};
    b.invMass = desc.type == BodyType::Dynamic ? 1.0f / b.desc.mass : 0.0f;
    b.alive = true;
    ++alive_;
    return makeId(index, b.generation);
}

void PhysicsWorld::destroyBody(BodyId id) {
    Body* b = find(id);
    if (!b) return;
    b->alive = false;
    if (++b->generation == 0) b->generation = 1;
    free_.push_back((uint32_t)(id & 0xFFFFFFFFull));
    --alive_;
}

bool PhysicsWorld::valid(BodyId id) const { return find(id) != nullptr; }

Vector3 PhysicsWorld::position(BodyId id) const {
    const Body* b = find(id);
    return b ? b->desc.position : Vector3();
}
Vector3 PhysicsWorld::velocity(BodyId id) const {
    const Body* b = find(id);
    return b ? b->velocity : Vector3();
}
uint64_t PhysicsWorld::userData(BodyId id) const {
    const Body* b = find(id);
    return b ? b->desc.userData : 0;
}
void PhysicsWorld::setPosition(BodyId id, const Vector3& p) {
    if (Body* b = find(id)) b->desc.position = p;
}
void PhysicsWorld::setVelocity(BodyId id, const Vector3& v) {
    Body* b = find(id);
    if (b && b->desc.type != BodyType::Static) b->velocity = v;
}
void PhysicsWorld::applyImpulse(BodyId id, const Vector3& impulse) {
    Body* b = find(id);
    if (b && b->invMass > 0.0f) b->velocity += impulse * b->invMass;
}
void PhysicsWorld::applyForce(BodyId id, const Vector3& force) {
    Body* b = find(id);
    if (b && b->invMass > 0.0f) b->force += force;
}

void PhysicsWorld::step(float deltaTime) {
    if (deltaTime <= 0.0f || fixedTimeStep <= 0.0f) return;
    accumulator_ += deltaTime;
    int steps = 0;
    while (accumulator_ >= fixedTimeStep && steps < maxSteps) {
        // Fast bodies are advanced in smaller slices so they can't skip
        // through thin geometry between collision checks.
        float maxSpeed = 0.0f, minSize = 1e9f;
        for (const Body& b : bodies_) {
            if (!b.alive) continue;
            if (b.desc.type != BodyType::Static) maxSpeed = std::max(maxSpeed, b.velocity.length());
            float size = b.desc.shape == ShapeType::Sphere
                             ? b.desc.radius
                             : std::min({b.desc.halfExtents.x, b.desc.halfExtents.y,
                                         b.desc.halfExtents.z});
            minSize = std::min(minSize, size);
        }
        int slices = 1;
        if (minSize > 0.0f && minSize < 1e8f) {
            slices = std::clamp((int)std::ceil(maxSpeed * fixedTimeStep / minSize), 1, 32);
        }
        for (int i = 0; i < slices; i++) substep(fixedTimeStep / (float)slices);
        accumulator_ -= fixedTimeStep;
        ++steps;
    }
    if (steps == maxSteps) accumulator_ = 0.0f; // spiral-of-death guard
    for (Body& b : bodies_) b.force = {};
}

void PhysicsWorld::substep(float h) {
    // Integrate.
    for (Body& b : bodies_) {
        if (!b.alive || b.desc.type == BodyType::Static) continue;
        if (b.desc.type == BodyType::Dynamic) {
            if (b.desc.useGravity) b.velocity += gravity * h;
            b.velocity += b.force * (b.invMass * h);
            b.velocity *= std::pow(std::max(0.0f, 1.0f - b.desc.linearDamping), h);
        }
        b.desc.position += b.velocity * h;
    }

    // Detect contacts.
    struct Pair {
        Body* a;
        Body* b;
        BodyId idA, idB;
        Contact contact;
        float closing; // closing speed along the normal when first detected
        float jn = 0.0f;
    };
    std::vector<Pair> pairs;
    const size_t n = bodies_.size();
    for (size_t i = 0; i < n; i++) {
        Body& a = bodies_[i];
        if (!a.alive) continue;
        Aabb boundsA = bounds(a);
        for (size_t j = i + 1; j < n; j++) {
            Body& b = bodies_[j];
            if (!b.alive || (a.invMass == 0.0f && b.invMass == 0.0f)) continue;
            if (!((a.desc.layer & b.desc.mask) && (b.desc.layer & a.desc.mask))) continue;
            if (!intersects(boundsA, bounds(b))) continue;

            Contact c;
            bool hit;
            if (a.desc.shape == ShapeType::Sphere && b.desc.shape == ShapeType::Sphere) {
                hit = collide(Sphere{a.desc.position, a.desc.radius},
                              Sphere{b.desc.position, b.desc.radius}, c);
            } else if (a.desc.shape == ShapeType::Sphere) {
                hit = collide(Sphere{a.desc.position, a.desc.radius}, bounds(b), c);
            } else if (b.desc.shape == ShapeType::Sphere) {
                hit = collide(Sphere{b.desc.position, b.desc.radius}, boundsA, c);
                c.normal = c.normal * -1.0f; // computed sphere->box; we need a->b
            } else {
                hit = collide(boundsA, bounds(b), c);
            }
            if (!hit) continue;

            Pair p{&a, &b, makeId((uint32_t)i, a.generation), makeId((uint32_t)j, b.generation), c,
                   -Vector3::dot(b.velocity - a.velocity, c.normal)};
            pairs.push_back(p);
        }
    }

    // Events for contacts that weren't touching on the previous substep.
    std::unordered_set<uint64_t> nowTouching;
    for (const Pair& p : pairs) {
        uint64_t key = pairKey(p.idA, p.idB);
        nowTouching.insert(key);
        if (!touching_.count(key)) {
            events_.push_back({p.idA, p.idB, p.contact.point, p.contact.normal,
                               std::max(p.closing, 0.0f)});
        }
    }
    touching_ = std::move(nowTouching);

    // Sequential impulses.
    for (int iter = 0; iter < solverIterations; iter++) {
        for (Pair& p : pairs) {
            Body& a = *p.a;
            Body& b = *p.b;
            const float invSum = a.invMass + b.invMass;
            const Vector3& nrm = p.contact.normal;

            Vector3 rel = b.velocity - a.velocity;
            float vn = Vector3::dot(rel, nrm);
            float e = std::min(a.desc.restitution, b.desc.restitution);
            float target = p.closing > kRestitutionThreshold ? e * p.closing : 0.0f;
            float dj = (target - vn) / invSum;
            float newJn = std::max(0.0f, p.jn + dj);
            dj = newJn - p.jn;
            p.jn = newJn;
            a.velocity -= nrm * (dj * a.invMass);
            b.velocity += nrm * (dj * b.invMass);

            // Coulomb friction, limited by the normal impulse.
            rel = b.velocity - a.velocity;
            Vector3 tangent = rel - nrm * Vector3::dot(rel, nrm);
            float tlen = tangent.length();
            if (tlen > 1e-6f) {
                tangent = tangent / tlen;
                float mu = std::sqrt(a.desc.friction * b.desc.friction);
                float jt = std::clamp(-tlen / invSum, -mu * p.jn, mu * p.jn);
                a.velocity -= tangent * (jt * a.invMass);
                b.velocity += tangent * (jt * b.invMass);
            }
        }
    }

    // Push overlapping bodies apart (positional correction).
    for (const Pair& p : pairs) {
        float invSum = p.a->invMass + p.b->invMass;
        float corr = std::max(p.contact.depth - kSlop, 0.0f) / invSum * kCorrectionPercent;
        p.a->desc.position -= p.contact.normal * (corr * p.a->invMass);
        p.b->desc.position += p.contact.normal * (corr * p.b->invMass);
    }
}

std::vector<CollisionEvent> PhysicsWorld::takeEvents() {
    std::vector<CollisionEvent> out;
    out.swap(events_);
    return out;
}

bool PhysicsWorld::raycast(const Ray& ray, float maxDistance, RaycastHit& out,
                           uint32_t layerMask) const {
    bool found = false;
    float best = maxDistance;
    for (size_t i = 0; i < bodies_.size(); i++) {
        const Body& b = bodies_[i];
        if (!b.alive || !(b.desc.layer & layerMask)) continue;
        RayHit h;
        bool hit = b.desc.shape == ShapeType::Sphere
                       ? ion::raycast(ray, Sphere{b.desc.position, b.desc.radius}, best, h)
                       : ion::raycast(ray, bounds(b), best, h);
        if (hit && h.distance <= best) {
            best = h.distance;
            found = true;
            out = {makeId((uint32_t)i, b.generation), h.distance, h.point, h.normal};
        }
    }
    return found;
}

std::vector<BodyId> PhysicsWorld::overlapSphere(const Vector3& center, float radius,
                                                uint32_t layerMask) const {
    std::vector<BodyId> result;
    Sphere s{center, radius};
    for (size_t i = 0; i < bodies_.size(); i++) {
        const Body& b = bodies_[i];
        if (!b.alive || !(b.desc.layer & layerMask)) continue;
        bool hit = b.desc.shape == ShapeType::Sphere
                       ? intersects(s, Sphere{b.desc.position, b.desc.radius})
                       : intersects(s, bounds(b));
        if (hit) result.push_back(makeId((uint32_t)i, b.generation));
    }
    return result;
}

} // namespace ion
