#include <ion/math/Geometry.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ion {

// ---------------------------------------------------------------------------
// 2D overlap tests
// ---------------------------------------------------------------------------

bool intersects(const Circle& a, const Circle& b) {
    float r = a.radius + b.radius;
    return Vector2::distanceSquared(a.center, b.center) <= r * r;
}

bool intersects(const Rect& rect, const Circle& circle) {
    Vector2 closest = rect.closestPoint(circle.center);
    return Vector2::distanceSquared(closest, circle.center) <=
           circle.radius * circle.radius;
}

Manifold2D collide(const Rect& a, const Rect& b) {
    Manifold2D m;
    float overlapX = std::min(a.right(), b.right()) - std::max(a.left(), b.left());
    float overlapY = std::min(a.top(), b.top()) - std::max(a.bottom(), b.bottom());
    if (overlapX <= 0.0f || overlapY <= 0.0f) {
        return m;
    }
    m.colliding = true;
    Vector2 delta = b.center() - a.center();
    // Resolve along the axis of least penetration.
    if (overlapX < overlapY) {
        m.depth = overlapX;
        m.normal = Vector2(delta.x < 0.0f ? -1.0f : 1.0f, 0.0f);
    } else {
        m.depth = overlapY;
        m.normal = Vector2(0.0f, delta.y < 0.0f ? -1.0f : 1.0f);
    }
    return m;
}

Manifold2D collide(const Circle& a, const Circle& b) {
    Manifold2D m;
    Vector2 delta = b.center - a.center;
    float r = a.radius + b.radius;
    float distSq = delta.lengthSquared();
    if (distSq >= r * r) {
        return m;
    }
    m.colliding = true;
    float dist = std::sqrt(distSq);
    m.depth = r - dist;
    m.normal = dist > 0.0f ? delta / dist : Vector2(1.0f, 0.0f);
    return m;
}

Manifold2D collide(const Rect& a, const Circle& b) {
    Manifold2D m;
    Vector2 closest = a.closestPoint(b.center);
    Vector2 delta = b.center - closest;
    float distSq = delta.lengthSquared();
    bool inside = a.contains(b.center);
    if (!inside && distSq >= b.radius * b.radius) {
        return m;
    }
    m.colliding = true;
    if (!inside) {
        float dist = std::sqrt(distSq);
        m.normal = dist > 0.0f ? delta / dist : Vector2(1.0f, 0.0f);
        m.depth = b.radius - dist;
        return m;
    }
    // Center inside the rect: push out through the nearest edge.
    float dl = b.center.x - a.left();
    float dr = a.right() - b.center.x;
    float db = b.center.y - a.bottom();
    float dt = a.top() - b.center.y;
    float best = dl;
    m.normal = Vector2(-1.0f, 0.0f);
    if (dr < best) { best = dr; m.normal = Vector2(1.0f, 0.0f); }
    if (db < best) { best = db; m.normal = Vector2(0.0f, -1.0f); }
    if (dt < best) { best = dt; m.normal = Vector2(0.0f, 1.0f); }
    m.depth = best + b.radius;
    return m;
}

Manifold2D collide(const Circle& a, const Rect& b) {
    Manifold2D m = collide(b, a);
    m.normal = -m.normal;
    return m;
}

Vector2 closestPointOnSegment(const Vector2& a, const Vector2& b, const Vector2& p) {
    Vector2 ab = b - a;
    float lenSq = ab.lengthSquared();
    if (lenSq <= 0.0f) {
        return a;
    }
    float t = clamp(Vector2::dot(p - a, ab) / lenSq, 0.0f, 1.0f);
    return a + ab * t;
}

bool intersects(const Segment2D& s, const Segment2D& t, Vector2* point) {
    Vector2 r = s.b - s.a;
    Vector2 q = t.b - t.a;
    float denom = Vector2::cross(r, q);
    Vector2 diff = t.a - s.a;
    if (std::fabs(denom) < 1e-12f) {
        // Parallel. Collinear segments overlap if their projections do.
        if (std::fabs(Vector2::cross(diff, r)) > 1e-6f) {
            return false;
        }
        float rr = r.lengthSquared();
        if (rr <= 0.0f) {
            return false;
        }
        float t0 = Vector2::dot(diff, r) / rr;
        float t1 = t0 + Vector2::dot(q, r) / rr;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        if (t1 < 0.0f || t0 > 1.0f) {
            return false;
        }
        if (point) {
            *point = s.a + r * std::max(0.0f, t0);
        }
        return true;
    }
    float u = Vector2::cross(diff, q) / denom;
    float v = Vector2::cross(diff, r) / denom;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return false;
    }
    if (point) {
        *point = s.a + r * u;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 2D rays
// ---------------------------------------------------------------------------

RayHit2D raycast(const Ray2D& ray, const Rect& rect, float maxDistance) {
    RayHit2D hit;
    float tmin = 0.0f;
    float tmax = maxDistance;
    Vector2 normal;
    const float origin[2] = {ray.origin.x, ray.origin.y};
    const float dir[2] = {ray.direction.x, ray.direction.y};
    const float lo[2] = {rect.left(), rect.bottom()};
    const float hi[2] = {rect.right(), rect.top()};
    for (int axis = 0; axis < 2; axis++) {
        if (std::fabs(dir[axis]) < 1e-12f) {
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) {
                return hit;
            }
            continue;
        }
        float inv = 1.0f / dir[axis];
        float t1 = (lo[axis] - origin[axis]) * inv;
        float t2 = (hi[axis] - origin[axis]) * inv;
        float sideNormal = -1.0f;
        if (t1 > t2) {
            std::swap(t1, t2);
            sideNormal = 1.0f;
        }
        if (t1 > tmin) {
            tmin = t1;
            normal = axis == 0 ? Vector2(sideNormal, 0.0f) : Vector2(0.0f, sideNormal);
        }
        tmax = std::min(tmax, t2);
        if (tmin > tmax) {
            return hit;
        }
    }
    hit.hit = true;
    hit.distance = tmin;
    hit.point = ray.origin + ray.direction * tmin;
    hit.normal = normal;
    return hit;
}

RayHit2D raycast(const Ray2D& ray, const Circle& circle, float maxDistance) {
    RayHit2D hit;
    Vector2 m = ray.origin - circle.center;
    float b = Vector2::dot(m, ray.direction);
    float c = m.lengthSquared() - circle.radius * circle.radius;
    if (c > 0.0f && b > 0.0f) {
        return hit;
    }
    float disc = b * b - c;
    if (disc < 0.0f) {
        return hit;
    }
    float t = std::max(0.0f, -b - std::sqrt(disc));
    if (t > maxDistance) {
        return hit;
    }
    hit.hit = true;
    hit.distance = t;
    hit.point = ray.origin + ray.direction * t;
    hit.normal = (hit.point - circle.center).normalized();
    return hit;
}

RayHit2D raycast(const Ray2D& ray, const Segment2D& segment, float maxDistance) {
    RayHit2D hit;
    Vector2 q = segment.b - segment.a;
    float denom = Vector2::cross(ray.direction, q);
    if (std::fabs(denom) < 1e-12f) {
        return hit;
    }
    Vector2 diff = segment.a - ray.origin;
    float t = Vector2::cross(diff, q) / denom;
    float u = Vector2::cross(diff, ray.direction) / denom;
    if (t < 0.0f || t > maxDistance || u < 0.0f || u > 1.0f) {
        return hit;
    }
    hit.hit = true;
    hit.distance = t;
    hit.point = ray.origin + ray.direction * t;
    Vector2 n = q.perpendicular().normalized();
    hit.normal = Vector2::dot(n, ray.direction) > 0.0f ? -n : n;
    return hit;
}

bool pointInPolygon(const Vector2& point, const Vector2* vertices, size_t count) {
    if (!vertices || count < 3) {
        return false;
    }
    bool inside = false;
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        const Vector2& a = vertices[i];
        const Vector2& b = vertices[j];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

bool sweep(const Rect& moving, const Vector2& velocity, const Rect& target,
           float& timeOfImpact, Vector2& normal) {
    // Minkowski sum: cast the moving rect's center against the target
    // expanded by the moving rect's half size.
    Rect expanded(target.x - moving.width * 0.5f, target.y - moving.height * 0.5f,
                  target.width + moving.width, target.height + moving.height);
    if (moving.intersects(target)) {
        timeOfImpact = 0.0f;
        normal = Vector2();
        return true;
    }
    float len = velocity.length();
    if (len <= 0.0f) {
        return false;
    }
    Ray2D ray{moving.center(), velocity / len};
    RayHit2D hit = raycast(ray, expanded, len);
    if (!hit.hit) {
        return false;
    }
    timeOfImpact = hit.distance / len;
    normal = hit.normal;
    return true;
}

// ---------------------------------------------------------------------------
// 3D
// ---------------------------------------------------------------------------

AABB AABB::transformed(const Matrix4& m) const {
    // Arvo's method: transform the center, then accumulate absolute basis
    // contributions of the extents.
    Vector3 c = m * center();
    Vector3 e = extents();
    Vector3 out;
    for (int row = 0; row < 3; row++) {
        out[row] = std::fabs(m(row, 0)) * e.x + std::fabs(m(row, 1)) * e.y +
                   std::fabs(m(row, 2)) * e.z;
    }
    return AABB(c - out, c + out);
}

bool raycast(const Ray& ray, const AABB& box, float& t) {
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; axis++) {
        float o = ray.origin[axis];
        float d = ray.direction[axis];
        if (std::fabs(d) < 1e-12f) {
            if (o < box.min[axis] || o > box.max[axis]) {
                return false;
            }
            continue;
        }
        float inv = 1.0f / d;
        float t1 = (box.min[axis] - o) * inv;
        float t2 = (box.max[axis] - o) * inv;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) {
            return false;
        }
    }
    t = tmin;
    return true;
}

bool raycast(const Ray& ray, const Sphere& sphere, float& t) {
    Vector3 m = ray.origin - sphere.center;
    float b = Vector3::dot(m, ray.direction);
    float c = m.lengthSquared() - sphere.radius * sphere.radius;
    if (c > 0.0f && b > 0.0f) {
        return false;
    }
    float disc = b * b - c;
    if (disc < 0.0f) {
        return false;
    }
    t = std::max(0.0f, -b - std::sqrt(disc));
    return true;
}

bool raycast(const Ray& ray, const Plane& plane, float& t) {
    float denom = Vector3::dot(plane.normal, ray.direction);
    if (std::fabs(denom) < 1e-12f) {
        return false;
    }
    float hit = -plane.distance(ray.origin) / denom;
    if (hit < 0.0f) {
        return false;
    }
    t = hit;
    return true;
}

bool raycast(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c,
             float& t) {
    Vector3 e1 = b - a;
    Vector3 e2 = c - a;
    Vector3 p = Vector3::cross(ray.direction, e2);
    float det = Vector3::dot(e1, p);
    if (std::fabs(det) < 1e-12f) {
        return false;
    }
    float inv = 1.0f / det;
    Vector3 s = ray.origin - a;
    float u = Vector3::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    Vector3 q = Vector3::cross(s, e1);
    float v = Vector3::dot(ray.direction, q) * inv;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    float hit = Vector3::dot(e2, q) * inv;
    if (hit < 0.0f) {
        return false;
    }
    t = hit;
    return true;
}

Frustum Frustum::fromMatrix(const Matrix4& m) {
    // Gribb/Hartmann: planes are sums/differences of the clip matrix rows.
    auto row = [&](int r) { return Vector4(m(r, 0), m(r, 1), m(r, 2), m(r, 3)); };
    Vector4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    Vector4 raw[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
    Frustum f;
    for (int i = 0; i < 6; i++) {
        Vector3 n(raw[i].x, raw[i].y, raw[i].z);
        float len = n.length();
        if (len > 0.0f) {
            f.planes[i] = Plane{n / len, raw[i].w / len};
        }
    }
    return f;
}

bool Frustum::contains(const Vector3& point) const {
    for (const Plane& p : planes) {
        if (p.distance(point) < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersects(const Sphere& sphere) const {
    for (const Plane& p : planes) {
        if (p.distance(sphere.center) < -sphere.radius) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersects(const AABB& box) const {
    for (const Plane& p : planes) {
        // Test the box corner furthest along the plane normal.
        Vector3 v(p.normal.x >= 0.0f ? box.max.x : box.min.x,
                  p.normal.y >= 0.0f ? box.max.y : box.min.y,
                  p.normal.z >= 0.0f ? box.max.z : box.min.z);
        if (p.distance(v) < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace ion
