#pragma once

#include <ion/ecs/Entity.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ion {

enum class ComponentType : uint32_t {};

struct Component {
    ComponentType type{};
};

namespace detail {

inline uint32_t nextComponentTypeIndex() {
    static uint32_t counter = 0;
    return counter++;
}

// Dense, process-wide index per component type (used to index registry pools).
template <typename T>
uint32_t componentTypeIndex() {
    static const uint32_t index = nextComponentTypeIndex();
    return index;
}

class PoolBase {
public:
    virtual ~PoolBase() = default;
    virtual bool contains(uint32_t entityIndex) const = 0;
    virtual void remove(uint32_t entityIndex) = 0;
    virtual size_t size() const = 0;
};

// Sparse-set storage: O(1) add/remove/lookup, components packed contiguously
// for cache-friendly iteration. Removal swaps the last element into the gap.
template <typename T>
class ComponentPool final : public PoolBase {
public:
    bool contains(uint32_t index) const override {
        return index < sparse_.size() && sparse_[index] != kNone;
    }

    template <typename... Args>
    T& emplace(uint32_t index, EntityId id, Args&&... args) {
        if (contains(index)) {
            components_[sparse_[index]] = T{std::forward<Args>(args)...};
            return components_[sparse_[index]];
        }
        if (index >= sparse_.size()) {
            sparse_.resize(static_cast<size_t>(index) + 1, kNone);
        }
        sparse_[index] = static_cast<uint32_t>(components_.size());
        entities_.push_back(id);
        components_.emplace_back(T{std::forward<Args>(args)...});
        return components_.back();
    }

    T& get(uint32_t index) {
        assert(contains(index));
        return components_[sparse_[index]];
    }

    T* tryGet(uint32_t index) {
        return contains(index) ? &components_[sparse_[index]] : nullptr;
    }

    void remove(uint32_t index) override {
        if (!contains(index)) {
            return;
        }
        uint32_t slot = sparse_[index];
        uint32_t last = static_cast<uint32_t>(components_.size()) - 1;
        if (slot != last) {
            components_[slot] = std::move(components_[last]);
            entities_[slot] = entities_[last];
            sparse_[entityIndex(entities_[slot])] = slot;
        }
        components_.pop_back();
        entities_.pop_back();
        sparse_[index] = kNone;
    }

    size_t size() const override { return components_.size(); }

    // Packed entity ids, parallel to components().
    const std::vector<EntityId>& entities() const { return entities_; }
    std::vector<T>& components() { return components_; }

private:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    std::vector<uint32_t> sparse_;
    std::vector<EntityId> entities_;
    std::vector<T> components_;
};

} // namespace detail
} // namespace ion
