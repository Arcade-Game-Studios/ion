#pragma once

#include <ion/math/Vector3.hpp>

namespace ion {

//
// Geometric queries: overlap tests with contact information, and ray casts.
// All shapes are in world space. These are stateless and allocation free, so
// they are usable on their own (picking, line of sight, triggers) as well as
// through PhysicsWorld.
//

struct Ray {
    Vector3 origin;
    Vector3 direction = {0.0f, 0.0f, -1.0f}; // need not be normalised
};

struct Aabb {
    Vector3 min;
    Vector3 max;

    static Aabb fromCenter(const Vector3& center, const Vector3& halfExtents) {
        return {center - halfExtents, center + halfExtents};
    }
    Vector3 center() const { return (min + max) * 0.5f; }
    Vector3 halfExtents() const { return (max - min) * 0.5f; }
    bool contains(const Vector3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }
};

struct Sphere {
    Vector3 center;
    float radius = 0.5f;
};

// Result of an overlap test between shapes A and B.
struct Contact {
    Vector3 normal; // unit vector pointing from A toward B
    float depth = 0.0f; // penetration distance along the normal (> 0)
    Vector3 point;  // a point on the contact surface
};

struct RayHit {
    float distance = 0.0f; // along the (normalised) ray direction
    Vector3 point;
    Vector3 normal; // surface normal facing the ray origin
};

bool intersects(const Aabb& a, const Aabb& b);
bool intersects(const Sphere& a, const Sphere& b);
bool intersects(const Sphere& s, const Aabb& b);

bool collide(const Sphere& a, const Sphere& b, Contact& out);
bool collide(const Sphere& a, const Aabb& b, Contact& out);
bool collide(const Aabb& a, const Aabb& b, Contact& out);

// Ray casts report the nearest hit within maxDistance. A ray starting inside
// a shape does not hit it.
bool raycast(const Ray& ray, const Aabb& box, float maxDistance, RayHit& out);
bool raycast(const Ray& ray, const Sphere& sphere, float maxDistance, RayHit& out);
// Two-sided triangle test (Moller-Trumbore); normal faces the ray origin.
bool raycastTriangle(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c,
                     float maxDistance, RayHit& out);

} // namespace ion
