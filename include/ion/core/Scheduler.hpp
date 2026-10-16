#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace ion {

//
// Game-time callbacks driven by update(deltaTime), so they respect pausing
// and time scale (unlike wall-clock timers):
//
//   Scheduler scheduler;
//   scheduler.after(2.0f, [] { spawnBoss(); });
//   auto blink = scheduler.every(0.25f, [&] { visible = !visible; });
//   scheduler.cancel(blink);
//   ...
//   scheduler.update(dt); // once per frame
//
// Callbacks may schedule or cancel other callbacks (or themselves).
//
class Scheduler {
public:
    using TaskId = uint64_t;
    static constexpr TaskId INVALID_TASK = 0;

    // Runs once after `delay` seconds.
    TaskId after(float delay, std::function<void()> callback);

    // Runs every `interval` seconds; repeats `count` times (-1 = forever).
    // The first call happens after one interval.
    TaskId every(float interval, std::function<void()> callback, int count = -1);

    bool cancel(TaskId id);
    bool isScheduled(TaskId id) const;
    // Seconds until the task next fires (0 if unknown).
    float remaining(TaskId id) const;

    // Pausing stops time for one task; update() still advances the others.
    void pause(TaskId id);
    void resume(TaskId id);

    // Advances time and fires due callbacks in time order. A repeating task
    // that fell behind (e.g. a long frame) fires once per elapsed interval.
    void update(float deltaTime);

    void clear();
    size_t size() const;

private:
    struct Task {
        TaskId id = INVALID_TASK;
        float remaining = 0.0f;
        float interval = 0.0f;
        int count = 1; // -1 = forever
        bool paused = false;
        bool cancelled = false;
        std::function<void()> callback;
    };

    std::vector<Task> tasks_;
    TaskId nextId_ = 1;
    bool updating_ = false;
};

} // namespace ion
