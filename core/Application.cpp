#include <ion/core/Application.hpp>

#include <ion/core/Engine.hpp>

namespace ion {

Application::Application() = default;
Application::~Application() = default;

void Application::initialize() {}
void Application::update(float) {}
void Application::render() {}
void Application::shutdown() {}

void Application::run() {
    Engine engine(windowConfig_);
    engine.setFrameCallback([this](float deltaTime) {
        update(deltaTime);
        render();
    });

    if (!engine.initialize()) {
        return;
    }

    initialize();
    engine.run();
    shutdown();
}

} // namespace ion
