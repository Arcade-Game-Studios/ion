#include <ion/core/Application.hpp>

#include <ion/platform/Window.hpp>

#include <chrono>

namespace ion {

Application::Application() = default;
Application::~Application() = default;

void Application::initialize() {}
void Application::update(float) {}
void Application::render() {}
void Application::shutdown() {}

void Application::run() {
    Window window;
    if (!window.create()) {
        return;
    }

    initialize();

    auto last = std::chrono::steady_clock::now();
    while (window.isOpen()) {
        window.pollEvents();

        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - last).count();
        last = now;

        update(deltaTime);
        render();
    }

    shutdown();
    window.destroy();
}

} // namespace ion
