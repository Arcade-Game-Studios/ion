#include <ion/render/Skybox.hpp>

#include <ion/render/Renderer.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace ion {

namespace {

struct FaceDef {
    float bx, by, bz;  // base axis
    float sx, sy, sz;  // +u axis
    float tx, ty, tz;  // +v axis
};

// OpenGL-style cubemap face directions (right-handed, Y-up), ordered
// +X, -X, +Y, -Y, +Z, -Z.
const FaceDef kFaceDefs[6] = {
    {1, 0, 0, 0, 0, -1, 0, -1, 0},    // +X: (1, -t, -s)
    {-1, 0, 0, 0, 0, 1, 0, -1, 0},    // -X: (-1, -t, s)
    {0, 1, 0, 1, 0, 0, 0, 0, 1},      // +Y: (s, 1, t)
    {0, -1, 0, 1, 0, 0, 0, 0, -1},    // -Y: (s, -1, -t)
    {0, 0, 1, 1, 0, 0, 0, -1, 0},     // +Z: (s, -t, 1)
    {0, 0, -1, -1, 0, 0, 0, -1, 0},   // -Z: (-s, -t, -1)
};

void writeFace(uint8_t* out, uint32_t size, const SkyboxConfig& config,
               const FaceDef& face) {
    for (uint32_t v = 0; v < size; ++v) {
        float t = 2.0f * (static_cast<float>(v) + 0.5f) /
                      static_cast<float>(size) -
                  1.0f;
        for (uint32_t u = 0; u < size; ++u) {
            float s = 2.0f * (static_cast<float>(u) + 0.5f) /
                          static_cast<float>(size) -
                      1.0f;
            float x = face.bx + face.sx * s + face.tx * t;
            float y = face.by + face.sy * s + face.ty * t;
            float z = face.bz + face.sz * s + face.tz * t;
            float len = std::sqrt(x * x + y * y + z * z);
            float ny = y / len;

            float r, g, b;
            if (ny >= 0.0f) {
                r = config.horizon.x + (config.top.x - config.horizon.x) * ny;
                g = config.horizon.y + (config.top.y - config.horizon.y) * ny;
                b = config.horizon.z + (config.top.z - config.horizon.z) * ny;
            } else {
                float f = -ny;
                r = config.horizon.x + (config.bottom.x - config.horizon.x) * f;
                g = config.horizon.y + (config.bottom.y - config.horizon.y) * f;
                b = config.horizon.z + (config.bottom.z - config.horizon.z) * f;
            }

            size_t index = (static_cast<size_t>(v) * size + u) * 4;
            out[index] = static_cast<uint8_t>(r * 255.0f + 0.5f);
            out[index + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
            out[index + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
            out[index + 3] = 255;
        }
    }
}

} // namespace

Texture createCubemap(Renderer& renderer, const TextureDesc& desc,
                      const void* const faces[6]) {
    return renderer.createCubemap(desc, faces);
}

Texture createSkyboxTexture(Renderer& renderer, const SkyboxConfig& config) {
    uint32_t size = config.size < 16 ? 16 : config.size;
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);

    const void* facePtrs[6];
    for (int i = 0; i < 6; ++i) {
        writeFace(pixels.data(), size, config, kFaceDefs[i]);
        facePtrs[i] = pixels.data();
    }

    TextureDesc desc;
    desc.width = size;
    desc.height = size;
    desc.format = TextureFormat::RGBA8;
    desc.filterLinear = true;
    return renderer.createCubemap(desc, facePtrs);
}

} // namespace ion
