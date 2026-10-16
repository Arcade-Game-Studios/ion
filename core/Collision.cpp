#include <ion/physics/Collision.hpp>

#include <algorithm>
#include <cmath>

namespace ion {

namespace {

constexpr float kEps = 1e-6f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

Vector3 clampPoint(const Vector3& p, const Aabb& b) {
    return {clampf(p.x, b.min.x, b.max.x), clampf(p.y, b.min.y, b.max.y),
            clampf(p.z, b.min.z, b.max.z)};
}

bool normalizedRay(const Ray& ray, Vector3& dir) {
    float len = ray.direction.length();
    if (len < kEps) return false;
    dir = ray.direction / len;
    return true;
}

} // namespace

bool intersects(const Aabb& a, const Aabb& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y &&
           a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

bool intersects(const Sphere& a, const Sphere& b) {
    float r = a.radius + b.radius;
    return (b.center - a.center).lengthSquared() <= r * r;
}

bool intersects(const Sphere& s, const Aabb& b) {
    return (clampPoint(s.center, b) - s.center).lengthSquared() <= s.radius * s.radius;
}

bool collide(const Sphere& a, const Sphere& b, Contact& out) {
    Vector3 d = b.center - a.center;
    float r = a.radius + b.radius;
    float dist2 = d.lengthSquared();
    if (dist2 >= r * r) return false;
    float dist = std::sqrt(dist2);
    out.normal = dist > kEps ? d / dist : Vector3(0.0f, 1.0f, 0.0f);
    out.depth = r - dist;
    out.point = a.center + out.normal * (a.radius - out.depth * 0.5f);
    return true;
}

bool collide(const Sphere& a, const Aabb& b, Contact& out) {
    Vector3 p = clampPoint(a.center, b);
    Vector3 diff = p - a.center;
    float dist2 = diff.lengthSquared();
    if (dist2 > kEps * kEps) {
        if (dist2 >= a.radius * a.radius) return false;
        float dist = std::sqrt(dist2);
        out.normal = diff / dist;
        out.depth = a.radius - dist;
        out.point = p;
        return true;
    }

    // Center is inside the box: leave through the nearest face.
    const Vector3& c = a.center;
    float best = c.x - b.min.x;
    Vector3 outward(-1.0f, 0.0f, 0.0f);
    auto consider = [&](float d, Vector3 n) {
        if (d < best) {
            best = d;
            outward = n;
        }
    };
    consider(b.max.x - c.x, {1, 0, 0});
    consider(c.y - b.min.y, {0, -1, 0});
    consider(b.max.y - c.y, {0, 1, 0});
    consider(c.z - b.min.z, {0, 0, -1});
    consider(b.max.z - c.z, {0, 0, 1});
    out.normal = outward * -1.0f; // sphere -> box
    out.depth = best + a.radius;
    out.point = c + outward * best;
    return true;
}

bool collide(const Aabb& a, const Aabb& b, Contact& out) {
    float ox = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
    float oy = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);
    float oz = std::min(a.max.z, b.max.z) - std::max(a.min.z, b.min.z);
    if (ox <= 0.0f || oy <= 0.0f || oz <= 0.0f) return false;

    Vector3 d = b.center() - a.center();
    if (ox <= oy && ox <= oz) {
        out.normal = {d.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
        out.depth = ox;
    } else if (oy <= oz) {
        out.normal = {0.0f, d.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
        out.depth = oy;
    } else {
        out.normal = {0.0f, 0.0f, d.z >= 0.0f ? 1.0f : -1.0f};
        out.depth = oz;
    }
    Vector3 lo(std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y), std::max(a.min.z, b.min.z));
    Vector3 hi(std::min(a.max.x, b.max.x), std::min(a.max.y, b.max.y), std::min(a.max.z, b.max.z));
    out.point = (lo + hi) * 0.5f;
    return true;
}

bool raycast(const Ray& ray, const Aabb& box, float maxDistance, RayHit& out) {
    Vector3 dir;
    if (!normalizedRay(ray, dir)) return false;

    const float o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float d[3] = {dir.x, dir.y, dir.z};
    const float lo[3] = {box.min.x, box.min.y, box.min.z};
    const float hi[3] = {box.max.x, box.max.y, box.max.z};

    float tNear = -INFINITY, tFar = INFINITY;
    int axis = 0;
    float sign = 1.0f;
    for (int i = 0; i < 3; i++) {
        if (std::fabs(d[i]) < kEps) {
            if (o[i] < lo[i] || o[i] > hi[i]) return false;
            continue;
        }
        float t1 = (lo[i] - o[i]) / d[i];
        float t2 = (hi[i] - o[i]) / d[i];
        float s = -1.0f; // entering through the min face -> normal points -axis
        if (t1 > t2) {
            std::swap(t1, t2);
            s = 1.0f;
        }
        if (t1 > tNear) {
            tNear = t1;
            axis = i;
            sign = s;
        }
        tFar = std::min(tFar, t2);
        if (tNear > tFar) return false;
    }
    if (tNear < 0.0f || tNear > maxDistance) return false; // inside, behind, or too far

    out.distance = tNear;
    out.point = ray.origin + dir * tNear;
    out.normal = {};
    (axis == 0 ? out.normal.x : axis == 1 ? out.normal.y : out.normal.z) = sign;
    return true;
}

bool raycast(const Ray& ray, const Sphere& sphere, float maxDistance, RayHit& out) {
    Vector3 dir;
    if (!normalizedRay(ray, dir)) return false;
    Vector3 oc = ray.origin - sphere.center;
    float b = Vector3::dot(oc, dir);
    float c = Vector3::dot(oc, oc) - sphere.radius * sphere.radius;
    if (c < 0.0f) return false; // origin inside
    float disc = b * b - c;
    if (disc < 0.0f) return false;
    float t = -b - std::sqrt(disc);
    if (t < 0.0f || t > maxDistance) return false;
    out.distance = t;
    out.point = ray.origin + dir * t;
    out.normal = (out.point - sphere.center) / sphere.radius;
    return true;
}

bool raycastTriangle(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c,
                     float maxDistance, RayHit& out) {
    Vector3 dir;
    if (!normalizedRay(ray, dir)) return false;
    Vector3 e1 = b - a, e2 = c - a;
    Vector3 pvec = Vector3::cross(dir, e2);
    float det = Vector3::dot(e1, pvec);
    if (std::fabs(det) < 1e-9f) return false; // parallel
    float inv = 1.0f / det;
    Vector3 tvec = ray.origin - a;
    float u = Vector3::dot(tvec, pvec) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    Vector3 qvec = Vector3::cross(tvec, e1);
    float v = Vector3::dot(dir, qvec) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    float t = Vector3::dot(e2, qvec) * inv;
    if (t < kEps || t > maxDistance) return false;
    out.distance = t;
    out.point = ray.origin + dir * t;
    Vector3 n = Vector3::cross(e1, e2).normalized();
    out.normal = Vector3::dot(n, dir) > 0.0f ? n * -1.0f : n;
    return true;
}

} // namespace ion
