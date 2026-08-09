#pragma once

#include <ion/render/Texture.hpp>

#include <cstdint>

namespace ion {

//
// Description of an offscreen render target. format selects the color
// attachment format; use TextureFormat::Depth for a depth-only target
// (e.g. a shadow map). withDepth attaches a depth buffer to color targets.
//
struct RenderTargetDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    bool withDepth = true;
};

//
// An offscreen render target. Owns its color and depth attachment textures;
// color is invalid for depth-only targets. Destroy with destroyRenderTarget
// (do not call destroyTexture on the attachments).
//
struct RenderTarget {
    uint64_t id = 0;
    RenderTargetDesc desc;
    Texture color;  // color attachment texture (valid unless depth-only)
    Texture depth;  // depth attachment texture (when desc.withDepth)

    bool isValid() const {
        return id != 0;
    }
};

} // namespace ion
