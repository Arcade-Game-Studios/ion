#include <ion/core/Scheduler.hpp>

#include <algorithm>

namespace ion {

Scheduler::TaskId Scheduler::after(float delay, std::function<void()> callback) {
    return every(delay, std::move(callback), 1);
}

Scheduler::TaskId Scheduler::every(float interval, std::function<void()> callback, int count) {
    if (!callback || count == 0) {
        return INVALID_TASK;
    }
    Task task;
    task.id = nextId_++;
    task.interval = std::max(0.0f, interval);
    task.remaining = task.interval;
    task.count = count;
    task.callback = std::move(callback);
    tasks_.push_back(std::move(task));
    return tasks_.back().id;
}

bool Scheduler::cancel(TaskId id) {
    for (Task& task : tasks_) {
        if (task.id == id && !task.cancelled) {
            task.cancelled = true;
            if (!updating_) {
                tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                                            [](const Task& t) { return t.cancelled; }),
                             tasks_.end());
            }
            return true;
        }
    }
    return false;
}

bool Scheduler::isScheduled(TaskId id) const {
    for (const Task& task : tasks_) {
        if (task.id == id) {
            return !task.cancelled;
        }
    }
    return false;
}

float Scheduler::remaining(TaskId id) const {
    for (const Task& task : tasks_) {
        if (task.id == id && !task.cancelled) {
            return std::max(0.0f, task.remaining);
        }
    }
    return 0.0f;
}

void Scheduler::pause(TaskId id) {
    for (Task& task : tasks_) {
        if (task.id == id) {
            task.paused = true;
        }
    }
}

void Scheduler::resume(TaskId id) {
    for (Task& task : tasks_) {
        if (task.id == id) {
            task.paused = false;
        }
    }
}

void Scheduler::update(float deltaTime) {
    if (deltaTime < 0.0f) {
        deltaTime = 0.0f;
    }
    updating_ = true;
    // Tasks added during this update start counting next frame.
    size_t count = tasks_.size();
    for (size_t i = 0; i < count; i++) {
        if (tasks_[i].cancelled || tasks_[i].paused) {
            continue;
        }
        tasks_[i].remaining -= deltaTime;
    }
    // Fire due tasks in time order (most overdue first), one occurrence at a
    // time, so interleaved timers keep their relative order. Zero-interval
    // repeating tasks fire at most once per update.
    std::vector<char> firedZeroInterval(count, 0);
    for (;;) {
        size_t due = count;
        float mostOverdue = 0.0f;
        for (size_t i = 0; i < count; i++) {
            const Task& t = tasks_[i];
            if (t.cancelled || t.paused || t.remaining > 0.0f || firedZeroInterval[i]) {
                continue;
            }
            if (due == count || t.remaining < mostOverdue) {
                due = i;
                mostOverdue = t.remaining;
            }
        }
        if (due == count) {
            break;
        }
        // Copy the callback: it may add tasks (reallocating tasks_).
        std::function<void()> callback = tasks_[due].callback;
        Task& task = tasks_[due];
        if (task.count > 0) {
            task.count--;
        }
        if (task.count == 0) {
            task.cancelled = true;
        } else if (task.interval > 0.0f) {
            task.remaining += task.interval;
        } else {
            task.remaining = 0.0f;
            firedZeroInterval[due] = 1;
        }
        callback();
    }
    updating_ = false;
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                                [](const Task& t) { return t.cancelled; }),
                 tasks_.end());
}

void Scheduler::clear() {
    if (updating_) {
        for (Task& task : tasks_) {
            task.cancelled = true;
        }
        return;
    }
    tasks_.clear();
}

size_t Scheduler::size() const {
    size_t n = 0;
    for (const Task& task : tasks_) {
        n += task.cancelled ? 0 : 1;
    }
    return n;
}

} // namespace ion
