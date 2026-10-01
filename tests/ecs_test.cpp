#include <ion/ecs/Registry.hpp>
#include <ion/ecs/System.hpp>

#include <cstdio>
#include <set>

static int failures = 0;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            failures++;                                                              \
        }                                                                            \
    } while (0)

struct Position { float x = 0, y = 0; };
struct Velocity { float dx = 0, dy = 0; };
struct Tag {};

static void testLifecycle() {
    ion::Registry reg;
    ion::EntityId a = reg.create();
    ion::EntityId b = reg.create();
    CHECK(a != ion::INVALID_ENTITY);
    CHECK(a != b);
    CHECK(reg.valid(a) && reg.valid(b));
    CHECK(reg.aliveCount() == 2);

    reg.destroy(a);
    CHECK(!reg.valid(a));
    CHECK(reg.aliveCount() == 1);
    reg.destroy(a); // double destroy is a no-op
    CHECK(reg.aliveCount() == 1);

    // Slot is recycled but the stale id must stay invalid.
    ion::EntityId c = reg.create();
    CHECK(ion::entityIndex(c) == ion::entityIndex(a));
    CHECK(c != a);
    CHECK(reg.valid(c));
    CHECK(!reg.valid(a));
    CHECK(!reg.valid(ion::INVALID_ENTITY));
}

static void testComponents() {
    ion::Registry reg;
    ion::EntityId e = reg.create();
    CHECK(!reg.has<Position>(e));
    CHECK(reg.tryGet<Position>(e) == nullptr);

    reg.emplace<Position>(e, 1.0f, 2.0f);
    CHECK(reg.has<Position>(e));
    CHECK(reg.get<Position>(e).x == 1.0f);
    CHECK(reg.get<Position>(e).y == 2.0f);

    reg.emplace<Position>(e, 5.0f, 6.0f); // replaces
    CHECK(reg.get<Position>(e).x == 5.0f);

    reg.get<Position>(e).x = 9.0f;
    CHECK(reg.tryGet<Position>(e)->x == 9.0f);

    reg.remove<Position>(e);
    CHECK(!reg.has<Position>(e));
    reg.remove<Velocity>(e); // never added: no-op

    // Stale ids see nothing, even after the slot is reused with components.
    reg.emplace<Position>(e, 1.0f, 1.0f);
    reg.destroy(e);
    ion::EntityId f = reg.create();
    reg.emplace<Position>(f, 3.0f, 3.0f);
    CHECK(!reg.has<Position>(e));
    CHECK(reg.tryGet<Position>(e) == nullptr);
    CHECK(reg.get<Position>(f).x == 3.0f);
}

static void testSwapRemoveKeepsOthersIntact() {
    ion::Registry reg;
    std::vector<ion::EntityId> ids;
    for (int i = 0; i < 10; ++i) {
        ids.push_back(reg.create());
        reg.emplace<Position>(ids.back(), (float)i, 0.0f);
    }
    reg.destroy(ids[0]);
    reg.destroy(ids[5]);
    reg.remove<Position>(ids[9]);
    for (int i = 1; i < 9; ++i) {
        if (i == 5) continue;
        CHECK(reg.has<Position>(ids[i]));
        CHECK(reg.get<Position>(ids[i]).x == (float)i);
    }
}

static void testView() {
    ion::Registry reg;
    ion::EntityId moving = reg.create();
    ion::EntityId still = reg.create();
    ion::EntityId velOnly = reg.create();
    reg.emplace<Position>(moving, 0.0f, 0.0f);
    reg.emplace<Velocity>(moving, 1.0f, 2.0f);
    reg.emplace<Position>(still, 7.0f, 7.0f);
    reg.emplace<Velocity>(velOnly, 1.0f, 1.0f);

    std::set<ion::EntityId> seen;
    reg.view<Position, Velocity>().each([&](ion::EntityId id, Position& p, Velocity& v) {
        seen.insert(id);
        p.x += v.dx;
        p.y += v.dy;
    });
    CHECK(seen.size() == 1 && seen.count(moving) == 1);
    CHECK(reg.get<Position>(moving).x == 1.0f);
    CHECK(reg.get<Position>(moving).y == 2.0f);
    CHECK(reg.get<Position>(still).x == 7.0f);

    // A component type that was never registered yields an empty view.
    int visited = 0;
    reg.view<Position, Tag>().each([&](ion::EntityId, Position&, Tag&) { ++visited; });
    CHECK(visited == 0);
}

static void testDestroyDuringIteration() {
    ion::Registry reg;
    for (int i = 0; i < 100; ++i) {
        ion::EntityId e = reg.create();
        reg.emplace<Position>(e, (float)i, 0.0f);
    }
    int visited = 0;
    reg.view<Position>().each([&](ion::EntityId id, Position& p) {
        ++visited;
        if (static_cast<int>(p.x) % 2 == 0) {
            reg.destroy(id);
        }
    });
    CHECK(visited == 100);
    CHECK(reg.aliveCount() == 50);
    int remaining = 0;
    reg.view<Position>().each([&](ion::EntityId, Position& p) {
        CHECK(static_cast<int>(p.x) % 2 == 1);
        ++remaining;
    });
    CHECK(remaining == 50);
}

static void testClear() {
    ion::Registry reg;
    ion::EntityId e = reg.create();
    reg.emplace<Position>(e, 1.0f, 1.0f);
    reg.clear();
    CHECK(!reg.valid(e));
    CHECK(reg.aliveCount() == 0);
    int visited = 0;
    reg.view<Position>().each([&](ion::EntityId, Position&) { ++visited; });
    CHECK(visited == 0);
    ion::EntityId n = reg.create();
    CHECK(reg.valid(n) && n != e);
}

struct Counter : ion::System {
    int* total;
    explicit Counter(int* t) : total(t) {}
    void update(float) override { ++*total; }
};

static void testSystems() {
    ion::SystemManager systems;
    int total = 0;
    systems.add<Counter>(&total);
    systems.add<Counter>(&total);
    systems.update(0.016f);
    CHECK(total == 2);
    CHECK(systems.size() == 2);
}

int main() {
    testLifecycle();
    testComponents();
    testSwapRemoveKeepsOthersIntact();
    testView();
    testDestroyDuringIteration();
    testClear();
    testSystems();
    if (failures == 0) {
        std::printf("ecs_test: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
