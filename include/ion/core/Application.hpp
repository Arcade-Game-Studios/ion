#pragma once

#include <ion/platform/Window.hpp>

namespace ion {

class Application {
public:
    Application();
    virtual ~Application();

    virtual void initialize();
    virtual void update(float deltaTime);
    virtual void render();
    virtual void shutdown();

    // Requests the engine loop to stop after the current frame.
    void quit();

    void run();

protected:
    WindowConfig windowConfig_;
    bool quitRequested_ = false;
};

} // namespace ion