#pragma once

#include <cstdint>

namespace ion {

// An EntityId packs a slot index (low 32 bits) and a generation (high 32
// bits). Generations start at 1, so a live id is never INVALID_ENTITY, and a
// stale id (whose slot has since been recycled) never matches the new owner.
using EntityId = uint64_t;

constexpr EntityId INVALID_ENTITY = 0;

constexpr uint32_t entityIndex(EntityId id) {
    return static_cast<uint32_t>(id & 0xFFFFFFFFull);
}

constexpr uint32_t entityGeneration(EntityId id) {
    return static_cast<uint32_t>(id >> 32);
}

constexpr EntityId makeEntityId(uint32_t index, uint32_t generation) {
    return (static_cast<EntityId>(generation) << 32) | index;
}

struct Entity {
    EntityId id = INVALID_ENTITY;

    bool isValid() const {
        return id != INVALID_ENTITY;
    }

    friend bool operator==(Entity a, Entity b) { return a.id == b.id; }
    friend bool operator!=(Entity a, Entity b) { return a.id != b.id; }
};

} // namespace ion
