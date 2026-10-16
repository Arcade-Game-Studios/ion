// Tests for the gameplay-support modules: events, tweens, scheduler, state
// machine, object pool, JSON, file system, profiler, asset cache, scenes and
// the ECS additions.

#include <ion/core/AssetCache.hpp>
#include <ion/core/Events.hpp>
#include <ion/core/FileSystem.hpp>
#include <ion/core/Json.hpp>
#include <ion/core/ObjectPool.hpp>
#include <ion/core/Profiler.hpp>
#include <ion/core/Scheduler.hpp>
#include <ion/core/StateMachine.hpp>
#include <ion/core/Tween.hpp>
#include <ion/ecs/Registry.hpp>
#include <ion/ecs/System.hpp>
#include <ion/scene/Scene.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
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
        double _a = (a), _b = (b);                                                   \
        if (!(std::fabs(_a - _b) <= (eps))) {                                        \
            std::printf("FAIL %s:%d: %s = %f, expected %f\n", __FILE__, __LINE__,    \
                        #a, _a, _b);                                                 \
            failures++;                                                              \
        }                                                                            \
    } while (0)

using namespace ion;

// --- Events -----------------------------------------------------------------

struct Damage {
    int amount;
};
struct Healed {
    int amount;
};

static void testEvents() {
    EventBus bus;
    int total = 0;
    EventBus::Handle a = bus.subscribe<Damage>([&](const Damage& d) { total += d.amount; });
    bus.subscribe<Damage>([&](const Damage& d) { total += d.amount * 10; });
    int healed = 0;
    bus.subscribe<Healed>([&](const Healed& h) { healed += h.amount; });
    bus.publish(Damage{1});
    CHECK(total == 11);
    CHECK(healed == 0);
    CHECK(bus.subscriberCount<Damage>() == 2);

    CHECK(bus.unsubscribe(a));
    CHECK(!bus.unsubscribe(a));
    bus.publish(Damage{1});
    CHECK(total == 21);

    // Deferred delivery.
    bus.enqueue(Healed{5});
    bus.enqueue(Healed{6});
    CHECK(healed == 0);
    CHECK(bus.queuedCount() == 2);
    CHECK(bus.dispatchQueued() == 2);
    CHECK(healed == 11);

    // A handler that unsubscribes itself and subscribes another mid-publish.
    EventBus reentrant;
    int calls = 0, lateCalls = 0;
    EventBus::Handle self = 0;
    self = reentrant.subscribe<Damage>([&](const Damage&) {
        calls++;
        reentrant.unsubscribe(self);
        reentrant.subscribe<Damage>([&](const Damage&) { lateCalls++; });
    });
    reentrant.publish(Damage{1});
    CHECK(calls == 1);
    CHECK(lateCalls == 0); // added during the publish: sees later events only
    reentrant.publish(Damage{1});
    CHECK(calls == 1);
    CHECK(lateCalls == 1);

    // Queued events published while dispatching are delivered too.
    int chain = 0;
    EventBus chained;
    chained.subscribe<Healed>([&](const Healed& h) {
        chain++;
        if (h.amount > 0) {
            chained.enqueue(Healed{h.amount - 1});
        }
    });
    chained.enqueue(Healed{3});
    CHECK(chained.dispatchQueued() == 4);
    CHECK(chain == 4);
    bus.clear();
    CHECK(bus.subscriberCount<Damage>() == 0);
}

// --- Tween ------------------------------------------------------------------

static void testEasing() {
    for (int i = 0; i < (int)Ease::Count; i++) {
        Ease e = (Ease)i;
        CHECK_NEAR(ease(e, 0.0f), 0.0, 1e-5);
        CHECK_NEAR(ease(e, 1.0f), 1.0, 1e-5);
        CHECK(std::string(easeName(e)) != "Unknown");
    }
    CHECK_NEAR(ease(Ease::Linear, 0.3f), 0.3, 1e-6);
    CHECK_NEAR(ease(Ease::QuadIn, 0.5f), 0.25, 1e-6);
    CHECK_NEAR(ease(Ease::QuadOut, 0.5f), 0.75, 1e-6);
    CHECK_NEAR(ease(Ease::SineInOut, 0.5f), 0.5, 1e-6);
    CHECK(ease(Ease::BackIn, 0.2f) < 0.0f);   // anticipation dips below 0
    CHECK(ease(Ease::BackOut, 0.8f) > 1.0f);  // overshoot
    CHECK(ease(Ease::Linear, -1.0f) == 0.0f); // clamped input
}

static void testTween() {
    bool done = false;
    Tween<float> t(0.0f, 10.0f, 1.0f);
    t.onComplete([&] { done = true; });
    CHECK_NEAR(t.update(0.25f), 2.5, 1e-5);
    CHECK(!t.isFinished());
    CHECK_NEAR(t.update(1.0f), 10.0, 1e-5);
    CHECK(t.isFinished());
    CHECK(done);

    Tween<Vector2> delayed(Vector2(0, 0), Vector2(4, 8), 1.0f);
    delayed.setDelay(0.5f);
    CHECK(delayed.update(0.25f) == Vector2(0, 0));
    CHECK(delayed.update(0.5f) == Vector2(1, 2)); // 0.25s into the tween

    Tween<float> yoyo(0.0f, 1.0f, 1.0f);
    yoyo.setRepeat(1).setYoyo(true);
    CHECK_NEAR(yoyo.update(0.5f), 0.5, 1e-5);
    CHECK_NEAR(yoyo.update(1.0f), 0.5, 1e-5); // on the way back
    CHECK(!yoyo.isFinished());
    CHECK_NEAR(yoyo.update(1.0f), 0.0, 1e-5);
    CHECK(yoyo.isFinished());

    Tween<float> forever(0.0f, 1.0f, 0.1f);
    forever.setRepeat(-1);
    for (int i = 0; i < 100; i++) {
        forever.update(0.037f);
    }
    CHECK(!forever.isFinished());
    forever.complete();
    CHECK(forever.isFinished());

    Tween<Color> color(Color::black(), Color::white(), 2.0f, Ease::Linear);
    color.pause();
    color.update(1.0f);
    CHECK(color.value() == Color::black());
    color.resume();
    CHECK_NEAR(color.update(1.0f).r, 0.5, 1e-5);

    Tween<float> instant(3.0f, 7.0f, 0.0f);
    CHECK(instant.update(0.0f) == 7.0f);
    CHECK(instant.isFinished());
}

// --- Scheduler ----------------------------------------------------------------

static void testScheduler() {
    Scheduler s;
    std::vector<std::string> log;
    s.after(1.0f, [&] { log.push_back("a"); });
    Scheduler::TaskId every = s.every(0.4f, [&] { log.push_back("e"); }, 3);
    CHECK(s.size() == 2);
    s.update(0.5f); // e at 0.4
    CHECK((log == std::vector<std::string>{"e"}));
    CHECK_NEAR(s.remaining(every), 0.3, 1e-5);
    s.update(0.6f); // e at 0.8, a at 1.0 (time order)
    CHECK((log == std::vector<std::string>{"e", "e", "a"}));
    s.update(1.0f); // last e at 1.2
    CHECK(log.size() == 4);
    CHECK(!s.isScheduled(every));
    CHECK(s.size() == 0);

    // A long frame fires a repeater once per elapsed interval.
    int ticks = 0;
    s.every(0.1f, [&] { ticks++; });
    s.update(0.55f);
    CHECK(ticks == 5);
    s.clear();

    // Cancel from inside a callback, schedule from inside a callback.
    int fired = 0;
    Scheduler::TaskId victim = s.after(0.5f, [&] { fired += 100; });
    s.after(0.2f, [&] {
        fired++;
        s.cancel(victim);
        s.after(0.0f, [&] { fired += 10; }); // runs next update
    });
    s.update(1.0f);
    CHECK(fired == 1);
    s.update(0.0f);
    CHECK(fired == 11);

    // Pause one task.
    int paused = 0;
    Scheduler::TaskId p = s.after(0.1f, [&] { paused++; });
    s.pause(p);
    s.update(1.0f);
    CHECK(paused == 0);
    s.resume(p);
    s.update(0.1f);
    CHECK(paused == 1);

    // Zero-interval repeaters fire once per update.
    int frames = 0;
    s.every(0.0f, [&] { frames++; });
    s.update(0.016f);
    s.update(0.016f);
    CHECK(frames == 2);
    CHECK(s.after(1.0f, {}) == Scheduler::INVALID_TASK);
}

// --- StateMachine -------------------------------------------------------------

enum class Mode { Idle, Run, Jump };

static void testStateMachine() {
    StateMachine<Mode> fsm;
    std::vector<std::string> log;
    fsm.addState(Mode::Idle, {[&] { log.push_back("enter idle"); },
                              [&](float) { fsm.change(Mode::Run); },
                              [&] { log.push_back("exit idle"); }});
    fsm.addState(Mode::Run, {[&] { log.push_back("enter run"); },
                             nullptr,
                             [&] { log.push_back("exit run"); }});
    fsm.addState(Mode::Jump, {[&] { fsm.change(Mode::Idle); }, nullptr, nullptr});
    std::vector<std::pair<Mode, Mode>> transitions;
    fsm.onTransition([&](Mode a, Mode b) { transitions.emplace_back(a, b); });
    CHECK(!fsm.isStarted());
    fsm.update(1.0f); // not started: nothing
    fsm.start(Mode::Idle);
    CHECK(fsm.is(Mode::Idle));
    fsm.update(0.5f); // requests Run from inside onUpdate; applied after
    CHECK(fsm.is(Mode::Run));
    CHECK(fsm.previous() == Mode::Idle);
    CHECK((log == std::vector<std::string>{"enter idle", "exit idle", "enter run"}));
    CHECK(fsm.timeInState() == 0.0f);
    fsm.update(0.25f);
    CHECK_NEAR(fsm.timeInState(), 0.25, 1e-6);
    // onEnter that immediately redirects.
    fsm.change(Mode::Jump);
    CHECK(fsm.is(Mode::Idle));
    CHECK(transitions.size() == 3);
    CHECK(fsm.hasState(Mode::Jump));
}

// --- ObjectPool ---------------------------------------------------------------

struct Tracked {
    static int live;
    int value;
    explicit Tracked(int v) : value(v) { live++; }
    ~Tracked() { live--; }
};
int Tracked::live = 0;

static void testObjectPool() {
    {
        ObjectPool<Tracked> pool(3);
        Tracked* a = pool.acquire(1);
        Tracked* b = pool.acquire(2);
        Tracked* c = pool.acquire(3);
        CHECK(a && b && c);
        CHECK(pool.full());
        CHECK(pool.acquire(4) == nullptr);
        CHECK(Tracked::live == 3);
        pool.release(b);
        pool.release(b); // double release ignored
        CHECK(Tracked::live == 2);
        Tracked outside(9);
        pool.release(&outside); // foreign pointer ignored
        Tracked* d = pool.acquire(5);
        CHECK(d == b); // slot reused, stable address
        int sum = 0;
        pool.forEach([&](Tracked& t) { sum += t.value; });
        CHECK(sum == 1 + 5 + 3);
        pool.forEach([&](Tracked& t) {
            if (t.value == 1) {
                pool.release(&t);
            }
        });
        CHECK(pool.size() == 2);
    }
    CHECK(Tracked::live == 0); // destructor releases the rest
}

// --- JSON -----------------------------------------------------------------------

static void testJson() {
    JsonValue doc = JsonValue::makeObject();
    doc.set("name", "Ada").set("level", 3).set("ratio", 0.1).set("alive", true);
    doc.set("tags", JsonValue::makeArray({"a", "b"}));
    doc.set("nothing", nullptr);
    doc.set("count", (size_t)7); // size_t works on every platform
    JsonValue nested;
    nested.set("x", -1.5f);
    nested.push(1); // ignored: nested is an object now
    doc.set("pos", nested);
    doc.set("level", 4); // replace keeps position

    std::string compact = doc.dump();
    CHECK(compact == "{\"name\":\"Ada\",\"level\":4,\"ratio\":0.1,\"alive\":true,"
                     "\"tags\":[\"a\",\"b\"],\"nothing\":null,\"count\":7,\"pos\":{\"x\":-1.5}}");
    JsonValue parsed;
    std::string error;
    CHECK(JsonValue::parse(doc.dump(2), parsed, &error));
    CHECK(parsed == doc);
    CHECK(parsed.getString("name") == "Ada");
    CHECK(parsed.getNumber("level") == 4);
    CHECK(parsed.getBool("alive"));
    CHECK(parsed.getNumber("missing", 9) == 9);
    CHECK(parsed.getString("level", "def") == "def"); // wrong type -> default
    CHECK(parsed.get("tags")->size() == 2);
    CHECK(parsed.get("tags")->at(1)->asString() == "b");
    CHECK(parsed.get("tags")->at(5) == nullptr);
    CHECK(parsed.remove("tags") && !parsed.has("tags"));

    // Escapes and unicode round trip.
    JsonValue text("line\n\"quoted\"\t\\ \x01 caf\xC3\xA9");
    JsonValue back;
    CHECK(JsonValue::parse(text.dump(), back) && back == text);
    CHECK(JsonValue::parse("\"\\u00e9\\ud83d\\ude00\"", back));
    CHECK(back.string == "\xC3\xA9\xF0\x9F\x98\x80");

    // Numbers round-trip exactly.
    for (double v : {0.1, 1e300, -2.5e-8, 123456789.0, 3.0}) {
        JsonValue n(v), m;
        CHECK(JsonValue::parse(n.dump(), m) && m.number == v);
    }
    CHECK(JsonValue(std::nan("")).dump() == "null");

    // Errors carry a position and leave output untouched.
    JsonValue untouched(5);
    CHECK(!JsonValue::parse("{\"a\":1,}", untouched, &error));
    CHECK(untouched.number == 5);
    CHECK(error.find("offset") != std::string::npos);
    CHECK(!JsonValue::parse("[1 2]", untouched));
    CHECK(!JsonValue::parse("01", untouched));
    CHECK(!JsonValue::parse("1.", untouched));
    CHECK(!JsonValue::parse("\"unterminated", untouched));
    CHECK(!JsonValue::parse("{} extra", untouched));
    CHECK(!JsonValue::parse("\"tab\tin string\"", untouched));
    CHECK(!JsonValue::parse(std::string(1000, '['), untouched)); // depth limit
    CHECK(JsonValue::parse("  [ ]  ", untouched) && untouched.isArray());

    const char* path = "/tmp/ion_json_test.json";
    CHECK(doc.save(path));
    JsonValue loaded;
    CHECK(JsonValue::load(path, loaded) && loaded == doc);
    std::remove(path);
    CHECK(!JsonValue::load("/nonexistent/x.json", loaded, &error));
}

// --- File system --------------------------------------------------------------

static void testFileSystem() {
    std::string dir = "/tmp/ion_fs_test/sub/dir";
    CHECK(fs::createDirectories(dir));
    CHECK(fs::isDirectory(dir));
    std::string file = fs::joinPath(dir, "save.dat");
    CHECK(fs::writeTextFile(file, "hello"));
    CHECK(fs::isFile(file));
    CHECK(fs::fileSize(file) == 5);
    CHECK(!fs::exists(file + ".tmp")); // atomic write cleaned up
    std::string text;
    CHECK(fs::readTextFile(file, text) && text == "hello");
    CHECK(fs::writeTextFile(file, "replaced"));
    CHECK(fs::readTextFile(file, text) && text == "replaced");
    uint8_t bytes[3] = {1, 2, 3};
    CHECK(fs::writeBinaryFile(fs::joinPath(dir, "b.BIN"), bytes, 3));
    std::vector<uint8_t> read;
    CHECK(fs::readBinaryFile(fs::joinPath(dir, "b.BIN"), read) && read.size() == 3 && read[2] == 3);
    CHECK((fs::listDirectory(dir) == std::vector<std::string>{"b.BIN", "save.dat"}));
    CHECK((fs::listDirectory(dir, ".bin") == std::vector<std::string>{"b.BIN"}));
    CHECK(fs::lastWriteTime(file) != 0);
    CHECK(fs::removeFile(file));
    CHECK(!fs::exists(file));
    CHECK(fs::fileSize(file) == -1);
    CHECK(!fs::readTextFile(file, text));

    CHECK(fs::fileName("a/b/c.png") == "c.png");
    CHECK(fs::stem("a/b/c.tar.gz") == "c.tar");
    CHECK(fs::extension("a/b/C.PNG") == ".png");
    CHECK(fs::extension("a/.hidden") == "");
    CHECK(fs::parentPath("a/b/c.png") == "a/b");
    CHECK(fs::parentPath("c.png") == "");
    CHECK(fs::joinPath("a/", "b") == "a/b");
    CHECK(fs::joinPath("", "b") == "b");
    CHECK(!fs::userDataDirectory("ion_fs_test_app").empty());
}

// --- Profiler -------------------------------------------------------------------

static void testProfiler() {
    Profiler::reset();
    for (int frame = 0; frame < 3; frame++) {
        {
            ION_PROFILE_SCOPE("outer");
            for (int i = 0; i < 2; i++) {
                ION_PROFILE_SCOPE("inner");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        Profiler::endFrame();
    }
    const Profiler::Section* outer = Profiler::find("outer");
    const Profiler::Section* inner = Profiler::find("inner");
    CHECK(outer && inner);
    if (outer && inner) {
        CHECK(inner->lastFrameCalls == 2);
        CHECK(outer->lastFrameMs >= inner->lastFrameMs);
        CHECK(inner->lastFrameMs >= 2.0);
        CHECK(outer->maxMs >= outer->lastFrameMs);
    }
    CHECK(Profiler::frameCount() == 3);
    std::string report = Profiler::report();
    CHECK(report.find("outer") != std::string::npos);
    CHECK(report.find("outer") < report.find("inner")); // slowest first
    Profiler::setEnabled(false);
    Profiler::record("ignored", 5.0);
    CHECK(Profiler::find("ignored") == nullptr);
    Profiler::setEnabled(true);
    Profiler::reset();
    CHECK(Profiler::sections().empty());
}

// --- AssetCache -----------------------------------------------------------------

static void testAssetCache() {
    std::string path = "/tmp/ion_asset_test.txt";
    fs::writeTextFile(path, "v1");
    int loads = 0;
    AssetCache<std::string> cache([&](const std::string& p) -> std::shared_ptr<std::string> {
        std::string text;
        if (!fs::readTextFile(p, text)) {
            return nullptr;
        }
        loads++;
        return std::make_shared<std::string>(text);
    });
    std::shared_ptr<std::string> a = cache.get(path);
    std::shared_ptr<std::string> b = cache.get(path);
    CHECK(a && a == b);
    CHECK(loads == 1);
    CHECK(*a == "v1");
    CHECK(cache.get("/tmp/ion_missing_asset.txt") == nullptr);
    CHECK(cache.size() == 1);

    // Hot reload: same handle, new contents.
    std::string reloadedPath;
    cache.onReload([&](const std::string& p, std::string&) { reloadedPath = p; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    fs::writeTextFile(path, "v2 longer");
    std::vector<std::string> changed = cache.reloadChanged();
    CHECK(changed.size() == 1);
    CHECK(*a == "v2 longer");
    CHECK(reloadedPath == path);
    CHECK(cache.reloadChanged().empty()); // nothing new

    cache.put("generated", std::make_shared<std::string>("made in code"));
    CHECK(*cache.get("generated") == "made in code");
    a.reset();
    b.reset();
    CHECK(cache.evictUnused() == 2);
    CHECK(cache.size() == 0);
    fs::removeFile(path);
}

// --- Scenes ---------------------------------------------------------------------

static std::vector<std::string> sceneLog;

class LoggingScene : public Scene {
public:
    LoggingScene(const std::string& name, bool overlay = false, bool updatesBelow = false)
        : Scene(name), overlay_(overlay), updatesBelow_(updatesBelow) {}
    void onEnter() override { sceneLog.push_back(name() + ".enter"); }
    void onExit() override { sceneLog.push_back(name() + ".exit"); }
    void onPause() override { sceneLog.push_back(name() + ".pause"); }
    void onResume() override { sceneLog.push_back(name() + ".resume"); }
    void update(float dt) override {
        sceneLog.push_back(name() + ".update");
        Scene::update(dt);
        if (next_) {
            manager()->replace(std::move(next_));
        }
    }
    void render() override { sceneLog.push_back(name() + ".render"); }
    bool isOverlay() const override { return overlay_; }
    bool updatesBelow() const override { return updatesBelow_; }

    std::unique_ptr<Scene> next_;

private:
    bool overlay_;
    bool updatesBelow_;
};

struct Counter {
    int value = 0;
};

class CountSystem : public System {
public:
    explicit CountSystem(Registry& r) : registry(r) {}
    void update(float) override {
        registry.view<Counter>().each([](EntityId, Counter& c) { c.value++; });
    }
    Registry& registry;
};

static void testScenes() {
    sceneLog.clear();
    SceneManager scenes;
    auto game = std::make_unique<LoggingScene>("game");
    LoggingScene* gamePtr = game.get();
    EntityId e = gamePtr->registry().create();
    gamePtr->registry().emplace<Counter>(e);
    gamePtr->systems().add<CountSystem>(gamePtr->registry());
    int timer = 0;
    gamePtr->scheduler().after(0.5f, [&] { timer++; });
    scenes.push(std::move(game));
    CHECK(scenes.top() == gamePtr);
    CHECK(gamePtr->manager() == &scenes);

    scenes.update(1.0f);
    CHECK(gamePtr->registry().get<Counter>(e).value == 1); // systems ran
    CHECK(timer == 1);                                     // scheduler ran

    scenes.push(std::make_unique<LoggingScene>("pause", /*overlay*/ true));
    sceneLog.clear();
    scenes.update(0.1f);
    scenes.render();
    CHECK((sceneLog == std::vector<std::string>{"pause.update", "game.render", "pause.render"}));
    CHECK(gamePtr->registry().get<Counter>(e).value == 1); // paused below

    // A scene that keeps the one below updating (e.g. a HUD).
    scenes.push(std::make_unique<LoggingScene>("hud", false, true));
    sceneLog.clear();
    scenes.update(0.1f);
    CHECK((sceneLog == std::vector<std::string>{"pause.update", "hud.update"}));
    scenes.pop();
    scenes.pop();
    CHECK(scenes.top() == gamePtr);

    // Replace requested from inside update is deferred to the next update.
    gamePtr->next_ = std::make_unique<LoggingScene>("level2");
    sceneLog.clear();
    scenes.update(0.1f);
    CHECK(scenes.top() == gamePtr); // still alive during its own update
    CHECK(scenes.hasPendingChanges());
    scenes.update(0.1f);
    CHECK(scenes.top()->name() == "level2");
    CHECK((sceneLog == std::vector<std::string>{"game.update", "game.exit", "level2.enter",
                                                "level2.update"}));
    scenes.clear();
    CHECK(scenes.empty());
    scenes.pop(); // popping an empty stack is harmless
}

// --- ECS additions ----------------------------------------------------------------

struct Position {
    float x = 0, y = 0;
};
struct Health {
    int hp = 100;
};

class OrderSystem : public System {
public:
    OrderSystem(std::vector<int>& log, int id) : log_(log), id_(id) {}
    void update(float) override { log_.push_back(id_); }
    void onAdded() override { log_.push_back(-id_); }

private:
    std::vector<int>& log_;
    int id_;
};

class OtherSystem : public System {
public:
    void update(float) override {}
};

static void testEcsAdditions() {
    Registry reg;
    // Parentheses when a constructor matches: five 3s, not {3, 5}.
    EntityId e = reg.create();
    reg.emplace<std::vector<int>>(e, (size_t)5, 3);
    CHECK(reg.get<std::vector<int>>(e).size() == 5);
    // Aggregates still work.
    reg.emplace<Position>(e, 1.0f, 2.0f);
    CHECK(reg.get<Position>(e).y == 2.0f);

    CHECK(reg.getOrEmplace<Health>(e).hp == 100);
    reg.get<Health>(e).hp = 5;
    CHECK(reg.getOrEmplace<Health>(e, 70).hp == 5); // existing kept
    CHECK(reg.patch<Health>(e, [](Health& h) { h.hp *= 2; }));
    CHECK(reg.get<Health>(e).hp == 10);
    EntityId bare = reg.create();
    CHECK(!reg.patch<Health>(bare, [](Health&) {}));
    const Registry& constReg = reg;
    CHECK(constReg.tryGet<Health>(e)->hp == 10);

    CHECK(reg.count<Position>() == 1);
    CHECK((reg.view<Position, Health>().count() == 1));
    CHECK((reg.view<Position, Health>().front() == e));
    CHECK(reg.view<Counter>().empty());
    CHECK(reg.view<Counter>().front() == INVALID_ENTITY);

    std::vector<EntityId> destroyed;
    reg.onDestroy([&](EntityId id) {
        destroyed.push_back(id);
        CHECK(reg.has<Position>(id) || id == bare); // components still readable
    });
    std::unordered_set<Entity> seen;
    reg.each([&](EntityId id) { seen.insert(Entity{id}); });
    CHECK(seen.size() == 2);
    reg.destroy(e);
    CHECK(destroyed.size() == 1 && destroyed[0] == e);
    int alive = 0;
    reg.each([&](EntityId) { alive++; });
    CHECK(alive == 1);

    reg.emplace<Position>(bare);
    reg.clearComponent<Position>();
    CHECK(reg.count<Position>() == 0);
    reg.clear();
    alive = 0;
    reg.each([&](EntityId) { alive++; });
    CHECK(alive == 0);

    // Systems: priority order, enable/disable, get/remove, lifecycle hooks.
    std::vector<int> log;
    SystemManager systems;
    systems.addWithPriority<OrderSystem>(10, log, 2);
    systems.addWithPriority<OrderSystem>(-5, log, 1);
    systems.add<OtherSystem>();
    CHECK((log == std::vector<int>({-2, -1})));
    log.clear();
    systems.update(0.0f);
    CHECK((log == std::vector<int>({1, 2})));
    systems.get<OrderSystem>()->setEnabled(false);
    log.clear();
    systems.update(0.0f);
    CHECK((log == std::vector<int>({2})));
    CHECK(systems.get<OtherSystem>() != nullptr);
    CHECK(systems.remove<OtherSystem>());
    CHECK(!systems.remove<OtherSystem>());
    CHECK(systems.size() == 2);
}

int main() {
    testEvents();
    testEasing();
    testTween();
    testScheduler();
    testStateMachine();
    testObjectPool();
    testJson();
    testFileSystem();
    testProfiler();
    testAssetCache();
    testScenes();
    testEcsAdditions();
    if (failures == 0) {
        std::printf("All systems tests passed\n");
        return 0;
    }
    std::printf("%d systems test(s) FAILED\n", failures);
    return 1;
}
