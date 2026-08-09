#pragma once

#include <ion/render/Color.hpp>
#include <ion/render/Texture.hpp>

#include <string>

namespace ion {

//
// Surface appearance for a model part. baseColor multiplies the sampled
// texture (or is the final color when texture is invalid). metallic and
// roughness are carried through for the future lighting system; the current
// renderers treat them as informational.
//
struct Material {
    std::string name;
    Color baseColor = Color::white();
    Texture texture;             // optional albedo texture (sampled in slot 0)
    float metallic = 0.0f;
    float roughness = 1.0f;
};

} // namespace ion
