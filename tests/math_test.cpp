#include <ion/math/Geometry.hpp>
#include <ion/math/MathUtils.hpp>
#include <ion/math/Matrix4.hpp>
#include <ion/math/Quaternion.hpp>
#include <ion/math/Random.hpp>
#include <ion/math/Vector2.hpp>
#include <ion/math/Vector3.hpp>
#include <ion/math/Vector4.hpp>
#include <ion/render/Color.hpp>

#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

static int failures = 0;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            failures++;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
    do {                                                                             \
        float _a = (a);                                                              \
        float _b = (b);                                                              \
        if (!(_a >= _b - (eps) && _a <= _b + (eps))) {                               \
            std::printf("FAIL %s:%d: %s = %f, expected %f (+/-%f)\n",                \
                        __FILE__, __LINE__, #a, _a, _b, (float)(eps));               \
            failures++;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_VEC3(v, ex, ey, ez, eps)                                               \
    do {                                                                             \
        ion::Vector3 _v = (v);                                                       \
        CHECK_NEAR(_v.x, ex, eps);                                                   \
        CHECK_NEAR(_v.y, ey, eps);                                                   \
        CHECK_NEAR(_v.z, ez, eps);                                                   \
    } while (0)

using namespace ion;

// Compile-time checks: the core vector ops are usable in constant expressions.
static_assert(Vector2(1, 2) + Vector2(3, 4) == Vector2(4, 6));
static_assert(-Vector3(1, 2, 3) == Vector3(-1, -2, -3));
static_assert(Vector3::dot({1, 0, 0}, {0, 1, 0}) == 0.0f);
static_assert(Vector3::cross(Vector3::unitX(), Vector3::unitY()) == Vector3::unitZ());
static_assert(Vector2(2, 3) * Vector2(4, 5) == Vector2(8, 15));
static_assert(nextPowerOfTwo(17) == 32 && nextPowerOfTwo(32) == 32 && nextPowerOfTwo(0) == 1);
static_assert(Rect(0, 0, 10, 10).contains(Vector2(5, 5)));
static_assert(!Rect(0, 0, 10, 10).contains(Vector2(10, 5))); // half-open
static_assert(Color::fromHex(0xFF8000) == Color(1.0f, 128 / 255.0f, 0.0f));

static void testScalar() {
    CHECK_NEAR(radians(180.0f), PI, 1e-6f);
    CHECK_NEAR(degrees(HALF_PI), 90.0f, 1e-4f);
    CHECK(clamp(5, 0, 3) == 3);
    CHECK(clamp(-1.0f, 0.0f, 1.0f) == 0.0f);
    CHECK_NEAR(lerp(2.0f, 4.0f, 0.25f), 2.5f, 1e-6f);
    CHECK_NEAR(inverseLerp(2.0f, 4.0f, 3.0f), 0.5f, 1e-6f);
    CHECK(inverseLerp(1.0f, 1.0f, 3.0f) == 0.0f);
    CHECK_NEAR(remap(5.0f, 0.0f, 10.0f, 100.0f, 200.0f), 150.0f, 1e-4f);
    CHECK_NEAR(smoothstep(0.0f, 1.0f, 0.5f), 0.5f, 1e-6f);
    CHECK(smoothstep(0.0f, 1.0f, -1.0f) == 0.0f);
    CHECK(smoothstep(0.0f, 1.0f, 2.0f) == 1.0f);
    CHECK_NEAR(smootherstep(0.0f, 1.0f, 0.5f), 0.5f, 1e-6f);
    CHECK(moveTowards(0.0f, 10.0f, 3.0f) == 3.0f);
    CHECK(moveTowards(9.0f, 10.0f, 3.0f) == 10.0f);
    CHECK(moveTowards(0.0f, -10.0f, 3.0f) == -3.0f);
    CHECK_NEAR(wrapAngle(3.0f * PI), -PI, 1e-4f);
    CHECK_NEAR(wrapAngle(-0.5f), -0.5f, 1e-6f);
    CHECK_NEAR(angleDifference(radians(350.0f), radians(10.0f)), radians(20.0f), 1e-4f);
    CHECK_NEAR(lerpAngle(radians(350.0f), radians(10.0f), 0.5f), radians(360.0f), 1e-4f);
    CHECK_NEAR(repeat(-1.0f, 3.0f), 2.0f, 1e-6f);
    CHECK_NEAR(pingPong(4.0f, 3.0f), 2.0f, 1e-6f);
    CHECK(sign(-2.0f) == -1.0f && sign(0.0f) == 0.0f && sign(5.0f) == 1.0f);
    CHECK(approxEqual(0.1f + 0.2f, 0.3f));
    float d = damp(0.0f, 10.0f, 5.0f, 0.1f);
    CHECK(d > 0.0f && d < 10.0f);
    // damp is frame-rate independent: two half steps == one full step.
    float half = damp(damp(0.0f, 10.0f, 5.0f, 0.05f), 10.0f, 5.0f, 0.05f);
    CHECK_NEAR(half, d, 1e-4f);
}

static void testVector2() {
    Vector2 v(3.0f, 4.0f);
    CHECK_NEAR(v.length(), 5.0f, 1e-6f);
    CHECK_NEAR(v.normalized().length(), 1.0f, 1e-6f);
    Vector2 n = v;
    CHECK_NEAR(n.normalize(), 5.0f, 1e-6f);
    CHECK_NEAR(n.x, 0.6f, 1e-6f);
    CHECK_NEAR(Vector2(0, 1).angle(), HALF_PI, 1e-6f);
    Vector2 r = Vector2(1, 0).rotated(HALF_PI);
    CHECK_NEAR(r.x, 0.0f, 1e-6f);
    CHECK_NEAR(r.y, 1.0f, 1e-6f);
    CHECK(Vector2(1, 0).perpendicular() == Vector2(0, 1));
    CHECK(Vector2::cross(Vector2(1, 0), Vector2(0, 1)) == 1.0f);
    CHECK_NEAR(Vector2::distance(Vector2(0, 0), Vector2(3, 4)), 5.0f, 1e-6f);
    CHECK_NEAR(v.clampedLength(2.5f).length(), 2.5f, 1e-5f);
    CHECK(v.clampedLength(10.0f) == v);
    CHECK(Vector2::min(Vector2(1, 5), Vector2(3, 2)) == Vector2(1, 2));
    CHECK(Vector2::max(Vector2(1, 5), Vector2(3, 2)) == Vector2(3, 5));
    Vector2 p = Vector2::project(Vector2(2, 2), Vector2(1, 0));
    CHECK(p == Vector2(2, 0));
    CHECK(Vector2::project(v, Vector2()) == Vector2());
    CHECK_NEAR(Vector2::angleBetween(Vector2(1, 0), Vector2(0, -1)), HALF_PI, 1e-6f);
    Vector2 f = Vector2::fromAngle(PI, 2.0f);
    CHECK_NEAR(f.x, -2.0f, 1e-5f);
    CHECK(Vector2::moveTowards(Vector2(), Vector2(10, 0), 4.0f) == Vector2(4, 0));
    CHECK(Vector2() .isZero());
    CHECK(v[0] == 3.0f && v[1] == 4.0f);
}

static void testVector3() {
    Vector3 a(1, 2, 3);
    CHECK(a + Vector3(1, 1, 1) == Vector3(2, 3, 4));
    CHECK(a * Vector3(2, 2, 2) == Vector3(2, 4, 6));
    CHECK(a / Vector3(1, 2, 3) == Vector3(1, 1, 1));
    CHECK(a.xy() == Vector2(1, 2));
    CHECK(Vector3(Vector2(1, 2), 3) == a);
    CHECK_NEAR(Vector3::distance(Vector3(), Vector3(2, 3, 6)), 7.0f, 1e-5f);
    CHECK(Vector3::reflect(Vector3(1, -1, 0), Vector3::unitY()) == Vector3(1, 1, 0));
    CHECK(Vector3::projectOnPlane(Vector3(1, 5, 2), Vector3::unitY()) == Vector3(1, 0, 2));
    CHECK_NEAR(Vector3::angleBetween(Vector3::unitX(), Vector3::unitZ()), HALF_PI, 1e-6f);
    CHECK(Vector3::lerp(Vector3(), Vector3(2, 4, 6), 0.5f) == Vector3(1, 2, 3));
    CHECK(Vector3::abs(Vector3(-1, 2, -3)) == Vector3(1, 2, 3));
    CHECK(Vector3::approxEqual(Vector3(1, 1, 1), Vector3(1, 1, 1.0000001f)));
    CHECK(a[2] == 3.0f);

    Vector4 v4(Vector3(1, 2, 3), 4);
    CHECK(v4.xyz() == a);
    CHECK(Vector4::dot(v4, Vector4(1, 1, 1, 1)) == 10.0f);
    CHECK(-v4 == Vector4(-1, -2, -3, -4));
    CHECK(Vector4() == Vector4(0, 0, 0, 1)); // existing default w = 1 preserved
}

static void testMatrix4() {
    Matrix4 rx = Matrix4::rotationX(HALF_PI);
    CHECK_VEC3(rx * Vector3(0, 1, 0), 0, 0, 1, 1e-6f);
    Matrix4 ry = Matrix4::rotationY(HALF_PI);
    CHECK_VEC3(ry * Vector3(0, 0, 1), 1, 0, 0, 1e-6f);
    Matrix4 rz = Matrix4::rotationZ(HALF_PI);
    CHECK_VEC3(rz * Vector3(1, 0, 0), 0, 1, 0, 1e-6f);
    // Axis-angle agrees with the dedicated axis rotations.
    CHECK(approxEqual(Matrix4::rotation(Vector3::unitX(), 0.7f), Matrix4::rotationX(0.7f)));
    CHECK(approxEqual(Matrix4::rotation(Vector3::unitY(), 0.7f), Matrix4::rotationY(0.7f)));
    CHECK(approxEqual(Matrix4::rotation(Vector3::unitZ(), 0.7f), Matrix4::rotationZ(0.7f)));
    CHECK(Matrix4::rotation(Vector3(), 1.0f) == Matrix4::identity());

    Matrix4 t = Matrix4::translation(Vector3(1, 2, 3));
    CHECK(t.getTranslation() == Vector3(1, 2, 3));
    CHECK_VEC3(t.transformDirection(Vector3(1, 0, 0)), 1, 0, 0, 0);
    CHECK_VEC3(Matrix4::scale(Vector3(2, 3, 4)).getScale(), 2, 3, 4, 1e-6f);

    Matrix4 m = Matrix4::compose(Vector3(1, 2, 3), Vector3(0.3f, 0.5f, 0.7f), Vector3(2, 2, 2));
    CHECK(approxEqual(m * m.inverted(), Matrix4::identity(), 1e-5f));
    CHECK_NEAR(Matrix4::identity().determinant(), 1.0f, 1e-6f);
    CHECK_NEAR(Matrix4::scale(Vector3(2, 3, 4)).determinant(), 24.0f, 1e-4f);
    CHECK_NEAR(m.determinant(), 8.0f, 1e-3f); // rotation keeps det, scale 2^3
    CHECK(m.transposed().transposed() == m);
    CHECK(Matrix4::transpose(t)(3, 0) == 1.0f);

    Vector4 hv = t * Vector4(0, 0, 0, 1);
    CHECK(hv == Vector4(1, 2, 3, 1));
    Vector4 hd = t * Vector4(1, 0, 0, 0);
    CHECK(hd == Vector4(1, 0, 0, 0));

    // Perspective divide: a point on the far plane maps to NDC z = 1.
    Matrix4 proj = Matrix4::perspective(HALF_PI, 1.0f, 1.0f, 100.0f);
    CHECK_NEAR(proj.transformPoint(Vector3(0, 0, -100)).z, 1.0f, 1e-4f);
    CHECK_NEAR(proj.transformPoint(Vector3(0, 0, -1)).z, -1.0f, 1e-4f);
}

static void testQuaternion() {
    Quaternion q = Quaternion::fromAxisAngle(Vector3::unitY(), HALF_PI);
    CHECK_VEC3(q.rotate(Vector3(0, 0, 1)), 1, 0, 0, 1e-6f);
    CHECK_VEC3(q * Vector3(0, 0, 1), 1, 0, 0, 1e-6f);
    CHECK(approxEqual(q.toMatrix(), Matrix4::rotationY(HALF_PI)));
    CHECK_NEAR(q.angle(), HALF_PI, 1e-5f);

    // Composition order matches matrices: (a * b) applies b first.
    Quaternion a = Quaternion::fromAxisAngle(Vector3::unitX(), 0.4f);
    Quaternion b = Quaternion::fromAxisAngle(Vector3::unitZ(), 1.1f);
    CHECK(approxEqual((a * b).toMatrix(), a.toMatrix() * b.toMatrix(), 1e-5f));

    Quaternion inv = q.inverse();
    CHECK_VEC3((q * inv).rotate(Vector3(1, 2, 3)), 1, 2, 3, 1e-5f);

    // Euler round trip.
    Quaternion e = Quaternion::fromEuler(0.3f, -0.8f, 0.5f);
    Vector3 euler = e.toEuler();
    CHECK_NEAR(euler.x, 0.3f, 1e-4f);
    CHECK_NEAR(euler.y, -0.8f, 1e-4f);
    CHECK_NEAR(euler.z, 0.5f, 1e-4f);
    CHECK(approxEqual(e.toMatrix(),
                      Matrix4::compose(Vector3(), Vector3(0.3f, -0.8f, 0.5f), Vector3(1, 1, 1)),
                      1e-5f));

    // Matrix round trip.
    Quaternion fm = Quaternion::fromMatrix(e.toMatrix());
    CHECK_NEAR(Quaternion::angleBetween(fm, e), 0.0f, 1e-3f);

    // fromTo, including the antiparallel case.
    Quaternion ft = Quaternion::fromTo(Vector3::unitX(), Vector3::unitY());
    CHECK_VEC3(ft.rotate(Vector3::unitX()), 0, 1, 0, 1e-5f);
    Quaternion opp = Quaternion::fromTo(Vector3::unitX(), -Vector3::unitX());
    CHECK_VEC3(opp.rotate(Vector3::unitX()), -1, 0, 0, 1e-5f);

    // lookRotation points forward (-Z) along the requested direction.
    Quaternion look = Quaternion::lookRotation(Vector3(1, 0, 0));
    CHECK_VEC3(look.forward(), 1, 0, 0, 1e-5f);
    CHECK_VEC3(look.up(), 0, 1, 0, 1e-5f);

    // slerp hits endpoints and the halfway angle.
    Quaternion s0 = Quaternion::identity();
    Quaternion s1 = Quaternion::fromAxisAngle(Vector3::unitZ(), 2.0f);
    CHECK_NEAR(Quaternion::angleBetween(Quaternion::slerp(s0, s1, 0.0f), s0), 0.0f, 1e-3f);
    CHECK_NEAR(Quaternion::angleBetween(Quaternion::slerp(s0, s1, 1.0f), s1), 0.0f, 1e-3f);
    CHECK_NEAR(Quaternion::slerp(s0, s1, 0.5f).angle(), 1.0f, 1e-4f);
    CHECK_NEAR(Quaternion::nlerp(s0, s1, 1.0f).angle(), 2.0f, 1e-4f);

    Matrix4 trs = composeTransform(Vector3(5, 0, 0), q, Vector3(2, 2, 2));
    CHECK_VEC3(trs * Vector3(0, 0, 1), 7, 0, 0, 1e-5f);
}

static void testRect() {
    Rect r(0, 0, 10, 5);
    CHECK(r.center() == Vector2(5, 2.5f));
    CHECK(r.right() == 10 && r.top() == 5);
    CHECK(r.intersects(Rect(9, 4, 2, 2)));
    CHECK(!r.intersects(Rect(10, 0, 2, 2))); // touching only
    CHECK(r.intersection(Rect(8, 3, 5, 5)) == Rect(8, 3, 2, 2));
    CHECK(r.intersection(Rect(20, 20, 1, 1)).isEmpty());
    CHECK(r.merged(Rect(-2, 1, 1, 10)) == Rect(-2, 0, 12, 11));
    CHECK(r.contains(Rect(1, 1, 2, 2)));
    CHECK(!r.contains(Rect(9, 1, 2, 2)));
    CHECK(Rect::fromCenter(Vector2(5, 5), Vector2(2, 4)) == Rect(4, 3, 2, 4));
    CHECK(r.expanded(1) == Rect(-1, -1, 12, 7));
    CHECK(r.closestPoint(Vector2(20, -3)) == Vector2(10, 0));
}

static void testCollision2D() {
    CHECK(intersects(Circle({0, 0}, 1), Circle({1.5f, 0}, 1)));
    CHECK(!intersects(Circle({0, 0}, 1), Circle({2.5f, 0}, 1)));
    CHECK(intersects(Rect(0, 0, 2, 2), Circle({3, 1}, 1.1f)));
    CHECK(!intersects(Rect(0, 0, 2, 2), Circle({3, 3}, 1.1f)));

    Manifold2D m = collide(Rect(0, 0, 2, 2), Rect(1.5f, 0.5f, 2, 2));
    CHECK(m.colliding);
    CHECK(m.normal == Vector2(1, 0));
    CHECK_NEAR(m.depth, 0.5f, 1e-6f);
    m = collide(Rect(0, 0, 2, 2), Rect(0.2f, -1.8f, 2, 2));
    CHECK(m.normal == Vector2(0, -1));
    CHECK_NEAR(m.depth, 0.2f, 1e-5f);
    CHECK(!collide(Rect(0, 0, 1, 1), Rect(5, 5, 1, 1)).colliding);

    m = collide(Circle({0, 0}, 1), Circle({1.5f, 0}, 1));
    CHECK(m.colliding);
    CHECK_NEAR(m.depth, 0.5f, 1e-6f);
    CHECK(m.normal == Vector2(1, 0));
    m = collide(Circle({0, 0}, 1), Circle({0, 0}, 1)); // concentric: still a valid normal
    CHECK(m.colliding && m.normal.length() > 0.99f);

    m = collide(Rect(0, 0, 4, 4), Circle({5, 2}, 1.5f));
    CHECK(m.colliding);
    CHECK(m.normal == Vector2(1, 0));
    CHECK_NEAR(m.depth, 0.5f, 1e-5f);
    m = collide(Rect(0, 0, 4, 4), Circle({3.5f, 2}, 1)); // center inside
    CHECK(m.colliding);
    CHECK(m.normal == Vector2(1, 0));
    CHECK_NEAR(m.depth, 1.5f, 1e-5f);
    Manifold2D flipped = collide(Circle({5, 2}, 1.5f), Rect(0, 0, 4, 4));
    CHECK(flipped.normal == Vector2(-1, 0));

    Vector2 hitPoint;
    CHECK(intersects(Segment2D{{0, 0}, {2, 2}}, Segment2D{{0, 2}, {2, 0}}, &hitPoint));
    CHECK(Vector2::approxEqual(hitPoint, Vector2(1, 1)));
    CHECK(!intersects(Segment2D{{0, 0}, {1, 0}}, Segment2D{{0, 1}, {1, 1}}));
    CHECK(intersects(Segment2D{{0, 0}, {2, 0}}, Segment2D{{1, 0}, {3, 0}})); // collinear overlap
    CHECK(Vector2::approxEqual(closestPointOnSegment({0, 0}, {10, 0}, {4, 5}), Vector2(4, 0)));
    CHECK(closestPointOnSegment({0, 0}, {10, 0}, {-4, 5}) == Vector2(0, 0));

    RayHit2D rh = raycast(Ray2D{{-5, 1}, {1, 0}}, Rect(0, 0, 2, 2));
    CHECK(rh.hit);
    CHECK_NEAR(rh.distance, 5.0f, 1e-5f);
    CHECK(rh.normal == Vector2(-1, 0));
    rh = raycast(Ray2D{{1, 5}, {0, -1}}, Rect(0, 0, 2, 2));
    CHECK(rh.hit && rh.normal == Vector2(0, 1));
    CHECK(!raycast(Ray2D{{-5, 5}, {1, 0}}, Rect(0, 0, 2, 2)).hit);
    CHECK(!raycast(Ray2D{{-5, 1}, {1, 0}}, Rect(0, 0, 2, 2), 3.0f).hit);
    rh = raycast(Ray2D{{-5, 0}, {1, 0}}, Circle({0, 0}, 1));
    CHECK(rh.hit);
    CHECK_NEAR(rh.distance, 4.0f, 1e-5f);
    CHECK(Vector2::approxEqual(rh.normal, Vector2(-1, 0)));
    rh = raycast(Ray2D{{0, -5}, {0, 1}}, Segment2D{{-1, 0}, {1, 0}});
    CHECK(rh.hit);
    CHECK_NEAR(rh.distance, 5.0f, 1e-5f);
    CHECK(Vector2::approxEqual(rh.normal, Vector2(0, -1)));

    Vector2 tri[3] = {{0, 0}, {4, 0}, {0, 4}};
    CHECK(pointInPolygon({1, 1}, tri, 3));
    CHECK(!pointInPolygon({3, 3}, tri, 3));
    CHECK(!pointInPolygon({1, 1}, tri, 2));

    float toi = -1;
    Vector2 normal;
    CHECK(sweep(Rect(0, 0, 1, 1), Vector2(10, 0), Rect(5, 0, 1, 1), toi, normal));
    CHECK_NEAR(toi, 0.4f, 1e-5f);
    CHECK(normal == Vector2(-1, 0));
    CHECK(!sweep(Rect(0, 0, 1, 1), Vector2(2, 0), Rect(5, 0, 1, 1), toi, normal));
    CHECK(!sweep(Rect(0, 0, 1, 1), Vector2(10, 0), Rect(5, 3, 1, 1), toi, normal));
}

static void testGeometry3D() {
    AABB box(Vector3(-1, -1, -1), Vector3(1, 1, 1));
    CHECK(box.contains(Vector3(0, 0, 0)));
    CHECK(!box.contains(Vector3(2, 0, 0)));
    CHECK(box.intersects(AABB(Vector3(0.5f, 0.5f, 0.5f), Vector3(3, 3, 3))));
    CHECK(!box.intersects(AABB(Vector3(2, 2, 2), Vector3(3, 3, 3))));
    CHECK(box.merged(AABB(Vector3(0, 0, 0), Vector3(5, 1, 1))).max == Vector3(5, 1, 1));
    AABB rotated = box.transformed(Matrix4::rotationY(PI / 4.0f));
    CHECK_NEAR(rotated.max.x, std::sqrt(2.0f), 1e-5f);
    CHECK_NEAR(rotated.max.y, 1.0f, 1e-5f);

    float t = -1;
    CHECK(raycast(Ray{Vector3(-5, 0, 0), Vector3(1, 0, 0)}, box, t));
    CHECK_NEAR(t, 4.0f, 1e-5f);
    CHECK(!raycast(Ray{Vector3(-5, 3, 0), Vector3(1, 0, 0)}, box, t));
    CHECK(!raycast(Ray{Vector3(-5, 0, 0), Vector3(-1, 0, 0)}, box, t));
    CHECK(raycast(Ray{Vector3(0, 0, 0), Vector3(1, 0, 0)}, box, t) && t == 0.0f);

    Sphere sphere{Vector3(0, 0, -10), 2};
    CHECK(raycast(Ray{Vector3(), Vector3::forward()}, sphere, t));
    CHECK_NEAR(t, 8.0f, 1e-5f);
    CHECK(sphere.intersects(Sphere{Vector3(0, 0, -13), 1.5f}));
    CHECK(sphere.intersects(AABB(Vector3(-1, -1, -8.5f), Vector3(1, 1, -7))));

    Plane ground = Plane::fromPointNormal(Vector3(0, 2, 0), Vector3(0, 5, 0));
    CHECK_NEAR(ground.distance(Vector3(0, 5, 0)), 3.0f, 1e-6f);
    CHECK(raycast(Ray{Vector3(0, 10, 0), Vector3(0, -1, 0)}, ground, t));
    CHECK_NEAR(t, 8.0f, 1e-5f);
    CHECK(!raycast(Ray{Vector3(0, 10, 0), Vector3(0, 1, 0)}, ground, t));

    CHECK(raycast(Ray{Vector3(0.2f, 0.2f, 5), Vector3(0, 0, -1)}, Vector3(0, 0, 0),
                  Vector3(1, 0, 0), Vector3(0, 1, 0), t));
    CHECK_NEAR(t, 5.0f, 1e-5f);
    CHECK(!raycast(Ray{Vector3(0.8f, 0.8f, 5), Vector3(0, 0, -1)}, Vector3(0, 0, 0),
                   Vector3(1, 0, 0), Vector3(0, 1, 0), t));

    Matrix4 view = Matrix4::lookAt(Vector3(0, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0));
    Matrix4 proj = Matrix4::perspective(HALF_PI, 1.0f, 0.1f, 100.0f);
    Frustum f = Frustum::fromMatrix(proj * view);
    CHECK(f.contains(Vector3(0, 0, -10)));
    CHECK(!f.contains(Vector3(0, 0, 10)));        // behind
    CHECK(!f.contains(Vector3(0, 0, -200)));      // beyond far
    CHECK(!f.contains(Vector3(50, 0, -10)));      // outside 90 deg fov
    CHECK(f.intersects(Sphere{Vector3(12, 0, -10), 3}));
    CHECK(!f.intersects(Sphere{Vector3(20, 0, -10), 3}));
    CHECK(f.intersects(AABB(Vector3(9, -1, -11), Vector3(11, 1, -9))));
    CHECK(!f.intersects(AABB(Vector3(-1, -1, 5), Vector3(1, 1, 6))));
}

static void testRandom() {
    Random a(42), b(42), c(7);
    bool same = true, differs = false;
    for (int i = 0; i < 100; i++) {
        uint32_t va = a.nextU32();
        same = same && va == b.nextU32();
        differs = differs || va != c.nextU32();
    }
    CHECK(same);
    CHECK(differs);

    Random r(1);
    std::set<int> seen;
    bool inRange = true;
    for (int i = 0; i < 2000; i++) {
        int v = r.range(-3, 3);
        inRange = inRange && v >= -3 && v <= 3;
        seen.insert(v);
        float f = r.nextFloat();
        inRange = inRange && f >= 0.0f && f < 1.0f;
        float g = r.range(5.0f, 6.0f);
        inRange = inRange && g >= 5.0f && g < 6.0f;
        inRange = inRange && r.insideUnitCircle().length() <= 1.0001f;
        inRange = inRange && std::fabs(r.unitVector3().length() - 1.0f) < 1e-4f;
    }
    CHECK(inRange);
    CHECK(seen.size() == 7);
    CHECK(r.range(5, 5) == 5);
    CHECK(r.range(3, 1) >= 1);
    CHECK(r.nextBelow(0) == 0);

    float mean = 0.0f;
    for (int i = 0; i < 5000; i++) {
        mean += r.gaussian(10.0f, 2.0f);
    }
    CHECK_NEAR(mean / 5000.0f, 10.0f, 0.2f);

    float weights[3] = {0.0f, 1.0f, 0.0f};
    CHECK(r.weightedIndex(weights, 3) == 1);
    float zeros[2] = {0.0f, 0.0f};
    CHECK(r.weightedIndex(zeros, 2) == 2);

    std::vector<int> items = {1, 2, 3, 4, 5, 6, 7, 8};
    r.shuffle(items);
    std::set<int> unique(items.begin(), items.end());
    CHECK(unique.size() == 8);
    CHECK(unique.count(r.pick(items)) == 1);
}

static void testColor() {
    Color c = Color::fromHSV(120.0f, 1.0f, 1.0f);
    CHECK_NEAR(c.r, 0.0f, 1e-5f);
    CHECK_NEAR(c.g, 1.0f, 1e-5f);
    float h, s, v;
    Color::fromHSV(200.0f, 0.5f, 0.8f).toHSV(h, s, v);
    CHECK_NEAR(h, 200.0f, 1e-3f);
    CHECK_NEAR(s, 0.5f, 1e-5f);
    CHECK_NEAR(v, 0.8f, 1e-5f);
    CHECK(Color::red().toRGBA8() == 0xFF0000FFu);
    CHECK(Color::fromHexRGBA(0x11223344).toRGBA8() == 0x44332211u);
    CHECK(Color::lerp(Color::black(), Color::white(), 0.5f) == Color(0.5f, 0.5f, 0.5f));
    CHECK(Color::white() * Color(0.5f, 0.25f, 1.0f, 0.5f) == Color(0.5f, 0.25f, 1.0f, 0.5f));
    CHECK(Color::white().withAlpha(0.2f).a == 0.2f);
    CHECK(Color(0.5f, 1.0f, 1.0f, 0.5f).premultiplied() == Color(0.25f, 0.5f, 0.5f, 0.5f));
    CHECK(Color(2.0f, -1.0f, 0.5f).clamped() == Color(1.0f, 0.0f, 0.5f));
    CHECK_NEAR(Color::white().luminance(), 1.0f, 1e-5f);
    CHECK(Color::black().lighten(0.5f) == Color(0.5f, 0.5f, 0.5f));
}

int main() {
    testScalar();
    testVector2();
    testVector3();
    testMatrix4();
    testQuaternion();
    testRect();
    testCollision2D();
    testGeometry3D();
    testRandom();
    testColor();

    if (failures == 0) {
        std::printf("All math tests passed\n");
        return 0;
    }
    std::printf("%d math test(s) FAILED\n", failures);
    return 1;
}
