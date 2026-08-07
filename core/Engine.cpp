#include <ion/core/Engine.hpp>

#include <ion/core/Log.hpp>
#include <ion/core/Timer.hpp>

#include <utility>

namespace ion {

class Engine::Impl {
public:
    explicit Impl(const WindowConfig& config) : window(config) {}

    Window window;
    Timer timer;
    FrameCallback frameCallback;
    bool running = false;
};

Engine::Engine() : impl(new Impl(WindowConfig{})) {
}

Engine::Engine(const WindowConfig& config) : impl(new Impl(config)) {
}

Engine::Engine(Engine&& other) noexcept : impl(other.impl) {
    other.impl = nullptr;
}

Engine& Engine::operator=(Engine&& other) noexcept {
    if (this != &other) {
        shutdown();
        delete impl;
        impl = other.impl;
        other.impl = nullptr;
    }
    return *this;
}

Engine::~Engine() {
    shutdown();
    delete impl;
}

bool Engine::initialize() {
    if (!impl) {
        return false;
    }
    if (impl->running) {
        return true;
    }
    if (!impl->window.create()) {
        ION_LOG_ERROR("Failed to create window");
        return false;
    }
    impl->timer.reset();
    impl->running = true;
    ION_LOG_INFO("Engine started (%ux%u)",
                 impl->window.width(), impl->window.height());
    return true;
}

void Engine::run() {
    if (!impl || !impl->running) {
        return;
    }
    while (impl->running && impl->window.isOpen()) {
        impl->window.pollEvents();
        float deltaTime = impl->timer.tick();
        if (impl->frameCallback) {
            impl->frameCallback(deltaTime);
        }
    }
}

void Engine::shutdown() {
    if (impl && impl->running) {
        impl->running = false;
        impl->window.destroy();
        ION_LOG_INFO("Engine stopped");
    }
}

bool Engine::isRunning() const {
    return impl && impl->running;
}

void Engine::setFrameCallback(FrameCallback callback) {
    if (impl) {
        impl->frameCallback = std::move(callback);
    }
}

Window& Engine::window() {
    return impl->window;
}

} // namespace ion
