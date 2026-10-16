#pragma once

#include <ion/platform/Window.hpp>

namespace ion {

class Engine;

//
// Convenience base class: override the hooks and call run().
//
//   initialize()          once, after the window is created
//   fixedUpdate(dt)       at a constant rate (fixedTimestep_), for physics
//   update(dt)            once per frame
//   render()              once per frame, after update
//   shutdown()            once, before the window is destroyed
//
class Application {
public:
    Application();
    virtual ~Application();

    virtual void initialize();
    virtual void fixedUpdate(float fixedDeltaTime);
    virtual void update(float deltaTime);
    virtual void render();
    virtual void shutdown();

    // Requests the engine loop to stop after the current frame.
    void quit();

    void run();

protected:
    // Valid between initialize() and shutdown() (null otherwise).
    Engine* engine() const { return engine_; }
    Window* window() const;

    WindowConfig windowConfig_;
    float fixedTimestep_ = 1.0f / 60.0f; // seconds; <= 0 disables fixedUpdate
    float targetFps_ = 0.0f;             // 0 = uncapped
    bool quitRequested_ = false;

private:
    Engine* engine_ = nullptr;
};

} // namespace ion
