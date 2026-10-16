#include <ion/physics/Physics2D.hpp>

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
        if (!(std::fabs(_a - _b) <= (eps))) {                                        \
            std::printf("FAIL %s:%d: %s = %f, expected %f\n", __FILE__, __LINE__,    \
                        #a, _a, _b);                                                 \
            failures++;                                                              \
        }                                                                            \
    } while (0)

using namespace ion;

static void stepFor(PhysicsWorld2D& world, float seconds) {
    const int steps = (int)std::lround(seconds * 60.0f); // exact step count
    for (int i = 0; i < steps; i++) {
        world.step(1.0f / 60.0f);
    }
}

static void testFreeFall() {
    PhysicsWorld2D world;
    world.setGravity(Vector2(0.0f, -10.0f));
    BodyId ball = world.createBody(BodyDef::circle(Vector2(0, 100), 1.0f));
    stepFor(world, 1.0f);
    // Semi-implicit Euler: v = g t exactly, y close to 100 - g t^2 / 2.
    CHECK_NEAR(world.body(ball)->velocity.y, -10.0f, 0.2f);
    CHECK_NEAR(world.body(ball)->position.y, 95.0f, 0.2f);
    CHECK(!world.body(ball)->grounded);
}

static void testRestingOnGround() {
    PhysicsWorld2D world;
    world.setGravity(Vector2(0.0f, -100.0f));
    world.createBody(BodyDef::box(Vector2(0, -5), Vector2(50, 5), BodyType::Static));
    BodyId box = world.createBody(BodyDef::box(Vector2(0, 20), Vector2(1, 1)));
    BodyId ball = world.createBody(BodyDef::circle(Vector2(10, 30), 2.0f));
    stepFor(world, 3.0f);
    const Body* b = world.body(box);
    CHECK_NEAR(b->position.y, 1.0f, 0.05f); // resting on the ground top (y = 0)
    CHECK_NEAR(b->velocity.y, 0.0f, 0.5f);
    CHECK(b->grounded);
    CHECK_NEAR(world.body(ball)->position.y, 2.0f, 0.05f);
    CHECK(!world.contacts().empty());
}

static void testStacking() {
    PhysicsWorld2D world;
    world.setGravity(Vector2(0.0f, -100.0f));
    world.createBody(BodyDef::box(Vector2(0, -5), Vector2(50, 5), BodyType::Static));
    BodyId bottom = world.createBody(BodyDef::box(Vector2(0, 1), Vector2(1, 1)));
    BodyId top = world.createBody(BodyDef::box(Vector2(0, 4), Vector2(1, 1)));
    stepFor(world, 3.0f);
    CHECK_NEAR(world.body(bottom)->position.y, 1.0f, 0.1f);
    CHECK_NEAR(world.body(top)->position.y, 3.0f, 0.15f);
}

static void testBounceAndFriction() {
    PhysicsWorld2D world;
    world.setGravity(Vector2(0.0f, -100.0f));
    BodyDef groundDef = BodyDef::box(Vector2(0, -5), Vector2(500, 5), BodyType::Static);
    groundDef.friction = 1.0f;
    world.createBody(groundDef);
    BodyDef bouncyDef = BodyDef::circle(Vector2(0, 20), 1.0f);
    bouncyDef.restitution = 0.9f;
    BodyId bouncy = world.createBody(bouncyDef);
    float maxUpward = 0.0f;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; i++) {
        world.step(dt);
        maxUpward = std::fmax(maxUpward, world.body(bouncy)->velocity.y);
    }
    CHECK(maxUpward > 30.0f); // it bounced back up

    // A sliding box with friction slows down; a frictionless one doesn't.
    BodyDef sliderDef = BodyDef::box(Vector2(100, 1), Vector2(1, 1));
    sliderDef.velocity = Vector2(20.0f, 0.0f);
    sliderDef.friction = 1.0f;
    BodyId slider = world.createBody(sliderDef);
    sliderDef.position = Vector2(-100, 1);
    sliderDef.friction = 0.0f;
    BodyId ice = world.createBody(sliderDef);
    stepFor(world, 1.0f);
    CHECK(world.body(slider)->velocity.x < 1.0f);
    CHECK(world.body(ice)->velocity.x > 19.0f);
}

static void testTriggersLayersAndQueries() {
    PhysicsWorld2D world;
    world.setGravity(Vector2(0.0f, -100.0f));
    BodyDef zoneDef = BodyDef::box(Vector2(0, 0), Vector2(5, 5), BodyType::Static);
    zoneDef.isTrigger = true;
    BodyId zone = world.createBody(zoneDef);
    BodyId faller = world.createBody(BodyDef::circle(Vector2(0, 8), 1.0f));
    int triggerHits = 0;
    world.setContactCallback([&](const Contact2D& c) {
        if (c.trigger && (c.a == zone || c.b == zone)) {
            triggerHits++;
        }
    });
    stepFor(world, 1.0f);
    CHECK(triggerHits > 0);
    CHECK(world.body(faller)->position.y < -5.0f); // fell straight through

    // Layers: a body on layer 2 ignores a floor that only collides with 1.
    BodyDef floorDef = BodyDef::box(Vector2(50, -5), Vector2(10, 5), BodyType::Static);
    floorDef.mask = 1;
    world.createBody(floorDef);
    BodyDef ghostDef = BodyDef::circle(Vector2(50, 3), 1.0f);
    ghostDef.layer = 2;
    BodyId ghost = world.createBody(ghostDef);
    BodyId solid = world.createBody(BodyDef::circle(Vector2(55, 3), 1.0f));
    stepFor(world, 1.0f);
    CHECK(world.body(ghost)->position.y < -5.0f);
    CHECK_NEAR(world.body(solid)->position.y, 1.0f, 0.1f);

    PhysicsRayHit hit;
    CHECK(world.raycast(Vector2(55, 20), Vector2(0, -1), 100.0f, hit));
    CHECK(hit.body == solid);
    CHECK_NEAR(hit.distance, 18.0f, 0.15f);
    CHECK(!world.raycast(Vector2(55, 20), Vector2(0, 1), 100.0f, hit));
    CHECK(!world.raycast(Vector2(55, 20), Vector2(0, 0), 100.0f, hit));

    CHECK(world.queryPoint(Vector2(55, 1)).size() == 1);
    CHECK(world.queryRect(Rect(40, -10, 30, 5)).size() == 1); // the floor
    CHECK(world.queryRect(Rect(40, -10, 30, 5), 2).empty());

    world.destroyBody(solid);
    CHECK(world.body(solid) == nullptr);
    CHECK(world.body(ghost) != nullptr);
    world.clear();
    CHECK(world.bodyCount() == 0);
}

static void testKinematicAndImpulses() {
    PhysicsWorld2D world;
    world.setGravity(Vector2());
    BodyDef paddleDef = BodyDef::box(Vector2(0, 0), Vector2(1, 3), BodyType::Kinematic);
    paddleDef.velocity = Vector2(10.0f, 0.0f);
    BodyId paddle = world.createBody(paddleDef);
    BodyId puck = world.createBody(BodyDef::circle(Vector2(3, 0), 1.0f));
    stepFor(world, 0.5f);
    CHECK(world.body(puck)->velocity.x >= 9.9f); // pushed along
    CHECK_NEAR(world.body(paddle)->velocity.x, 10.0f, 1e-4f); // kinematic unaffected

    BodyId heavy = world.createBody(BodyDef::circle(Vector2(100, 100), 1.0f));
    world.body(heavy)->mass = 4.0f;
    world.applyImpulse(heavy, Vector2(8, 0));
    CHECK_NEAR(world.body(heavy)->velocity.x, 2.0f, 1e-5f);
    world.applyForce(heavy, Vector2(0, 40)); // 10 units/s^2 for one step
    world.step(0.1f);
    CHECK_NEAR(world.body(heavy)->velocity.y, 1.0f, 1e-4f);
    world.step(0.1f); // forces are cleared after each step
    CHECK_NEAR(world.body(heavy)->velocity.y, 1.0f, 1e-4f);
    world.setVelocity(heavy, Vector2());
    world.setPosition(heavy, Vector2(7, 7));
    CHECK(world.body(heavy)->position == Vector2(7, 7));
}

int main() {
    testFreeFall();
    testRestingOnGround();
    testStacking();
    testBounceAndFriction();
    testTriggersLayersAndQueries();
    testKinematicAndImpulses();
    if (failures == 0) {
        std::printf("All physics tests passed\n");
        return 0;
    }
    std::printf("%d physics test(s) FAILED\n", failures);
    return 1;
}
