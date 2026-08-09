#pragma once

#include <ion/math/Vector3.hpp>

#include <array>
#include <cstdint>

namespace ion {

class Renderer;

enum class LightType : uint8_t {
    Directional,  // light shines along `direction` from infinitely far away
    Point,        // light located at `position` with `range` attenuation
};

struct Light {
    LightType type = LightType::Directional;
    Vector3 position = {};                              // point lights
    Vector3 direction = {0.0f, -1.0f, 0.0f};            // directional lights
    Vector3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 50.0f;  // point light attenuation radius
};

// Maximum lights packed into the uLightPos / uLightColor shader arrays.
constexpr uint32_t kMaxLights = 8;

//
// A frame's lighting setup. Packs the lights into fixed-size vec4 arrays the
// shaders declare as `uniform vec4 uLightPos[8]` / `uLightColor[8]` plus
// `uAmbient`. Each entry packs (direction.xyz | position.xyz, w = type) and
// (color.rgb, w = intensity); w == 0 marks an inactive entry (w = 1
// directional, w = 2 point).
//
struct Lighting {
    Vector3 ambient = {0.08f, 0.09f, 0.12f};
    std::array<Light, kMaxLights> lights;
    uint32_t lightCount = 0;
};

// Uploads the lighting to the active shader. The shader must declare
// uAmbient (vec4), uLightPos (vec4[8]) and uLightColor (vec4[8]).
void setLighting(Renderer& renderer, const Lighting& lighting);

} // namespace ion
