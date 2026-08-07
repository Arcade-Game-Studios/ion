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

    void run();

protected:
    WindowConfig windowConfig_;
};

} // namespace ion