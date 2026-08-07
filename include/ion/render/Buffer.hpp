#pragma once

#include <cstdint>

namespace ion {

struct VertexBuffer {
    uint64_t id = 0;
    uint64_t size = 0;

    bool isValid() const {
        return id != 0;
    }
};

struct IndexBuffer {
    uint64_t id = 0;
    uint32_t count = 0;
    bool is16Bit = true;

    bool isValid() const {
        return id != 0;
    }
};

} // namespace ion
