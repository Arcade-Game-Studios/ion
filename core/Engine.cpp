#include <ion/core/Engine.hpp>

#include <ion/core/Log.hpp>
#include <ion/core/Timer.hpp>
#include <ion/platform/Input.hpp>

#include <cassert>
#include <chrono>
#include <thread>
#include <utility>

namespace ion {

class Engine::Impl {
public:
    explicit Impl(const WindowConfig& config) : window(config) {}

    Window window;
    Timer timer;
    FrameCallback frameCallback;
    FixedUpdateCallback fixedCallback;
    bool running = false;
    bool quitRequested = false;

    float fixedStep = 1.0f / 60.0f;
    float accumulator = 0.0f;
    float maxDelta = 0.25f;
    float timeScale = 1.0f;
    float targetFps = 0.0f;

    uint64_t frames = 0;
    double elapsed = 0.0;
    float lastFrame = 0.0f;
    float fps = 0.0f;
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
    impl->quitRequested = false;
    impl->accumulator = 0.0f;
    impl->frames = 0;
    impl->elapsed = 0.0;
    impl->fps = 0.0f;
    ION_LOG_INFO("Engine started (%ux%u)",
                 impl->window.width(), impl->window.height());
    return true;
}

void Engine::run() {
    if (!impl || !impl->running) {
        return;
    }
    using Clock = std::chrono::steady_clock;
    Clock::time_point nextFrame = Clock::now();

    while (impl->running && !impl->quitRequested && impl->window.isOpen()) {
        impl->window.pollEvents();

        float rawDelta = impl->timer.tick();
        impl->lastFrame = rawDelta;
        if (rawDelta > 0.0f) {
            float instant = 1.0f / rawDelta;
            // Exponential moving average with ~1 second of memory.
            float blend = rawDelta > 1.0f ? 1.0f : rawDelta;
            impl->fps = impl->fps <= 0.0f ? instant
                                          : impl->fps + (instant - impl->fps) * blend;
        }
        float delta = rawDelta;
        if (impl->maxDelta > 0.0f && delta > impl->maxDelta) {
            delta = impl->maxDelta;
        }
        delta *= impl->timeScale;
        impl->elapsed += delta;

        if (impl->fixedCallback && impl->fixedStep > 0.0f) {
            impl->accumulator += delta;
            while (impl->accumulator >= impl->fixedStep) {
                impl->fixedCallback(impl->fixedStep);
                impl->accumulator -= impl->fixedStep;
            }
        }

        bool keepGoing = true;
        if (impl->frameCallback) {
            keepGoing = impl->frameCallback(delta);
        }
        ion::input::update();
        impl->frames++;
        if (!keepGoing) {
            break;
        }

        if (impl->targetFps > 0.0f) {
            auto period = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(1.0 / impl->targetFps));
            nextFrame += period;
            Clock::time_point now = Clock::now();
            if (nextFrame > now) {
                std::this_thread::sleep_until(nextFrame);
            } else if (now - nextFrame > period * 4) {
                // Fell far behind (e.g. a hitch); don't try to catch up.
                nextFrame = now;
            }
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

void Engine::requestQuit() {
    if (impl) {
        impl->quitRequested = true;
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

void Engine::setFixedUpdateCallback(FixedUpdateCallback callback) {
    if (impl) {
        impl->fixedCallback = std::move(callback);
    }
}

void Engine::setFixedTimestep(float seconds) {
    if (impl) {
        impl->fixedStep = seconds;
        impl->accumulator = 0.0f;
    }
}

float Engine::fixedTimestep() const {
    return impl ? impl->fixedStep : 0.0f;
}

float Engine::fixedInterpolation() const {
    if (!impl || impl->fixedStep <= 0.0f) {
        return 0.0f;
    }
    return impl->accumulator / impl->fixedStep;
}

void Engine::setMaxDeltaTime(float seconds) {
    if (impl) {
        impl->maxDelta = seconds;
    }
}

float Engine::maxDeltaTime() const {
    return impl ? impl->maxDelta : 0.0f;
}

void Engine::setTimeScale(float scale) {
    if (impl) {
        impl->timeScale = scale < 0.0f ? 0.0f : scale;
    }
}

float Engine::timeScale() const {
    return impl ? impl->timeScale : 1.0f;
}

void Engine::setTargetFps(float fps) {
    if (impl) {
        impl->targetFps = fps < 0.0f ? 0.0f : fps;
    }
}

float Engine::targetFps() const {
    return impl ? impl->targetFps : 0.0f;
}

uint64_t Engine::frameCount() const {
    return impl ? impl->frames : 0;
}

double Engine::elapsedTime() const {
    return impl ? impl->elapsed : 0.0;
}

float Engine::lastFrameTime() const {
    return impl ? impl->lastFrame : 0.0f;
}

float Engine::fps() const {
    return impl ? impl->fps : 0.0f;
}

Window& Engine::window() {
    assert(impl && "Engine::window() on a moved-from Engine");
    return impl->window;
}

} // namespace ion
