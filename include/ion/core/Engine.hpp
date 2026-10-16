#pragma once

#include <ion/platform/Window.hpp>

#include <cstdint>
#include <functional>

namespace ion {

//
// Owns the window and drives the main loop. Each frame:
//
//   1. pumps window events
//   2. measures the frame delta (clamped to maxDeltaTime, scaled by
//      timeScale)
//   3. runs the fixed-update callback zero or more times at the fixed
//      timestep, if one is set
//   4. runs the frame callback
//   5. advances per-frame input state
//   6. sleeps to honor targetFps, if one is set
//
class Engine {
public:
    // Return false from the callback to stop the engine loop.
    using FrameCallback = std::function<bool(float deltaTime)>;
    using FixedUpdateCallback = std::function<void(float fixedDeltaTime)>;

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

    // Asks run() to return after the current frame.
    void requestQuit();

    bool isRunning() const;

    void setFrameCallback(FrameCallback callback);

    // Called at a constant rate (see setFixedTimestep) before the frame
    // callback; use it for physics and other simulation that must not depend
    // on the frame rate.
    void setFixedUpdateCallback(FixedUpdateCallback callback);

    // Fixed-update interval in seconds (default 1/60). Values <= 0 disable
    // fixed updates.
    void setFixedTimestep(float seconds);
    float fixedTimestep() const;

    // Fraction (0..1) of a fixed step left over after this frame's fixed
    // updates; use it to interpolate rendering between simulation states.
    float fixedInterpolation() const;

    // Upper bound on a single frame's delta (default 0.25 s), so a stall
    // (breakpoint, window drag, slow load) doesn't produce one huge step.
    void setMaxDeltaTime(float seconds);
    float maxDeltaTime() const;

    // Multiplies every delta passed to callbacks (0 pauses, 0.5 is slow
    // motion). Default 1.
    void setTimeScale(float scale);
    float timeScale() const;

    // Caps the frame rate by sleeping at the end of each frame. 0 (the
    // default) means uncapped.
    void setTargetFps(float fps);
    float targetFps() const;

    // Frames completed by run() since initialize().
    uint64_t frameCount() const;
    // Scaled time accumulated from frame deltas, in seconds.
    double elapsedTime() const;
    // Unscaled duration of the last frame, in seconds.
    float lastFrameTime() const;
    // Frames per second, smoothed over roughly the last second.
    float fps() const;

    Window& window();

private:
    class Impl;
    Impl* impl;
};

} // namespace ion
