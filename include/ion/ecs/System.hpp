#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace ion {

class Registry;

class System {
public:
    System() = default;
    virtual ~System() = default;

    virtual void update(float deltaTime) = 0;
};

// Owns systems and runs them in registration order.
class SystemManager {
public:
    template <typename T, typename... Args>
    T& add(Args&&... args) {
        static_assert(std::is_base_of_v<System, T>, "T must derive from ion::System");
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *system;
        systems_.push_back(std::move(system));
        return ref;
    }

    void update(float deltaTime) {
        for (auto& system : systems_) {
            system->update(deltaTime);
        }
    }

    size_t size() const { return systems_.size(); }
    void clear() { systems_.clear(); }

private:
    std::vector<std::unique_ptr<System>> systems_;
};

} // namespace ion
