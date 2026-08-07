#pragma once

#include <ion/math/Vector2.hpp>
#include <ion/math/Vector3.hpp>

#include <cstdint>

namespace ion {

enum class TextureFormat {
    RGBA8,
    RGBA16F,
    Depth,
};

struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    bool filterLinear = true;
    bool generateMipmaps = false;
};

struct Texture {
    uint64_t id = 0;
    TextureDesc desc;

    bool isValid() const {
        return id != 0;
    }
};

} // namespace ion
