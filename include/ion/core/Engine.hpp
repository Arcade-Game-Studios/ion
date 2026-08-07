#pragma once

namespace ion {

class Engine {
public:
    Engine() = default;
    ~Engine() = default;

    bool initialize();
    void shutdown();

    bool isRunning() const;
};

} // namespace ion
