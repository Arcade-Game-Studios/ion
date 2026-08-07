#pragma once

#include <ion/platform/Window.hpp>

#include <functional>

namespace ion {

class Engine {
public:
    using FrameCallback = std::function<void(float deltaTime)>;

    Engine();
    explicit Engine(const WindowConfig& config);
    Engine(Engine&& other) noexcept;
    Engine& operator=(Engine&& other) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    ~Engine();

    bool initialize();
    void run();
    void shutdown();

    bool isRunning() const;

    void setFrameCallback(FrameCallback callback);

    Window& window();

private:
    class Impl;
    Impl* impl;
};

} // namespace ion
