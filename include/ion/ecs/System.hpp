#pragma once

#include <algorithm>
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

    // Called by SystemManager when the system is added / removed / the
    // manager is cleared.
    virtual void onAdded() {}
    virtual void onRemoved() {}

    // Disabled systems are skipped by SystemManager::update.
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

private:
    bool enabled_ = true;
};

// Owns systems and runs them in priority order (lower first); systems with
// equal priority run in registration order.
class SystemManager {
public:
    SystemManager() = default;
    SystemManager(const SystemManager&) = delete;
    SystemManager& operator=(const SystemManager&) = delete;
    SystemManager(SystemManager&&) = default;
    SystemManager& operator=(SystemManager&&) = default;
    ~SystemManager() { clear(); }

    template <typename T, typename... Args>
    T& add(Args&&... args) {
        return addWithPriority<T>(0, std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    T& addWithPriority(int priority, Args&&... args) {
        static_assert(std::is_base_of_v<System, T>, "T must derive from ion::System");
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *system;
        Entry entry{std::move(system), priority, nextOrder_++};
        auto pos = std::upper_bound(systems_.begin(), systems_.end(), entry,
                                    [](const Entry& a, const Entry& b) {
                                        return a.priority < b.priority ||
                                               (a.priority == b.priority && a.order < b.order);
                                    });
        systems_.insert(pos, std::move(entry));
        ref.onAdded();
        return ref;
    }

    // First system of exactly type T (or derived from it), or null.
    template <typename T>
    T* get() const {
        for (const Entry& entry : systems_) {
            if (T* system = dynamic_cast<T*>(entry.system.get())) {
                return system;
            }
        }
        return nullptr;
    }

    // Removes the first system of type T; returns whether one was found.
    template <typename T>
    bool remove() {
        for (auto it = systems_.begin(); it != systems_.end(); ++it) {
            if (dynamic_cast<T*>(it->system.get())) {
                it->system->onRemoved();
                systems_.erase(it);
                return true;
            }
        }
        return false;
    }

    void update(float deltaTime) {
        // Index loop: a system may add systems during update.
        for (size_t i = 0; i < systems_.size(); ++i) {
            System* system = systems_[i].system.get();
            if (system->isEnabled()) {
                system->update(deltaTime);
            }
        }
    }

    size_t size() const { return systems_.size(); }

    void clear() {
        for (Entry& entry : systems_) {
            entry.system->onRemoved();
        }
        systems_.clear();
    }

private:
    struct Entry {
        std::unique_ptr<System> system;
        int priority = 0;
        size_t order = 0;
    };
    std::vector<Entry> systems_;
    size_t nextOrder_ = 0;
};

} // namespace ion
