#pragma once

#include <chrono>

namespace ion {

class Timer {
public:
    Timer() {
        reset();
    }

    void reset() {
        start_ = Clock::now();
        last_ = start_;
    }

    float tick() {
        TimePoint now = Clock::now();
        float delta = std::chrono::duration<float>(now - last_).count();
        last_ = now;
        return delta;
    }

    float deltaSeconds() const {
        return std::chrono::duration<float>(Clock::now() - last_).count();
    }

    double elapsedSeconds() const {
        return std::chrono::duration<double>(Clock::now() - start_).count();
    }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    TimePoint start_;
    TimePoint last_;
};

} // namespace ion
