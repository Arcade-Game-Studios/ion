#pragma once

#include <cstdint>

namespace ion {

struct ShaderSource {
    const char* vertex = nullptr;
    const char* fragment = nullptr;
};

struct Shader {
    uint64_t id = 0;

    bool isValid() const {
        return id != 0;
    }
};

} // namespace ion
