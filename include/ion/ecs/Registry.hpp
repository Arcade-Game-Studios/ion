#pragma once

#include <ion/ecs/Component.hpp>
#include <ion/ecs/Entity.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace ion {

class Registry;

// Iterates every entity that owns all of Ts.
//
// Iteration walks the smallest pool backwards, so it is safe to destroy the
// current entity (or remove its components) from inside the callback.
// Creating entities or adding components of type Ts... during iteration is
// not supported.
template <typename... Ts>
class View {
public:
    // fn(EntityId, Ts&...)
    template <typename Fn>
    void each(Fn&& fn) const;

    size_t sizeHint() const;

    // Exact number of entities owning all of Ts (iterates).
    size_t count() const;
    bool empty() const { return count() == 0; }

    // First matching entity, or INVALID_ENTITY. Handy for singletons such as
    // the player.
    EntityId front() const;

private:
    friend class Registry;
    explicit View(Registry* registry) : registry_(registry) {}
    Registry* registry_;
};

class Registry {
public:
    EntityId create() {
        uint32_t index;
        if (!freeList_.empty()) {
            index = freeList_.back();
            freeList_.pop_back();
            aliveFlags_[index] = 1;
        } else {
            index = static_cast<uint32_t>(generations_.size());
            generations_.push_back(1);
            aliveFlags_.push_back(1);
        }
        ++alive_;
        return makeEntityId(index, generations_[index]);
    }

    bool valid(EntityId id) const {
        uint32_t index = entityIndex(id);
        return id != INVALID_ENTITY && index < generations_.size() &&
               generations_[index] == entityGeneration(id);
    }

    // Destroys the entity and all its components. Stale ids become invalid.
    void destroy(EntityId id) {
        if (!valid(id)) {
            return;
        }
        for (auto& callback : onDestroy_) {
            callback(id); // components are still attached here
        }
        uint32_t index = entityIndex(id);
        for (auto& pool : pools_) {
            if (pool) {
                pool->remove(index);
            }
        }
        // Skip generation 0 on wraparound so ids never collide with INVALID_ENTITY.
        if (++generations_[index] == 0) {
            generations_[index] = 1;
        }
        freeList_.push_back(index);
        aliveFlags_[index] = 0;
        --alive_;
    }

    size_t aliveCount() const { return alive_; }

    // Calls fn(EntityId) for every live entity.
    template <typename Fn>
    void each(Fn&& fn) const {
        std::vector<EntityId> live;
        live.reserve(alive_);
        for (uint32_t i = 0; i < generations_.size(); ++i) {
            EntityId id = makeEntityId(i, generations_[i]);
            if (aliveFlags_[i]) {
                live.push_back(id);
            }
        }
        // Collected first so fn may create or destroy entities.
        for (EntityId id : live) {
            if (valid(id)) {
                fn(id);
            }
        }
    }

    // Registers a callback run just before any entity is destroyed (while
    // its components are still readable). Returns a handle for removal.
    size_t onDestroy(std::function<void(EntityId)> callback) {
        onDestroy_.push_back(std::move(callback));
        return onDestroy_.size() - 1;
    }

    void clearCallbacks() { onDestroy_.clear(); }

    // Adds (or replaces) a component, constructed from args.
    template <typename T, typename... Args>
    T& emplace(EntityId id, Args&&... args) {
        assert(valid(id));
        return pool<T>().emplace(entityIndex(id), id, std::forward<Args>(args)...);
    }

    template <typename T>
    bool has(EntityId id) const {
        const auto* p = findPool<T>();
        return valid(id) && p && p->contains(entityIndex(id));
    }

    // Asserts that the entity owns a T; use tryGet when unsure.
    template <typename T>
    T& get(EntityId id) {
        assert(valid(id));
        return pool<T>().get(entityIndex(id));
    }

    // Returns the existing component, or adds one constructed from args.
    template <typename T, typename... Args>
    T& getOrEmplace(EntityId id, Args&&... args) {
        if (T* existing = tryGet<T>(id)) {
            return *existing;
        }
        return emplace<T>(id, std::forward<Args>(args)...);
    }

    // Applies fn(T&) to the entity's component if present; returns whether
    // it was.
    template <typename T, typename Fn>
    bool patch(EntityId id, Fn&& fn) {
        if (T* component = tryGet<T>(id)) {
            fn(*component);
            return true;
        }
        return false;
    }

    // Number of entities with a T.
    template <typename T>
    size_t count() const {
        const auto* p = findPool<T>();
        return p ? p->size() : 0;
    }

    // Removes T from every entity.
    template <typename T>
    void clearComponent() {
        if (auto* p = findPool<T>()) {
            p->clear();
        }
    }

    template <typename T>
    const T* tryGet(EntityId id) const {
        if (!valid(id)) {
            return nullptr;
        }
        auto* p = findPool<T>();
        return p ? p->tryGet(entityIndex(id)) : nullptr;
    }

    template <typename T>
    T* tryGet(EntityId id) {
        if (!valid(id)) {
            return nullptr;
        }
        auto* p = findPool<T>();
        return p ? p->tryGet(entityIndex(id)) : nullptr;
    }

    template <typename T>
    void remove(EntityId id) {
        if (!valid(id)) {
            return;
        }
        if (auto* p = findPool<T>()) {
            p->remove(entityIndex(id));
        }
    }

    template <typename... Ts>
    View<Ts...> view() {
        static_assert(sizeof...(Ts) > 0, "view requires at least one component type");
        return View<Ts...>(this);
    }

    // Destroys every entity. Invalidates all outstanding ids. (onDestroy
    // callbacks are not run.)
    void clear() {
        for (auto& pool : pools_) {
            if (pool) {
                pool->clear();
            }
        }
        for (uint32_t i = 0; i < generations_.size(); ++i) {
            if (++generations_[i] == 0) {
                generations_[i] = 1;
            }
        }
        std::fill(aliveFlags_.begin(), aliveFlags_.end(), 0);
        freeList_.clear();
        for (uint32_t i = static_cast<uint32_t>(generations_.size()); i-- > 0;) {
            freeList_.push_back(i);
        }
        alive_ = 0;
    }

private:
    template <typename... Ts>
    friend class View;

    template <typename T>
    detail::ComponentPool<T>& pool() {
        uint32_t type = detail::componentTypeIndex<T>();
        if (type >= pools_.size()) {
            pools_.resize(static_cast<size_t>(type) + 1);
        }
        if (!pools_[type]) {
            pools_[type] = std::make_unique<detail::ComponentPool<T>>();
        }
        return static_cast<detail::ComponentPool<T>&>(*pools_[type]);
    }

    template <typename T>
    detail::ComponentPool<T>* findPool() const {
        uint32_t type = detail::componentTypeIndex<T>();
        if (type >= pools_.size() || !pools_[type]) {
            return nullptr;
        }
        return static_cast<detail::ComponentPool<T>*>(pools_[type].get());
    }

    std::vector<std::unique_ptr<detail::PoolBase>> pools_;
    std::vector<uint32_t> generations_;
    std::vector<uint32_t> freeList_;
    std::vector<uint8_t> aliveFlags_; // per slot, parallel to generations_
    std::vector<std::function<void(EntityId)>> onDestroy_;
    size_t alive_ = 0;
};

template <typename... Ts>
size_t View<Ts...>::sizeHint() const {
    size_t smallest = static_cast<size_t>(-1);
    ((smallest = std::min(smallest, [&] {
          auto* p = registry_->template findPool<Ts>();
          return p ? p->size() : size_t{0};
      }())), ...);
    return smallest;
}

template <typename... Ts>
size_t View<Ts...>::count() const {
    size_t n = 0;
    each([&](EntityId, Ts&...) { ++n; });
    return n;
}

template <typename... Ts>
EntityId View<Ts...>::front() const {
    EntityId found = INVALID_ENTITY;
    each([&](EntityId id, Ts&...) { found = id; }); // backwards: last seen is first
    return found;
}

template <typename... Ts>
template <typename Fn>
void View<Ts...>::each(Fn&& fn) const {
    std::tuple<detail::ComponentPool<Ts>*...> pools{registry_->template findPool<Ts>()...};
    bool missing = ((std::get<detail::ComponentPool<Ts>*>(pools) == nullptr) || ...);
    if (missing) {
        return;
    }

    // Drive iteration from the smallest pool to minimise membership checks.
    const detail::PoolBase* driver = nullptr;
    const std::vector<EntityId>* driverEntities = nullptr;
    auto consider = [&](auto* p) {
        if (!driver || p->size() < driver->size()) {
            driver = p;
            driverEntities = &p->entities();
        }
    };
    (consider(std::get<detail::ComponentPool<Ts>*>(pools)), ...);

    // Backwards: swap-remove of the current element only moves already-visited
    // entries into the gap, so removal during iteration is safe.
    for (size_t i = driverEntities->size(); i-- > 0;) {
        if (i >= driverEntities->size()) {
            continue;
        }
        EntityId id = (*driverEntities)[i];
        uint32_t index = entityIndex(id);
        bool all = (std::get<detail::ComponentPool<Ts>*>(pools)->contains(index) && ...);
        if (all) {
            fn(id, std::get<detail::ComponentPool<Ts>*>(pools)->get(index)...);
        }
    }
}

} // namespace ion
