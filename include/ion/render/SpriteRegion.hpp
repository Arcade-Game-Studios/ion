#pragma once

#include <ion/render/Texture.hpp>

#include <cstdint>

namespace ion {

//
// A rectangular sub-region of a texture, defined in pixels (x, y measured
// from the top-left of the texture). UV coordinates are computed from the
// pixel rect and the texture dimensions.
//
struct SpriteRegion {
    Texture texture;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;

    bool isValid() const {
        return texture.isValid() && width > 0 && height > 0;
    }

    void computeUV() {
        if (!texture.isValid() || texture.desc.width == 0 ||
            texture.desc.height == 0) {
            u0 = 0.0f;
            v0 = 0.0f;
            u1 = 1.0f;
            v1 = 1.0f;
            return;
        }
        float texW = (float)texture.desc.width;
        float texH = (float)texture.desc.height;
        u0 = (float)x / texW;
        v0 = (float)y / texH;
        u1 = (float)(x + width) / texW;
        v1 = (float)(y + height) / texH;
    }

    static SpriteRegion full(const Texture& texture) {
        SpriteRegion region;
        region.texture = texture;
        region.width = texture.desc.width;
        region.height = texture.desc.height;
        region.computeUV();
        return region;
    }
};

} // namespace ion
