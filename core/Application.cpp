#include <ion/core/Application.hpp>

#include <ion/core/Engine.hpp>

namespace ion {

Application::Application() = default;
Application::~Application() = default;

void Application::initialize() {}
void Application::fixedUpdate(float) {}
void Application::update(float) {}
void Application::render() {}
void Application::shutdown() {}

Window* Application::window() const {
    return engine_ ? &engine_->window() : nullptr;
}

void Application::run() {
    Engine engine(windowConfig_);
    engine.setFixedTimestep(fixedTimestep_);
    engine.setTargetFps(targetFps_);
    engine.setFixedUpdateCallback([this](float fixedDeltaTime) {
        fixedUpdate(fixedDeltaTime);
    });
    engine.setFrameCallback([this](float deltaTime) {
        update(deltaTime);
        render();
        return !quitRequested_;
    });

    if (!engine.initialize()) {
        return;
    }

    engine_ = &engine;
    initialize();
    engine.run();
    shutdown();
    engine_ = nullptr;
}

void Application::quit() {
    quitRequested_ = true;
}

} // namespace ion
