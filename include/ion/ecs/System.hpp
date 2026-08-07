#pragma once

namespace ion {

class System {
public:
    System() = default;
    virtual ~System() = default;

    virtual void update(float deltaTime) = 0;
};

} // namespace ion
