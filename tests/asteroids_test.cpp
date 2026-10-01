#include "../examples/asteroids/Game.hpp"

#include <cstdio>

static int failures = 0;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            failures++;                                                              \
        }                                                                            \
    } while (0)

using namespace asteroids;

static void clearRocks(Game& g) {
    std::vector<ion::EntityId> ids;
    g.registry().view<Asteroid>().each([&](ion::EntityId id, Asteroid&) { ids.push_back(id); });
    for (auto id : ids) g.registry().destroy(id);
}

static ion::EntityId placeRock(Game& g, int size, ion::Vector2 pos) {
    auto& r = g.registry();
    ion::EntityId e = r.create();
    r.emplace<Transform>(e, pos, 0.0f);
    r.emplace<Velocity>(e);
    r.emplace<Collider>(e, radiusForSize(size) * 0.9f);
    r.emplace<Asteroid>(e).size = size;
    return e;
}

static const ion::Vector2 MID(WORLD_W * 0.5f, WORLD_H * 0.5f);

static void testStart() {
    Game g(1);
    CHECK(g.lives() == 3 && g.score() == 0 && g.wave() == 1);
    CHECK(g.state() == State::Playing);
    CHECK(g.countAsteroids() == 4);
    CHECK(g.registry().valid(g.player()));
}

static void testShootSplitsAndScores() {
    Game g(2);
    clearRocks(g);
    // Ship points up (+y); put a large rock directly ahead. A second rock far
    // away keeps the wave from completing.
    placeRock(g, 3, MID + ion::Vector2(0, 120));
    placeRock(g, 1, ion::Vector2(50, 50));

    Input fire;
    fire.fire = true;
    for (int i = 0; i < 60 && g.score() == 0; ++i) g.update(1.0f / 60.0f, fire);
    CHECK(g.score() == scoreForSize(3));
    CHECK(g.countAsteroids() == 3); // 2 mediums + the far small rock
    CHECK(!g.takeExplosions().empty());
}

static void testSmallRockDisappears() {
    Game g(3);
    clearRocks(g);
    placeRock(g, 1, MID + ion::Vector2(0, 100));
    placeRock(g, 1, ion::Vector2(50, 50));
    Input fire;
    fire.fire = true;
    for (int i = 0; i < 60 && g.score() == 0; ++i) g.update(1.0f / 60.0f, fire);
    CHECK(g.score() == scoreForSize(1));
    CHECK(g.countAsteroids() == 1);
}

static void testShipCollisionAndInvulnerability() {
    Game g(4);
    clearRocks(g);
    placeRock(g, 3, MID);

    g.update(0.1f, {}); // fresh ship is invulnerable
    CHECK(g.lives() == 3);

    for (int i = 0; i < 180; ++i) g.update(1.0f / 60.0f, {});
    CHECK(g.lives() == 2);
    CHECK(g.registry().valid(g.player())); // respawned
}

static void testGameOverAndRestart() {
    Game g(5);
    for (int life = 0; life < 3; ++life) {
        clearRocks(g);
        placeRock(g, 1, ion::Vector2(1000, 600)); // static; keeps the wave alive
        for (int i = 0; i < 150; ++i) g.update(1.0f / 60.0f, {}); // outlast invulnerability
        placeRock(g, 3, g.registry().get<Transform>(g.player()).pos);
        g.update(1.0f / 60.0f, {});
    }
    CHECK(g.lives() == 0);
    CHECK(g.state() == State::GameOver);
    CHECK(!g.registry().valid(g.player()));

    Input restart;
    restart.restart = true;
    g.update(1.0f / 60.0f, restart);
    CHECK(g.state() == State::Playing && g.lives() == 3 && g.score() == 0);
}

static void testWaveAdvances() {
    Game g(6);
    clearRocks(g);
    g.update(1.0f / 60.0f, {});
    CHECK(g.wave() == 2);
    CHECK(g.countAsteroids() == 5);
}

static void testWrapAndBulletExpiry() {
    Game g(7);
    clearRocks(g);
    placeRock(g, 1, ion::Vector2(50, 50)); // keep the wave alive
    auto& r = g.registry();
    r.get<Transform>(g.player()).pos = {1.0f, 1.0f};
    r.get<Velocity>(g.player()).v = {-300.0f, 0.0f};
    g.update(0.1f, {});
    CHECK(r.get<Transform>(g.player()).pos.x > WORLD_W - 50.0f);

    Input fire;
    fire.fire = true;
    g.update(0.016f, fire);
    int bullets = 0;
    r.view<Bullet>().each([&](ion::EntityId, Bullet&) { ++bullets; });
    CHECK(bullets == 1);
    for (int i = 0; i < 90; ++i) g.update(1.0f / 60.0f, {});
    bullets = 0;
    r.view<Bullet>().each([&](ion::EntityId, Bullet&) { ++bullets; });
    CHECK(bullets == 0);
}

int main() {
    testStart();
    testShootSplitsAndScores();
    testSmallRockDisappears();
    testShipCollisionAndInvulnerability();
    testGameOverAndRestart();
    testWaveAdvances();
    testWrapAndBulletExpiry();
    if (failures == 0) std::printf("asteroids_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
