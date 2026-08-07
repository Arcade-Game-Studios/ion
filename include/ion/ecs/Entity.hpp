#pragma once

#include <cstdint>

namespace ion {

using EntityId = uint64_t;

constexpr EntityId INVALID_ENTITY = 0;

struct Entity {
    EntityId id = INVALID_ENTITY;

    bool isValid() const {
        return id != INVALID_ENTITY;
    }
};

} // namespace ion
