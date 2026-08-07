#pragma once

#include <cstdint>

namespace ion {

enum class ComponentType : uint32_t {};

struct Component {
    ComponentType type{};
};

} // namespace ion
