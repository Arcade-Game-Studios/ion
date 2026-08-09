#include <ion/render/Light.hpp>

#include <ion/render/Renderer.hpp>

namespace ion {

void setLighting(Renderer& renderer, const Lighting& lighting) {
    // uAmbient: (ambient.r, ambient.g, ambient.b, 0)
    float ambient[4] = {lighting.ambient.x, lighting.ambient.y,
                        lighting.ambient.z, 0.0f};
    renderer.setUniform("uAmbient", ambient, 1);

    float lightPos[kMaxLights * 4] = {};
    float lightColor[kMaxLights * 4] = {};
    uint32_t count =
        lighting.lightCount > kMaxLights ? kMaxLights : lighting.lightCount;
    for (uint32_t i = 0; i < count; ++i) {
        const Light& light = lighting.lights[i];
        float* p = &lightPos[i * 4];
        float* c = &lightColor[i * 4];
        if (light.type == LightType::Directional) {
            p[0] = light.direction.x;
            p[1] = light.direction.y;
            p[2] = light.direction.z;
            p[3] = 1.0f;  // directional
        } else {
            p[0] = light.position.x;
            p[1] = light.position.y;
            p[2] = light.position.z;
            p[3] = 2.0f;  // point
        }
        c[0] = light.color.x;
        c[1] = light.color.y;
        c[2] = light.color.z;
        c[3] = light.intensity;
    }
    renderer.setUniform("uLightPos", lightPos, kMaxLights);
    renderer.setUniform("uLightColor", lightColor, kMaxLights);
}

} // namespace ion
