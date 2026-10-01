#pragma once

#include <ion/ecs/Component.hpp>
#include <ion/ecs/Entity.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
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
        } else {
            index = static_cast<uint32_t>(generations_.size());
            generations_.push_back(1);
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
        --alive_;
    }

    size_t aliveCount() const { return alive_; }

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

    // Destroys every entity. Invalidates all outstanding ids.
    void clear() {
        for (uint32_t i = 0; i < generations_.size(); ++i) {
            for (auto& pool : pools_) {
                if (pool) {
                    pool->remove(i);
                }
            }
            if (++generations_[i] == 0) {
                generations_[i] = 1;
            }
        }
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
