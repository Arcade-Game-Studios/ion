#pragma once

#include <ion/core/Events.hpp>
#include <ion/core/Scheduler.hpp>
#include <ion/ecs/Registry.hpp>
#include <ion/ecs/System.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ion {

class SceneManager;

//
// A game state (title screen, level, pause menu...). Each scene owns its own
// entity registry, systems, event bus and scheduler, so leaving a scene
// cleans everything up with it.
//
// The default update() advances the scheduler, then the systems, then
// delivers queued events. Override any hook; call Scene::update(dt) from an
// override to keep that behaviour.
//
class Scene {
public:
    explicit Scene(std::string name = "Scene") : name_(std::move(name)) {}
    virtual ~Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // Became the top scene (pushed, or the scene above it was popped).
    virtual void onEnter() {}
    // Stopped being the top scene because it was popped or replaced.
    virtual void onExit() {}
    // Another scene was pushed on top / that scene was popped again.
    virtual void onPause() {}
    virtual void onResume() {}

    virtual void fixedUpdate(float fixedDeltaTime) { (void)fixedDeltaTime; }
    virtual void update(float deltaTime) {
        scheduler_.update(deltaTime);
        systems_.update(deltaTime);
        events_.dispatchQueued();
    }
    virtual void render() {}

    // An overlay (pause menu, dialog) lets the scenes below keep rendering.
    virtual bool isOverlay() const { return false; }
    // Whether scenes below keep updating while this one is on top.
    virtual bool updatesBelow() const { return false; }

    const std::string& name() const { return name_; }
    Registry& registry() { return registry_; }
    SystemManager& systems() { return systems_; }
    EventBus& events() { return events_; }
    Scheduler& scheduler() { return scheduler_; }
    // The manager running this scene (null until pushed).
    SceneManager* manager() const { return manager_; }

private:
    friend class SceneManager;

    std::string name_;
    Registry registry_;
    SystemManager systems_;
    EventBus events_;
    Scheduler scheduler_;
    SceneManager* manager_ = nullptr;
};

//
// A stack of scenes. Changes requested during update/render (push, pop,
// replace) are deferred to the start of the next update so a scene is never
// destroyed while one of its own methods is running.
//
//   SceneManager scenes;
//   scenes.push(std::make_unique<TitleScene>());
//   // in the loop:
//   scenes.update(dt);
//   scenes.render();
//   // from inside TitleScene::update:
//   manager()->replace(std::make_unique<LevelScene>(1));
//
class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager();
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    void push(std::unique_ptr<Scene> scene);
    void pop();
    // Pops the top scene and pushes another (e.g. level -> next level).
    void replace(std::unique_ptr<Scene> scene);
    // Pops every scene.
    void clear();

    // Applies pending changes, then updates the top scene (and those below
    // it while each scene above reports updatesBelow()).
    void update(float deltaTime);
    void fixedUpdate(float fixedDeltaTime);
    // Renders bottom-up from the lowest scene still visible through
    // overlays.
    void render();

    // Applies pending push/pop/replace immediately. Called by update(); call
    // it yourself only from outside the scenes.
    void applyPending();

    Scene* top() const;
    size_t size() const { return stack_.size(); }
    bool empty() const { return stack_.empty(); }
    bool hasPendingChanges() const { return !pending_.empty(); }

private:
    enum class Op { Push, Pop, Replace, Clear };
    struct Change {
        Op op;
        std::unique_ptr<Scene> scene;
    };

    void popNow_();
    void pushNow_(std::unique_ptr<Scene> scene);

    std::vector<std::unique_ptr<Scene>> stack_;
    std::vector<Change> pending_;
    bool busy_ = false;
};

} // namespace ion
