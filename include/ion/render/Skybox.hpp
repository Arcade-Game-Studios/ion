#pragma once

#include <ion/math/Vector3.hpp>
#include <ion/render/Texture.hpp>

#include <cstdint>

namespace ion {

class Renderer;

//
// Colors for the procedurally generated gradient skybox. The gradient blends
// top -> horizon -> bottom along the view direction's +Y component.
//
struct SkyboxConfig {
    Vector3 top = {0.20f, 0.45f, 0.75f};
    Vector3 horizon = {0.65f, 0.78f, 0.90f};
    Vector3 bottom = {0.52f, 0.50f, 0.46f};
    uint32_t size = 128;  // cube face size in pixels
};

// Generates a procedural gradient cubemap usable as a skybox. Sample it with
// a `samplerCube` / `texturecube` shader; see examples/pbr for the full
// skybox pass (fullscreen quad + inverse view-projection ray).
Texture createSkyboxTexture(Renderer& renderer,
                            const SkyboxConfig& config = {});

// Creates a cubemap texture from six RGBA8 face buffers (order: +X, -X, +Y,
// -Y, +Z, -Z). Each face must contain desc.width * desc.height * 4 bytes.
Texture createCubemap(Renderer& renderer, const TextureDesc& desc,
                      const void* const faces[6]);

} // namespace ion
