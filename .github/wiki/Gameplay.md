# Gameplay Systems

Modules for structuring a game beyond rendering: scenes, events, timing,
physics, data and tooling. All are covered by `systems_test`,
`physics_test` and `terminal_test`.

## Scenes — `ion/scene/Scene.hpp`

A `Scene` owns its own `Registry`, `SystemManager`, `EventBus` and
`Scheduler`, so leaving a scene cleans everything up with it. A
`SceneManager` keeps a stack of scenes.

```cpp
class Level : public ion::Scene {
public:
    Level() : Scene("level") {}
    void onEnter() override { /* spawn entities into registry() */ }
    void update(float dt) override {
        ion::Scene::update(dt);            // scheduler, systems, queued events
        if (won) manager()->replace(std::make_unique<Level>());
    }
};

ion::SceneManager scenes;
scenes.push(std::make_unique<Level>());
// each frame
scenes.update(dt);
scenes.render();
```

- `push`/`pop`/`replace` requested from inside a scene are applied at the
  start of the next `update`, so a scene is never destroyed mid-call.
- `isOverlay()` (e.g. a pause menu) lets the scenes below keep rendering;
  `updatesBelow()` lets them keep updating.
- Hooks: `onEnter`, `onExit`, `onPause`, `onResume`, `fixedUpdate`,
  `update`, `render`.

## Events — `ion/core/Events.hpp`

```cpp
struct PlayerDied { int lives; };
ion::EventBus bus;
auto handle = bus.subscribe<PlayerDied>([](const PlayerDied& e) { ... });
bus.publish(PlayerDied{2});    // immediately
bus.enqueue(PlayerDied{1});    // on the next dispatchQueued()
bus.unsubscribe(handle);
```

Handlers may subscribe, unsubscribe (even themselves) and publish while an
event is being delivered.

## Scheduler — `ion/core/Scheduler.hpp`

Game-time callbacks, so they respect pausing and time scale:

```cpp
scheduler.after(2.0f, [] { spawnBoss(); });
auto blink = scheduler.every(0.25f, [&] { visible = !visible; }, 8);
scheduler.update(dt);
```

## State machines — `ion/core/StateMachine.hpp`

```cpp
enum class Enemy { Idle, Chase };
ion::StateMachine<Enemy> fsm;
fsm.addState(Enemy::Idle, {.onUpdate = [&](float) { if (seesPlayer()) fsm.change(Enemy::Chase); }});
fsm.addState(Enemy::Chase, {.onEnter = [&] { growl(); }});
fsm.start(Enemy::Idle);
fsm.update(dt);
```

Transitions requested inside a hook take effect after that hook returns.

## Tweens and easing — `ion/core/Tween.hpp`

31 easing curves (`Ease::QuadOut`, `Ease::BackOut`, `Ease::BounceOut`, ...)
and a `Tween<T>` for `float`, `Vector2/3/4`, `Color` and `Quaternion` (slerp).

```cpp
ion::Tween<ion::Vector2> slide(start, end, 0.4f, ion::Ease::BackOut);
slide.setDelay(0.1f).setRepeat(-1).setYoyo(true);
position = slide.update(dt);
```

## Physics — `ion/physics/Physics2D.hpp`

Arcade-style rigid bodies: axis-aligned boxes and circles (no rotation),
gravity, impulse response with restitution and friction, kinematic bodies,
trigger volumes and collision layers.

```cpp
ion::PhysicsWorld2D world;
world.setGravity({0, -980});
world.createBody(ion::BodyDef::box({0, -10}, {500, 10}, ion::BodyType::Static));
ion::BodyId ball = world.createBody(ion::BodyDef::circle({0, 200}, 16));
world.setContactCallback([](const ion::Contact2D& c) { ... });
world.step(1.0f / 60.0f);          // use a fixed timestep
bool onGround = world.body(ball)->grounded;
```

Also: `raycast`, `queryRect`, `queryPoint`, `applyImpulse`, `applyForce`.
For tile-based games, `Tilemap::moveAndSlide` resolves a box against solid
tiles directly.

## JSON — `ion/core/Json.hpp`

Parse, build and write JSON (save games, level files, settings):

```cpp
ion::JsonValue save = ion::JsonValue::makeObject();
save.set("level", 3).set("name", "Ada");
save.save(ion::fs::joinPath(ion::fs::userDataDirectory("MyGame"), "save.json"));

ion::JsonValue loaded;
std::string error;
if (ion::JsonValue::load(path, loaded, &error)) {
    int level = (int)loaded.getNumber("level", 1);
}
```

## Files — `ion/core/FileSystem.hpp`

`ion::fs::readTextFile`, `writeTextFile` (atomic: never leaves a truncated
file), `listDirectory`, `createDirectories`, path helpers (`joinPath`,
`extension`, `stem`, ...) and `userDataDirectory(app)` for saves.

## Assets and hot reload — `ion/core/AssetCache.hpp`

```cpp
ion::AssetCache<MyLevel> levels([](const std::string& path) { return loadLevel(path); });
std::shared_ptr<MyLevel> level = levels.get("levels/1.json");   // loaded once, shared
levels.reloadChanged();  // in development: picks up edited files in place
```

## Object pools — `ion/core/ObjectPool.hpp`

Fixed-capacity storage with stable addresses for bullets, particles, etc.:
`acquire(args...)`, `release(ptr)`, `forEach(fn)`.

## Profiler — `ion/core/Profiler.hpp`

```cpp
{ ION_PROFILE_SCOPE("physics"); world.step(dt); }
ion::Profiler::endFrame();
overlay.setLine("physics ms", std::to_string(ion::Profiler::find("physics")->lastFrameMs));
std::puts(ion::Profiler::report().c_str());
```

## Terminal games — `ion/terminal/Terminal.hpp`

Draw text-mode games with 24-bit color; `present()` only rewrites the
cells that changed. `TerminalInput` reads keys (arrows, function keys,
Ctrl/Alt) in raw mode. See `examples/terminal` (Snake).

```cpp
ion::TerminalCanvas canvas(80, 24);
ion::TerminalInput input;
canvas.drawBox(0, 0, 80, 24, ion::Color::gray(), ion::Color::black(), ion::BoxStyle::Rounded);
canvas.drawText(2, 1, "Score: 42", ion::Color::yellow());
canvas.present();
```
