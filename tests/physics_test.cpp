#include <ion/physics/Collision.hpp>
#include <ion/physics/PhysicsWorld.hpp>

#include <cmath>
#include <cstdio>

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
        float _a = (a), _b = (b);                                                    \
        if (std::fabs(_a - _b) > (eps)) {                                            \
            std::printf("FAIL %s:%d: %s = %f, expected %f (+/-%f)\n", __FILE__,      \
                        __LINE__, #a, _a, _b, (float)(eps));                         \
            failures++;                                                              \
        }                                                                            \
    } while (0)

using namespace ion;

static void testOverlap() {
    Aabb a{{0, 0, 0}, {2, 2, 2}};
    CHECK(intersects(a, Aabb{{1, 1, 1}, {3, 3, 3}}));
    CHECK(!intersects(a, Aabb{{2.5f, 0, 0}, {4, 2, 2}}));
    CHECK(intersects(Sphere{{0, 0, 0}, 1}, Sphere{{1.5f, 0, 0}, 1}));
    CHECK(!intersects(Sphere{{0, 0, 0}, 1}, Sphere{{2.5f, 0, 0}, 1}));
    CHECK(intersects(Sphere{{3, 1, 1}, 1.1f}, a));
    CHECK(!intersects(Sphere{{3, 1, 1}, 0.9f}, a));
    CHECK(a.contains({1, 1, 1}) && !a.contains({3, 1, 1}));
    CHECK_NEAR(a.center().x, 1.0f, 1e-6f);
    CHECK_NEAR(a.halfExtents().y, 1.0f, 1e-6f);
}

static void testContacts() {
    Contact c;
    // Sphere-sphere: normal runs a -> b.
    CHECK(collide(Sphere{{0, 0, 0}, 1}, Sphere{{1.5f, 0, 0}, 1}, c));
    CHECK_NEAR(c.normal.x, 1.0f, 1e-5f);
    CHECK_NEAR(c.depth, 0.5f, 1e-5f);
    CHECK_NEAR(c.point.x, 0.75f, 1e-5f);
    CHECK(!collide(Sphere{{0, 0, 0}, 1}, Sphere{{3, 0, 0}, 1}, c));
    // Coincident centers still yield a usable normal.
    CHECK(collide(Sphere{{0, 0, 0}, 1}, Sphere{{0, 0, 0}, 1}, c));
    CHECK_NEAR(c.normal.length(), 1.0f, 1e-5f);

    // Sphere above a box top: normal points down (sphere -> box).
    Aabb box{{-5, -1, -5}, {5, 0, 5}};
    CHECK(collide(Sphere{{0, 0.4f, 0}, 0.5f}, box, c));
    CHECK_NEAR(c.normal.y, -1.0f, 1e-5f);
    CHECK_NEAR(c.depth, 0.1f, 1e-5f);
    CHECK(!collide(Sphere{{0, 0.6f, 0}, 0.5f}, box, c));
    // Sphere center inside the box exits through the nearest face.
    CHECK(collide(Sphere{{0, -0.2f, 0}, 0.5f}, box, c));
    CHECK_NEAR(c.normal.y, -1.0f, 1e-5f); // leaves upward, so sphere->box is down
    CHECK_NEAR(c.depth, 0.7f, 1e-5f);

    // Box-box: smallest-overlap axis, normal a -> b.
    CHECK(collide(Aabb{{0, 0, 0}, {2, 2, 2}}, Aabb{{1.8f, 0, 0}, {4, 2, 2}}, c));
    CHECK_NEAR(c.normal.x, 1.0f, 1e-5f);
    CHECK_NEAR(c.depth, 0.2f, 1e-5f);
    CHECK(collide(Aabb{{1.8f, 0, 0}, {4, 2, 2}}, Aabb{{0, 0, 0}, {2, 2, 2}}, c));
    CHECK_NEAR(c.normal.x, -1.0f, 1e-5f);
    CHECK(!collide(Aabb{{0, 0, 0}, {1, 1, 1}}, Aabb{{2, 0, 0}, {3, 1, 1}}, c));
    // Touching exactly is not a collision (no depth).
    CHECK(!collide(Aabb{{0, 0, 0}, {1, 1, 1}}, Aabb{{1, 0, 0}, {2, 1, 1}}, c));
}

static void testRaycastBox() {
    Aabb box{{-1, -1, -1}, {1, 1, 1}};
    RayHit h;
    CHECK(raycast(Ray{{0, 0, 5}, {0, 0, -1}}, box, 100.0f, h));
    CHECK_NEAR(h.distance, 4.0f, 1e-5f);
    CHECK_NEAR(h.normal.z, 1.0f, 1e-5f);
    CHECK_NEAR(h.point.z, 1.0f, 1e-5f);
    // Direction need not be normalised; distance is still in world units.
    CHECK(raycast(Ray{{0, 0, 5}, {0, 0, -10}}, box, 100.0f, h));
    CHECK_NEAR(h.distance, 4.0f, 1e-5f);
    // From the other side: normal flips.
    CHECK(raycast(Ray{{-5, 0, 0}, {1, 0, 0}}, box, 100.0f, h));
    CHECK_NEAR(h.normal.x, -1.0f, 1e-5f);
    CHECK(!raycast(Ray{{0, 0, 5}, {0, 0, -1}}, box, 3.0f, h));   // too short
    CHECK(!raycast(Ray{{0, 0, 5}, {0, 0, 1}}, box, 100.0f, h));  // pointing away
    CHECK(!raycast(Ray{{3, 0, 5}, {0, 0, -1}}, box, 100.0f, h)); // misses
    CHECK(!raycast(Ray{{0, 0, 0}, {0, 0, -1}}, box, 100.0f, h)); // starts inside
    CHECK(!raycast(Ray{{0, 0, 0}, {0, 0, 0}}, box, 100.0f, h));  // zero direction
    // Diagonal hit picks the right face.
    CHECK(raycast(Ray{{-3, 3, 0}, {1, -1, 0}}, box, 100.0f, h));
    CHECK_NEAR(h.distance, std::sqrt(2.0f) * 2.0f, 1e-4f);
}

static void testRaycastSphereAndTriangle() {
    Sphere s{{0, 0, 0}, 1};
    RayHit h;
    CHECK(raycast(Ray{{0, 0, 5}, {0, 0, -1}}, s, 100.0f, h));
    CHECK_NEAR(h.distance, 4.0f, 1e-5f);
    CHECK_NEAR(h.normal.z, 1.0f, 1e-5f);
    CHECK(!raycast(Ray{{0, 2, 5}, {0, 0, -1}}, s, 100.0f, h));
    CHECK(!raycast(Ray{{0, 0, 5}, {0, 0, 1}}, s, 100.0f, h));
    CHECK(!raycast(Ray{{0, 0, 0.5f}, {0, 0, -1}}, s, 100.0f, h)); // inside
    CHECK(raycast(Ray{{0, 0.99f, 5}, {0, 0, -1}}, s, 100.0f, h)); // grazing

    Vector3 a{-1, -1, 0}, b{1, -1, 0}, c{0, 1, 0};
    CHECK(raycastTriangle(Ray{{0, 0, 3}, {0, 0, -1}}, a, b, c, 100.0f, h));
    CHECK_NEAR(h.distance, 3.0f, 1e-5f);
    CHECK_NEAR(h.normal.z, 1.0f, 1e-5f);
    CHECK(raycastTriangle(Ray{{0, 0, -3}, {0, 0, 1}}, a, b, c, 100.0f, h)); // two-sided
    CHECK_NEAR(h.normal.z, -1.0f, 1e-5f);
    CHECK(!raycastTriangle(Ray{{2, 0, 3}, {0, 0, -1}}, a, b, c, 100.0f, h)); // outside
    CHECK(!raycastTriangle(Ray{{0, 0, 3}, {1, 0, 0}}, a, b, c, 100.0f, h));  // parallel
    CHECK(!raycastTriangle(Ray{{0, 0, 3}, {0, 0, -1}}, a, b, c, 2.0f, h));   // too far
}

static BodyId makeFloor(PhysicsWorld& w, float restitution = 0.0f) {
    BodyDesc d;
    d.type = BodyType::Static;
    d.shape = ShapeType::Box;
    d.halfExtents = {50, 0.5f, 50};
    d.position = {0, -0.5f, 0}; // top surface at y = 0
    d.restitution = restitution;
    return w.createBody(d);
}

static void testFreeFall() {
    PhysicsWorld w;
    BodyDesc d;
    d.position = {0, 100, 0};
    d.linearDamping = 0.0f;
    BodyId b = w.createBody(d);
    for (int i = 0; i < 60; i++) w.step(1.0f / 60.0f); // 1 second
    CHECK_NEAR(w.position(b).y, 100.0f - 0.5f * 9.81f, 0.1f);
    CHECK_NEAR(w.velocity(b).y, -9.81f, 0.05f);

    // Gravity can be disabled per body and changed per world.
    d.useGravity = false;
    BodyId f = w.createBody(d);
    w.gravity = {0, 0, -5};
    for (int i = 0; i < 60; i++) w.step(1.0f / 60.0f);
    CHECK_NEAR(w.position(f).y, 100.0f, 1e-3f);
    CHECK(w.velocity(b).z < -4.5f);
}

static void testRestsOnFloor() {
    PhysicsWorld w;
    makeFloor(w);
    BodyDesc d;
    d.position = {0, 5, 0};
    d.restitution = 0.0f;
    BodyId ball = w.createBody(d);
    for (int i = 0; i < 60 * 4; i++) w.step(1.0f / 60.0f);
    CHECK_NEAR(w.position(ball).y, 0.5f, 0.02f);
    CHECK(std::fabs(w.velocity(ball).y) < 0.1f);
}

static void testBounce() {
    PhysicsWorld w;
    makeFloor(w, 0.8f);
    BodyDesc d;
    d.position = {0, 4.5f, 0}; // drops 4 m to contact
    d.restitution = 0.8f;
    d.linearDamping = 0.0f;
    BodyId ball = w.createBody(d);
    float peak = 0.0f;
    bool bounced = false;
    for (int i = 0; i < 60 * 3; i++) {
        w.step(1.0f / 60.0f);
        if (w.velocity(ball).y > 0.5f) bounced = true;
        if (bounced) peak = std::max(peak, w.position(ball).y - 0.5f);
    }
    CHECK(bounced);
    // Energy: rebound height ~ e^2 * h = 0.64 * 4 = 2.56 m.
    CHECK(peak > 2.2f && peak < 2.9f);
}

static void testElasticCollision() {
    PhysicsWorld w;
    w.gravity = {};
    BodyDesc d;
    d.restitution = 1.0f;
    d.friction = 0.0f;
    d.linearDamping = 0.0f;
    d.position = {-2, 0, 0};
    d.velocity = {4, 0, 0};
    BodyId a = w.createBody(d);
    d.position = {2, 0, 0};
    d.velocity = {};
    BodyId b = w.createBody(d);
    for (int i = 0; i < 60; i++) w.step(1.0f / 60.0f);
    // Equal masses: velocities swap.
    CHECK_NEAR(w.velocity(a).x, 0.0f, 0.1f);
    CHECK_NEAR(w.velocity(b).x, 4.0f, 0.1f);

    // Mass ratio: a heavy body barely slows when hitting a light one.
    PhysicsWorld w2;
    w2.gravity = {};
    BodyDesc h = d;
    h.mass = 100.0f;
    h.position = {-2, 0, 0};
    h.velocity = {4, 0, 0};
    BodyId heavy = w2.createBody(h);
    BodyDesc l = d;
    l.mass = 1.0f;
    l.position = {2, 0, 0};
    l.velocity = {};
    BodyId light = w2.createBody(l);
    for (int i = 0; i < 60; i++) w2.step(1.0f / 60.0f);
    CHECK(w2.velocity(heavy).x > 3.8f);
    CHECK(w2.velocity(light).x > 7.0f);
    // Momentum conserved.
    CHECK_NEAR(100.0f * w2.velocity(heavy).x + w2.velocity(light).x, 400.0f, 2.0f);
}

static void testEvents() {
    PhysicsWorld w;
    makeFloor(w);
    BodyDesc d;
    d.position = {0, 5, 0};
    d.restitution = 0.0f;
    BodyId ball = w.createBody(d);
    int impacts = 0;
    float speed = 0.0f;
    for (int i = 0; i < 60 * 4; i++) {
        w.step(1.0f / 60.0f);
        for (const CollisionEvent& e : w.takeEvents()) {
            if (e.a == ball || e.b == ball) {
                impacts++;
                speed = std::max(speed, e.impactSpeed);
            }
        }
    }
    // One real impact (~sqrt(2*9.81*4.5) = 9.4 m/s); resting contact doesn't spam events.
    CHECK(speed > 8.5f && speed < 10.5f);
    CHECK(impacts >= 1 && impacts <= 3);
    CHECK(w.takeEvents().empty());
}

static void testStaticAndKinematic() {
    PhysicsWorld w;
    BodyDesc s;
    s.type = BodyType::Static;
    s.shape = ShapeType::Box;
    s.position = {0, 10, 0};
    s.velocity = {5, 5, 5}; // ignored for static
    BodyId wall = w.createBody(s);
    for (int i = 0; i < 60; i++) w.step(1.0f / 60.0f);
    CHECK(w.position(wall) == Vector3(0, 10, 0));
    w.setVelocity(wall, {1, 0, 0});
    w.applyImpulse(wall, {100, 0, 0});
    CHECK(w.velocity(wall) == Vector3());

    // A kinematic box moves at constant velocity, ignoring gravity, and shoves a ball.
    PhysicsWorld k;
    k.gravity = {};
    BodyDesc kd;
    kd.type = BodyType::Kinematic;
    kd.shape = ShapeType::Box;
    kd.halfExtents = {0.5f, 2, 2};
    kd.position = {-3, 0, 0};
    kd.velocity = {3, 0, 0};
    BodyId plate = k.createBody(kd);
    BodyDesc bd;
    bd.position = {0, 0, 0};
    bd.restitution = 0.0f;
    BodyId ball = k.createBody(bd);
    for (int i = 0; i < 60 * 3; i++) k.step(1.0f / 60.0f);
    CHECK_NEAR(k.position(plate).x, -3.0f + 9.0f, 0.1f); // not slowed by the ball
    CHECK(k.position(ball).x > 4.5f);                    // pushed ahead of the plate
    CHECK(k.position(ball).x > k.position(plate).x);
}

static void testLayers() {
    PhysicsWorld w;
    BodyDesc f;
    f.type = BodyType::Static;
    f.shape = ShapeType::Box;
    f.halfExtents = {10, 0.5f, 10};
    f.position = {0, -0.5f, 0};
    f.layer = 2;
    f.mask = 2; // only collides with other layer-2 things
    w.createBody(f);

    BodyDesc d;
    d.position = {0, 3, 0};
    d.layer = 1; // doesn't match floor mask
    BodyId ghost = w.createBody(d);
    d.layer = 2;
    d.position = {5, 3, 0};
    BodyId solid = w.createBody(d);
    for (int i = 0; i < 60 * 3; i++) w.step(1.0f / 60.0f);
    CHECK(w.position(ghost).y < -5.0f);
    CHECK_NEAR(w.position(solid).y, 0.5f, 0.05f);
}

static void testBoxStack() {
    PhysicsWorld w;
    makeFloor(w);
    BodyDesc d;
    d.shape = ShapeType::Box;
    d.restitution = 0.0f;
    d.friction = 0.6f;
    BodyId boxes[3];
    for (int i = 0; i < 3; i++) {
        d.position = {0, 0.5f + i * 1.05f, 0};
        boxes[i] = w.createBody(d);
    }
    for (int i = 0; i < 60 * 5; i++) w.step(1.0f / 60.0f);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(w.position(boxes[i]).x, 0.0f, 0.05f);
        CHECK_NEAR(w.position(boxes[i]).y, 0.5f + i * 1.0f, 0.1f);
    }
}

static void testFriction() {
    PhysicsWorld w;
    makeFloor(w);
    BodyDesc d;
    d.shape = ShapeType::Box;
    d.position = {0, 0.5f, 0};
    d.velocity = {6, 0, 0};
    d.friction = 0.5f;
    d.linearDamping = 0.0f;
    BodyId slider = w.createBody(d);
    d.friction = 0.0f;
    d.position = {0, 0.5f, 10};
    BodyId ice = w.createBody(d);
    for (int i = 0; i < 60 * 3; i++) w.step(1.0f / 60.0f);
    // mu = sqrt(0.5 * 0.4 default floor) ~ 0.45: stops in v/(mu g) ~ 1.4 s.
    CHECK_NEAR(w.velocity(slider).x, 0.0f, 0.2f);
    CHECK(w.velocity(ice).x > 5.5f);
}

static void testNoTunneling() {
    PhysicsWorld w;
    w.gravity = {};
    BodyDesc wall;
    wall.type = BodyType::Static;
    wall.shape = ShapeType::Box;
    wall.halfExtents = {0.1f, 5, 5};
    wall.position = {10, 0, 0};
    w.createBody(wall);
    BodyDesc d;
    d.radius = 0.1f;
    d.velocity = {300, 0, 0}; // 2.5 m per fixed step, far thicker than the wall
    d.restitution = 0.0f;
    BodyId bullet = w.createBody(d);
    for (int i = 0; i < 60; i++) w.step(1.0f / 60.0f);
    CHECK(w.position(bullet).x < 10.0f);
}

static void testQueries() {
    PhysicsWorld w;
    w.gravity = {};
    BodyDesc d;
    d.position = {0, 0, -10};
    d.radius = 1;
    d.layer = 1;
    BodyId near = w.createBody(d);
    d.position = {0, 0, -20};
    BodyId far = w.createBody(d);
    d.shape = ShapeType::Box;
    d.halfExtents = {1, 1, 1};
    d.position = {5, 0, 0};
    d.layer = 4;
    BodyId box = w.createBody(d);

    RaycastHit hit;
    CHECK(w.raycast(Ray{{0, 0, 0}, {0, 0, -1}}, 100.0f, hit));
    CHECK(hit.body == near);
    CHECK_NEAR(hit.distance, 9.0f, 1e-4f);
    CHECK(!w.raycast(Ray{{0, 0, 0}, {0, 0, -1}}, 5.0f, hit));
    CHECK(w.raycast(Ray{{0, 0, 0}, {1, 0, 0}}, 100.0f, hit) && hit.body == box);
    CHECK(!w.raycast(Ray{{0, 0, 0}, {1, 0, 0}}, 100.0f, hit, 1)); // box is layer 4
    CHECK(w.raycast(Ray{{0, 0, -9.5f}, {0, 0, -1}}, 100.0f, hit) == false || hit.body == far);

    auto found = w.overlapSphere({0, 0, -10}, 2.0f);
    CHECK(found.size() == 1 && found[0] == near);
    CHECK(w.overlapSphere({5, 0, 0}, 0.5f).size() == 1);
    CHECK(w.overlapSphere({100, 0, 0}, 1.0f).empty());
    CHECK(w.overlapSphere({5, 0, 0}, 0.5f, 1).empty());
}

static void testLifecycleAndForces() {
    PhysicsWorld w;
    w.gravity = {};
    BodyDesc d;
    d.userData = 42;
    d.linearDamping = 0.0f;
    BodyId a = w.createBody(d);
    CHECK(a != INVALID_BODY && w.valid(a) && w.bodyCount() == 1);
    CHECK(w.userData(a) == 42);

    w.applyImpulse(a, {2, 0, 0}); // mass 1 -> v = 2
    CHECK_NEAR(w.velocity(a).x, 2.0f, 1e-6f);
    w.setVelocity(a, {});
    w.applyForce(a, {10, 0, 0});
    w.step(1.0f / 60.0f);
    CHECK_NEAR(w.velocity(a).x, 10.0f / 60.0f, 0.01f); // F*dt/m
    for (int i = 0; i < 59; i++) w.step(1.0f / 60.0f); // force was for one step only
    CHECK_NEAR(w.velocity(a).x, 10.0f / 60.0f, 0.01f);
    for (int i = 0; i < 60; i++) {                     // reapply each frame for a steady push
        w.applyForce(a, {10, 0, 0});
        w.step(1.0f / 60.0f);
    }
    CHECK_NEAR(w.velocity(a).x, 10.0f / 60.0f + 10.0f, 0.3f);

    w.setPosition(a, {1, 2, 3});
    CHECK(w.position(a) == Vector3(1, 2, 3));

    w.destroyBody(a);
    CHECK(!w.valid(a) && w.bodyCount() == 0);
    CHECK(w.position(a) == Vector3()); // stale id is inert
    w.setPosition(a, {9, 9, 9});
    w.destroyBody(a); // double destroy is a no-op
    BodyId b = w.createBody(d); // recycles the slot with a new generation
    CHECK(b != a && w.valid(b) && !w.valid(a));
    CHECK(w.position(b) == Vector3());
    CHECK(!w.valid(INVALID_BODY));
}

int main() {
    testOverlap();
    testContacts();
    testRaycastBox();
    testRaycastSphereAndTriangle();
    testFreeFall();
    testRestsOnFloor();
    testBounce();
    testElasticCollision();
    testEvents();
    testStaticAndKinematic();
    testLayers();
    testBoxStack();
    testFriction();
    testNoTunneling();
    testQueries();
    testLifecycleAndForces();
    if (failures == 0) std::printf("physics_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
