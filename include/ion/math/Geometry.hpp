#pragma once

#include <ion/math/Matrix4.hpp>
#include <ion/math/Vector2.hpp>
#include <ion/math/Vector3.hpp>

#include <cstddef>

namespace ion {

// ===========================================================================
// 2D shapes. Rects are axis-aligned, (x, y) is the minimum corner. With the
// engine's +y-up world space that is the bottom-left corner.
// ===========================================================================

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x, float y, float width, float height)
        : x(x), y(y), width(width), height(height) {}
    constexpr Rect(const Vector2& position, const Vector2& size)
        : x(position.x), y(position.y), width(size.x), height(size.y) {}

    static constexpr Rect fromCenter(const Vector2& center, const Vector2& size) {
        return Rect(center.x - size.x * 0.5f, center.y - size.y * 0.5f, size.x, size.y);
    }

    static constexpr Rect fromMinMax(const Vector2& minCorner, const Vector2& maxCorner) {
        return Rect(minCorner.x, minCorner.y, maxCorner.x - minCorner.x,
                    maxCorner.y - minCorner.y);
    }

    constexpr float left() const { return x; }
    constexpr float right() const { return x + width; }
    constexpr float bottom() const { return y; }
    constexpr float top() const { return y + height; }
    constexpr Vector2 min() const { return Vector2(x, y); }
    constexpr Vector2 max() const { return Vector2(x + width, y + height); }
    constexpr Vector2 position() const { return Vector2(x, y); }
    constexpr Vector2 size() const { return Vector2(width, height); }
    constexpr Vector2 center() const { return Vector2(x + width * 0.5f, y + height * 0.5f); }
    constexpr float area() const { return width * height; }
    constexpr bool isEmpty() const { return width <= 0.0f || height <= 0.0f; }

    // Half-open on the max edges so tiled rects never both contain a point.
    constexpr bool contains(const Vector2& p) const {
        return p.x >= x && p.x < x + width && p.y >= y && p.y < y + height;
    }

    constexpr bool contains(const Rect& other) const {
        return other.x >= x && other.y >= y && other.right() <= right() &&
               other.top() <= top();
    }

    // Overlap with positive area (touching edges do not intersect).
    constexpr bool intersects(const Rect& other) const {
        return x < other.right() && other.x < right() && y < other.top() &&
               other.y < top();
    }

    // Overlapping region, or an empty rect when there is none.
    constexpr Rect intersection(const Rect& other) const {
        float l = x > other.x ? x : other.x;
        float b = y > other.y ? y : other.y;
        float r = right() < other.right() ? right() : other.right();
        float t = top() < other.top() ? top() : other.top();
        if (r <= l || t <= b) {
            return Rect();
        }
        return Rect(l, b, r - l, t - b);
    }

    // Smallest rect containing both.
    constexpr Rect merged(const Rect& other) const {
        return fromMinMax(Vector2::min(min(), other.min()),
                          Vector2::max(max(), other.max()));
    }

    // Grows (or shrinks, if negative) by `amount` on every side.
    constexpr Rect expanded(float amount) const {
        return Rect(x - amount, y - amount, width + amount * 2.0f,
                    height + amount * 2.0f);
    }

    constexpr Rect translated(const Vector2& offset) const {
        return Rect(x + offset.x, y + offset.y, width, height);
    }

    // Closest point inside the rect to p.
    constexpr Vector2 closestPoint(const Vector2& p) const {
        return Vector2(clamp(p.x, x, right()), clamp(p.y, y, top()));
    }
};

inline constexpr bool operator==(const Rect& a, const Rect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

inline constexpr bool operator!=(const Rect& a, const Rect& b) { return !(a == b); }

struct Circle {
    Vector2 center;
    float radius = 0.0f;

    constexpr Circle() = default;
    constexpr Circle(const Vector2& center, float radius) : center(center), radius(radius) {}

    constexpr bool contains(const Vector2& p) const {
        return Vector2::distanceSquared(center, p) <= radius * radius;
    }

    constexpr Rect bounds() const {
        return Rect(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
    }
};

// Line segment from a to b.
struct Segment2D {
    Vector2 a;
    Vector2 b;
};

struct Ray2D {
    Vector2 origin;
    Vector2 direction; // should be normalized
};

// Result of a narrow-phase test. `normal` points from shape A toward shape
// B; moving A by -normal * depth (or B by +normal * depth) separates them.
struct Manifold2D {
    bool colliding = false;
    Vector2 normal;
    float depth = 0.0f;
};

struct RayHit2D {
    bool hit = false;
    float distance = 0.0f; // along the ray direction
    Vector2 point;
    Vector2 normal;
};

bool intersects(const Circle& a, const Circle& b);
bool intersects(const Rect& rect, const Circle& circle);
inline bool intersects(const Circle& circle, const Rect& rect) { return intersects(rect, circle); }
inline bool intersects(const Rect& a, const Rect& b) { return a.intersects(b); }

Manifold2D collide(const Rect& a, const Rect& b);
Manifold2D collide(const Circle& a, const Circle& b);
Manifold2D collide(const Rect& a, const Circle& b);
Manifold2D collide(const Circle& a, const Rect& b);

// Segment/segment intersection. On a hit, writes the intersection point.
bool intersects(const Segment2D& s, const Segment2D& t, Vector2* point = nullptr);

// Closest point on segment ab to p.
Vector2 closestPointOnSegment(const Vector2& a, const Vector2& b, const Vector2& p);

// Rays. maxDistance limits the cast; the hit normal faces the ray.
RayHit2D raycast(const Ray2D& ray, const Rect& rect, float maxDistance = 1e30f);
RayHit2D raycast(const Ray2D& ray, const Circle& circle, float maxDistance = 1e30f);
RayHit2D raycast(const Ray2D& ray, const Segment2D& segment, float maxDistance = 1e30f);

// Point in an arbitrary simple polygon (even-odd rule).
bool pointInPolygon(const Vector2& point, const Vector2* vertices, size_t count);

// Swept AABB: earliest time t in [0, 1] at which `moving`, displaced by
// `velocity`, first touches `target`. Returns false if it does not hit
// during the step. Useful for tunnelling-free platformer movement.
bool sweep(const Rect& moving, const Vector2& velocity, const Rect& target,
           float& timeOfImpact, Vector2& normal);

// ===========================================================================
// 3D shapes.
// ===========================================================================

struct AABB {
    Vector3 min;
    Vector3 max;

    constexpr AABB() = default;
    constexpr AABB(const Vector3& min, const Vector3& max) : min(min), max(max) {}

    static constexpr AABB fromCenterExtents(const Vector3& center, const Vector3& halfExtents) {
        return AABB(center - halfExtents, center + halfExtents);
    }

    constexpr Vector3 center() const { return (min + max) * 0.5f; }
    constexpr Vector3 extents() const { return (max - min) * 0.5f; }
    constexpr Vector3 size() const { return max - min; }

    constexpr bool contains(const Vector3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }

    constexpr bool intersects(const AABB& o) const {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y &&
               max.y >= o.min.y && min.z <= o.max.z && max.z >= o.min.z;
    }

    constexpr AABB merged(const AABB& o) const {
        return AABB(Vector3::min(min, o.min), Vector3::max(max, o.max));
    }

    constexpr AABB expandedToInclude(const Vector3& p) const {
        return AABB(Vector3::min(min, p), Vector3::max(max, p));
    }

    constexpr Vector3 closestPoint(const Vector3& p) const {
        return Vector3(clamp(p.x, min.x, max.x), clamp(p.y, min.y, max.y),
                       clamp(p.z, min.z, max.z));
    }

    // Bounds of this box after an affine transform.
    AABB transformed(const Matrix4& m) const;
};

struct Sphere {
    Vector3 center;
    float radius = 0.0f;

    constexpr bool contains(const Vector3& p) const {
        return Vector3::distanceSquared(center, p) <= radius * radius;
    }

    constexpr bool intersects(const Sphere& o) const {
        float r = radius + o.radius;
        return Vector3::distanceSquared(center, o.center) <= r * r;
    }

    constexpr bool intersects(const AABB& box) const {
        return Vector3::distanceSquared(box.closestPoint(center), center) <=
               radius * radius;
    }
};

// Plane: dot(normal, p) + d == 0. normal should be unit length.
struct Plane {
    Vector3 normal = Vector3::unitY();
    float d = 0.0f;

    static Plane fromPointNormal(const Vector3& point, const Vector3& normal) {
        Vector3 n = normal.normalized();
        return Plane{n, -Vector3::dot(n, point)};
    }

    // Signed distance: positive on the side the normal points to.
    constexpr float distance(const Vector3& p) const {
        return Vector3::dot(normal, p) + d;
    }
};

struct Ray {
    Vector3 origin;
    Vector3 direction; // should be normalized

    constexpr Vector3 at(float t) const { return origin + direction * t; }
};

// Each returns the hit distance along the ray in `t` (t >= 0). A ray that
// starts inside a box or sphere reports t = 0.
bool raycast(const Ray& ray, const AABB& box, float& t);
bool raycast(const Ray& ray, const Sphere& sphere, float& t);
bool raycast(const Ray& ray, const Plane& plane, float& t);
// Moller-Trumbore. Back faces count as hits.
bool raycast(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c,
             float& t);

//
// View frustum extracted from a view-projection matrix (OpenGL clip space).
// Use it to skip drawing objects that are fully off screen.
//
struct Frustum {
    Plane planes[6]; // left, right, bottom, top, near, far; normals point inward

    static Frustum fromMatrix(const Matrix4& viewProjection);

    bool contains(const Vector3& point) const;
    bool intersects(const Sphere& sphere) const;
    bool intersects(const AABB& box) const;
};

} // namespace ion
