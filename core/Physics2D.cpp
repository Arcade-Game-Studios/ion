#include <ion/physics/Physics2D.hpp>

#include <algorithm>
#include <cmath>

namespace ion {

namespace {

Circle circleOf(const Body& b) { return Circle(b.position, b.radius); }
Rect rectOf(const Body& b) { return Rect::fromCenter(b.position, b.halfExtents * 2.0f); }

bool layersInteract(const Body& a, const Body& b) {
    return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0;
}

} // namespace

Rect Body::bounds() const {
    if (shape == ShapeType::Circle) {
        return Rect::fromCenter(position, Vector2(radius * 2.0f, radius * 2.0f));
    }
    return Rect::fromCenter(position, halfExtents * 2.0f);
}

BodyId PhysicsWorld2D::createBody(const BodyDef& def) {
    Body body;
    static_cast<BodyDef&>(body) = def;
    body.id = nextId_++;
    if (body.type == BodyType::Static) {
        body.velocity = Vector2();
    }
    bodies_.push_back(body);
    return body.id;
}

void PhysicsWorld2D::destroyBody(BodyId id) {
    bodies_.erase(std::remove_if(bodies_.begin(), bodies_.end(),
                                 [&](const Body& b) { return b.id == id; }),
                  bodies_.end());
}

Body* PhysicsWorld2D::find_(BodyId id) {
    // Ids are handed out in increasing order and bodies keep their relative
    // order, so the vector is sorted by id.
    auto it = std::lower_bound(bodies_.begin(), bodies_.end(), id,
                               [](const Body& b, BodyId value) { return b.id < value; });
    return (it != bodies_.end() && it->id == id) ? &*it : nullptr;
}

Body* PhysicsWorld2D::body(BodyId id) {
    return find_(id);
}

const Body* PhysicsWorld2D::body(BodyId id) const {
    return const_cast<PhysicsWorld2D*>(this)->find_(id);
}

void PhysicsWorld2D::applyForce(BodyId id, const Vector2& force) {
    if (Body* b = find_(id)) {
        b->force += force;
    }
}

void PhysicsWorld2D::applyImpulse(BodyId id, const Vector2& impulse) {
    if (Body* b = find_(id)) {
        b->velocity += impulse * b->inverseMass();
    }
}

void PhysicsWorld2D::setVelocity(BodyId id, const Vector2& velocity) {
    if (Body* b = find_(id); b && b->type != BodyType::Static) {
        b->velocity = velocity;
    }
}

void PhysicsWorld2D::setPosition(BodyId id, const Vector2& position) {
    if (Body* b = find_(id)) {
        b->position = position;
    }
}

bool PhysicsWorld2D::collide_(const Body& a, const Body& b, Contact2D& contact) const {
    Manifold2D m;
    if (a.shape == ShapeType::Box && b.shape == ShapeType::Box) {
        m = collide(rectOf(a), rectOf(b));
    } else if (a.shape == ShapeType::Circle && b.shape == ShapeType::Circle) {
        m = collide(circleOf(a), circleOf(b));
    } else if (a.shape == ShapeType::Box) {
        m = collide(rectOf(a), circleOf(b));
    } else {
        m = collide(circleOf(a), rectOf(b));
    }
    if (!m.colliding) {
        return false;
    }
    contact.a = a.id;
    contact.b = b.id;
    contact.normal = m.normal;
    contact.depth = m.depth;
    contact.trigger = a.isTrigger || b.isTrigger;
    return true;
}

void PhysicsWorld2D::solve_(float dt) {
    // Integrate forces.
    for (Body& b : bodies_) {
        b.grounded = false;
        if (b.type != BodyType::Dynamic) {
            continue;
        }
        b.velocity += (gravity_ * b.gravityScale + b.force * b.inverseMass()) * dt;
        if (b.linearDamping > 0.0f) {
            b.velocity *= 1.0f / (1.0f + b.linearDamping * dt);
        }
    }

    // Broadphase: sweep and prune on x.
    std::vector<size_t> order(bodies_.size());
    for (size_t i = 0; i < order.size(); i++) {
        order[i] = i;
    }
    std::vector<Rect> bounds(bodies_.size());
    for (size_t i = 0; i < bodies_.size(); i++) {
        bounds[i] = bodies_[i].bounds();
    }
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return bounds[a].left() < bounds[b].left(); });

    std::vector<Contact2D> stepContacts;
    std::vector<std::pair<size_t, size_t>> pairs;
    for (size_t i = 0; i < order.size(); i++) {
        size_t ia = order[i];
        for (size_t j = i + 1; j < order.size(); j++) {
            size_t ib = order[j];
            if (bounds[ib].left() > bounds[ia].right()) {
                break; // sorted: nothing further can overlap on x
            }
            const Body& a = bodies_[ia];
            const Body& b = bodies_[ib];
            if (a.type != BodyType::Dynamic && b.type != BodyType::Dynamic &&
                !a.isTrigger && !b.isTrigger) {
                continue;
            }
            if (!layersInteract(a, b) || bounds[ia].bottom() > bounds[ib].top() ||
                bounds[ib].bottom() > bounds[ia].top()) {
                continue;
            }
            Contact2D contact;
            if (collide_(a, b, contact)) {
                stepContacts.push_back(contact);
                if (!contact.trigger) {
                    pairs.emplace_back(ia, ib);
                }
            }
        }
    }

    // Velocity resolution (sequential impulses).
    for (int iteration = 0; iteration < iterations_; iteration++) {
        size_t solid = 0;
        for (const Contact2D& c : stepContacts) {
            if (c.trigger) {
                continue;
            }
            Body& a = bodies_[pairs[solid].first];
            Body& b = bodies_[pairs[solid].second];
            solid++;
            float invA = a.inverseMass();
            float invB = b.inverseMass();
            float invSum = invA + invB;
            if (invSum <= 0.0f) {
                continue;
            }
            Vector2 relative = b.velocity - a.velocity;
            float along = Vector2::dot(relative, c.normal);
            if (along > 0.0f) {
                continue; // already separating
            }
            float restitution = std::max(a.restitution, b.restitution);
            // Small approach speeds don't bounce (prevents jitter at rest).
            if (-along < std::fabs(gravity_.length()) * dt * 2.0f) {
                restitution = 0.0f;
            }
            float j = -(1.0f + restitution) * along / invSum;
            Vector2 impulse = c.normal * j;
            a.velocity -= impulse * invA;
            b.velocity += impulse * invB;

            // Friction along the tangent, clamped by the Coulomb cone.
            relative = b.velocity - a.velocity;
            Vector2 tangent = relative - c.normal * Vector2::dot(relative, c.normal);
            float tangentLength = tangent.length();
            if (tangentLength > 1e-6f) {
                tangent /= tangentLength;
                float jt = -Vector2::dot(relative, tangent) / invSum;
                float mu = std::sqrt(a.friction * b.friction);
                jt = clamp(jt, -j * mu, j * mu);
                a.velocity -= tangent * (jt * invA);
                b.velocity += tangent * (jt * invB);
            }
        }
    }

    // Integrate velocity.
    for (Body& b : bodies_) {
        if (b.type != BodyType::Static) {
            b.position += b.velocity * dt;
        }
    }

    // Positional correction: push overlapping bodies apart (80%, with a
    // small slop so resting contacts stay stable).
    const float percent = 0.8f;
    const float slop = 0.01f;
    size_t solid = 0;
    for (const Contact2D& c : stepContacts) {
        if (c.trigger) {
            continue;
        }
        Body& a = bodies_[pairs[solid].first];
        Body& b = bodies_[pairs[solid].second];
        solid++;
        float invSum = a.inverseMass() + b.inverseMass();
        if (invSum <= 0.0f) {
            continue;
        }
        Vector2 correction = c.normal * (std::max(c.depth - slop, 0.0f) / invSum * percent);
        a.position -= correction * a.inverseMass();
        b.position += correction * b.inverseMass();
        // A body is grounded when the contact pushes it upward.
        if (c.normal.y < -0.5f) {
            a.grounded = true;
        }
        if (c.normal.y > 0.5f) {
            b.grounded = true;
        }
    }

    contacts_.insert(contacts_.end(), stepContacts.begin(), stepContacts.end());
}

void PhysicsWorld2D::step(float deltaTime) {
    contacts_.clear();
    if (deltaTime <= 0.0f || bodies_.empty()) {
        return;
    }
    float sub = deltaTime / (float)substeps_;
    std::vector<char> wasGrounded(bodies_.size(), 0);
    for (int i = 0; i < substeps_; i++) {
        solve_(sub);
        for (size_t b = 0; b < bodies_.size(); b++) {
            wasGrounded[b] = wasGrounded[b] || bodies_[b].grounded;
        }
    }
    for (size_t b = 0; b < bodies_.size(); b++) {
        bodies_[b].grounded = wasGrounded[b] != 0;
        bodies_[b].force = Vector2();
    }
    // Deduplicate contacts reported by several substeps.
    std::vector<Contact2D> unique;
    for (const Contact2D& c : contacts_) {
        bool seen = false;
        for (const Contact2D& u : unique) {
            seen = seen || (u.a == c.a && u.b == c.b);
        }
        if (!seen) {
            unique.push_back(c);
        }
    }
    contacts_.swap(unique);
    if (onContact_) {
        // Copy: the callback may create or destroy bodies.
        std::vector<Contact2D> report = contacts_;
        for (const Contact2D& c : report) {
            onContact_(c);
        }
    }
}

bool PhysicsWorld2D::raycast(const Vector2& origin, const Vector2& direction, float maxDistance,
                             PhysicsRayHit& hit, uint32_t mask) const {
    Vector2 dir = direction.normalized();
    if (dir.isZero()) {
        return false;
    }
    Ray2D ray{origin, dir};
    bool found = false;
    float best = maxDistance;
    for (const Body& b : bodies_) {
        if (b.isTrigger || !(b.layer & mask)) {
            continue;
        }
        RayHit2D h = b.shape == ShapeType::Circle ? ion::raycast(ray, circleOf(b), best)
                                                  : ion::raycast(ray, rectOf(b), best);
        if (h.hit && h.distance <= best) {
            best = h.distance;
            hit.body = b.id;
            hit.distance = h.distance;
            hit.point = h.point;
            hit.normal = h.normal;
            found = true;
        }
    }
    return found;
}

std::vector<BodyId> PhysicsWorld2D::queryRect(const Rect& rect, uint32_t mask) const {
    std::vector<BodyId> out;
    for (const Body& b : bodies_) {
        if (!(b.layer & mask)) {
            continue;
        }
        bool overlap = b.shape == ShapeType::Circle ? intersects(rect, circleOf(b))
                                                    : rect.intersects(rectOf(b));
        if (overlap) {
            out.push_back(b.id);
        }
    }
    return out;
}

std::vector<BodyId> PhysicsWorld2D::queryPoint(const Vector2& point, uint32_t mask) const {
    std::vector<BodyId> out;
    for (const Body& b : bodies_) {
        if (!(b.layer & mask)) {
            continue;
        }
        bool inside = b.shape == ShapeType::Circle ? circleOf(b).contains(point)
                                                   : rectOf(b).contains(point);
        if (inside) {
            out.push_back(b.id);
        }
    }
    return out;
}

void PhysicsWorld2D::clear() {
    bodies_.clear();
    contacts_.clear();
}

} // namespace ion
