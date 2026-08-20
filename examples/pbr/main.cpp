#include <ion/core/Timer.hpp>
#include <ion/math/Matrix4.hpp>
#include <ion/platform/Input.hpp>
#include <ion/platform/Window.hpp>
#include <ion/render/Camera.hpp>
#include <ion/render/Light.hpp>
#include <ion/render/Mesh.hpp>
#include <ion/render/RenderTarget.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/Skybox.hpp>
#include <ion/render/Vertex.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Shadow pass shaders (depth only)
// ---------------------------------------------------------------------------

const char* kShadowMetalVertex = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 uLightMVP;
};

struct VSIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float3 normal [[attribute(3)]];
};

vertex float4 vertexShader(VSIn in [[stage_in]],
                           const device Uniforms& uniforms [[buffer(0)]]) {
    return uniforms.uLightMVP * float4(in.position, 1.0);
}
)";

const char* kShadowMetalFragment = R"(
#include <metal_stdlib>
using namespace metal;

fragment void fragmentShader() {}
)";

const char* kShadowGLVertex = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uLightMVP;

void main() {
    gl_Position = uLightMVP * vec4(aPosition, 1.0);
}
)";

const char* kShadowGLFragment = R"(
#version 410 core

void main() {}
)";

// ---------------------------------------------------------------------------
// PBR shaders (Cook-Torrance, metallic-roughness)
// ---------------------------------------------------------------------------

const char* kPbrMetalVertex = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 uMVP;
    float4x4 uModel;
    float4x4 uLightMVP;
    float4 uCameraPos;
    float4 uAmbient;
    float4 uLightPos[8];
    float4 uLightColor[8];
    float4 uBaseColor;
    float2 uMetalRough;
};

struct VSIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float3 normal [[attribute(3)]];
};

struct VSOut {
    float4 position [[position]];
    float4 color;
    float3 worldPos;
    float3 worldNormal;
};

vertex VSOut vertexShader(VSIn in [[stage_in]],
                          const device Uniforms& uniforms [[buffer(0)]]) {
    VSOut out;
    out.position = uniforms.uMVP * float4(in.position, 1.0);
    out.color = in.color;
    out.worldPos = (uniforms.uModel * float4(in.position, 1.0)).xyz;
    out.worldNormal = (uniforms.uModel * float4(in.normal, 0.0)).xyz;
    return out;
}
)";

const char* kPbrMetalFragment = R"(
#include <metal_stdlib>
using namespace metal;

constant float kPI = 3.14159265359;

struct Uniforms {
    float4x4 uMVP;
    float4x4 uModel;
    float4x4 uLightMVP;
    float4 uCameraPos;
    float4 uAmbient;
    float4 uLightPos[8];
    float4 uLightColor[8];
    float4 uBaseColor;
    float2 uMetalRough;
};

struct VSOut {
    float4 position [[position]];
    float4 color;
    float3 worldPos;
    float3 worldNormal;
};

fragment float4 fragmentShader(VSOut in [[stage_in]],
                               texture2d<float> uShadowMap [[texture(0)]],
                               sampler samp [[sampler(0)]],
                               const device Uniforms& uniforms [[buffer(0)]]) {
    float3 albedo = in.color.rgb * uniforms.uBaseColor.rgb;
    float metallic = uniforms.uMetalRough.x;
    float roughness = uniforms.uMetalRough.y;
    float3 N = normalize(in.worldNormal);
    float3 V = normalize(uniforms.uCameraPos.xyz - in.worldPos);
    float3 F0 = mix(float3(0.04), albedo, metallic);
    float3 Lo = float3(0.0);

    for (int i = 0; i < 8; ++i) {
        float w = uniforms.uLightColor[i].w;
        if (w == 0.0) {
            continue;
        }
        float type = uniforms.uLightPos[i].w;
        float3 L;
        float3 radiance;
        if (type == 1.0) {
            L = normalize(-uniforms.uLightPos[i].xyz);
            radiance = uniforms.uLightColor[i].rgb * w;
        } else {
            float3 toLight = uniforms.uLightPos[i].xyz - in.worldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-4);
            float atten = 1.0 / (1.0 + 0.045 * dist + 0.0075 * dist * dist);
            radiance = uniforms.uLightColor[i].rgb * w * atten;
        }

        float shadow = 1.0;
        if (i == 0 && type == 1.0) {
            float4 shadowCoord =
                uniforms.uLightMVP * float4(in.worldPos, 1.0);
            shadowCoord /= shadowCoord.w;
            float2 uv = shadowCoord.xy * 0.5 + 0.5;
            uv.y = 1.0 - uv.y;
            if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 &&
                shadowCoord.z >= 0.0 && shadowCoord.z <= 1.0) {
                float depth = uShadowMap.sample(samp, uv).r;
                float bias = 0.0015;
                shadow = depth < shadowCoord.z - bias ? 0.35 : 1.0;
            }
        }

        float3 H = normalize(V + L);
        float NoL = max(dot(N, L), 0.0);
        float NoV = max(dot(N, V), 1e-4);
        float NoH = max(dot(N, H), 0.0);
        float VoH = max(dot(V, H), 0.0);

        float alpha = max(roughness * roughness, 0.001);
        float alpha2 = alpha * alpha;
        float d = NoH * NoH * (alpha2 - 1.0) + 1.0;
        float D = alpha2 / (kPI * d * d);

        float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
        float gv = NoV / (NoV * (1.0 - k) + k);
        float gl = NoL / (NoL * (1.0 - k) + k);
        float G = gv * gl;

        float f = pow(1.0 - VoH, 5.0);
        float3 F = F0 + (float3(1.0) - F0) * f;

        float3 specular = D * G * F / max(4.0 * NoV * NoL, 1e-4);
        float3 kS = F;
        float3 kD = (float3(1.0) - kS) * (1.0 - metallic);
        Lo += (kD * albedo / kPI + specular) * radiance * NoL * shadow;
    }

    float3 ambient = uniforms.uAmbient.rgb * albedo;
    return float4(ambient + Lo, 1.0);
}
)";

const char* kPbrGLVertex = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec4 vColor;
out vec3 vWorldPos;
out vec3 vWorldNormal;

void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    vColor = aColor;
    vWorldPos = (uModel * vec4(aPosition, 1.0)).xyz;
    vWorldNormal = mat3(uModel) * aNormal;
}
)";

const char* kPbrGLFragment = R"(
#version 410 core

in vec4 vColor;
in vec3 vWorldPos;
in vec3 vWorldNormal;

uniform mat4 uLightMVP;
uniform vec4 uCameraPos;
uniform vec4 uAmbient;
uniform vec4 uLightPos[8];
uniform vec4 uLightColor[8];
uniform vec4 uBaseColor;
uniform vec2 uMetalRough;
uniform sampler2D uShadowMap;

out vec4 fragColor;

const float kPI = 3.14159265359;

void main() {
    vec3 albedo = vColor.rgb * uBaseColor.rgb;
    float metallic = uMetalRough.x;
    float roughness = uMetalRough.y;
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(uCameraPos.xyz - vWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < 8; ++i) {
        float w = uLightColor[i].w;
        if (w == 0.0) {
            continue;
        }
        float type = uLightPos[i].w;
        vec3 L;
        vec3 radiance;
        if (type == 1.0) {
            L = normalize(-uLightPos[i].xyz);
            radiance = uLightColor[i].rgb * w;
        } else {
            vec3 toLight = uLightPos[i].xyz - vWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-4);
            float atten = 1.0 / (1.0 + 0.045 * dist + 0.0075 * dist * dist);
            radiance = uLightColor[i].rgb * w * atten;
        }

        float shadow = 1.0;
        if (i == 0 && type == 1.0) {
            vec4 shadowCoord = uLightMVP * vec4(vWorldPos, 1.0);
            shadowCoord /= shadowCoord.w;
            vec2 uv = shadowCoord.xy * 0.5 + 0.5;
            uv.y = 1.0 - uv.y;
            if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 &&
                shadowCoord.z >= 0.0 && shadowCoord.z <= 1.0) {
                float depth = texture(uShadowMap, uv).r;
                float bias = 0.0015;
                shadow = depth < shadowCoord.z - bias ? 0.35 : 1.0;
            }
        }

        vec3 H = normalize(V + L);
        float NoL = max(dot(N, L), 0.0);
        float NoV = max(dot(N, V), 1e-4);
        float NoH = max(dot(N, H), 0.0);
        float VoH = max(dot(V, H), 0.0);

        float alpha = max(roughness * roughness, 0.001);
        float alpha2 = alpha * alpha;
        float d = NoH * NoH * (alpha2 - 1.0) + 1.0;
        float D = alpha2 / (kPI * d * d);

        float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
        float gv = NoV / (NoV * (1.0 - k) + k);
        float gl = NoL / (NoL * (1.0 - k) + k);
        float G = gv * gl;

        float f = pow(1.0 - VoH, 5.0);
        vec3 F = F0 + (vec3(1.0) - F0) * f;

        vec3 specular = D * G * F / max(4.0 * NoV * NoL, 1e-4);
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        Lo += (kD * albedo / kPI + specular) * radiance * NoL * shadow;
    }

    vec3 ambient = uAmbient.rgb * albedo;
    fragColor = vec4(ambient + Lo, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Skybox shaders
// ---------------------------------------------------------------------------

const char* kSkyboxMetalVertex = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 uInvViewProjection;
    float4 uCameraPos;
};

struct VSIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float3 normal [[attribute(3)]];
};

struct VSOut {
    float4 position [[position]];
    float3 worldDir;
};

vertex VSOut vertexShader(VSIn in [[stage_in]],
                          const device Uniforms& uniforms [[buffer(0)]]) {
    VSOut out;
    out.position = float4(in.position.xy, 1.0, 1.0);
    float4 p =
        uniforms.uInvViewProjection * float4(in.position.xy, 1.0, 1.0);
    out.worldDir = p.xyz / max(p.w, 1e-4) - uniforms.uCameraPos.xyz;
    return out;
}
)";

const char* kSkyboxMetalFragment = R"(
#include <metal_stdlib>
using namespace metal;

struct VSOut {
    float4 position [[position]];
    float3 worldDir;
};

fragment float4 fragmentShader(VSOut in [[stage_in]],
                               texturecube<float> uSkybox [[texture(0)]],
                               sampler samp [[sampler(0)]]) {
    return uSkybox.sample(samp, normalize(in.worldDir));
}
)";

const char* kSkyboxGLVertex = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uInvViewProjection;
uniform vec4 uCameraPos;

out vec3 vWorldDir;

void main() {
    gl_Position = vec4(aPosition.xy, 1.0, 1.0);
    vec4 p = uInvViewProjection * vec4(aPosition.xy, 1.0, 1.0);
    vWorldDir = p.xyz / max(p.w, 1e-4) - uCameraPos.xyz;
}
)";

const char* kSkyboxGLFragment = R"(
#version 410 core

in vec3 vWorldDir;

uniform samplerCube uSkybox;

out vec4 fragColor;

void main() {
    fragColor = texture(uSkybox, normalize(vWorldDir));
}
)";

// ---------------------------------------------------------------------------
// Post-processing shaders (brightness + vignette)
// ---------------------------------------------------------------------------

const char* kPostMetalVertex = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float2 uResolution;
    float uExposure;
    float uVignette;
};

struct VSIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float3 normal [[attribute(3)]];
};

struct VSOut {
    float4 position [[position]];
    float2 uv;
};

vertex VSOut vertexShader(VSIn in [[stage_in]],
                          const device Uniforms& uniforms [[buffer(0)]]) {
    VSOut out;
    out.position = float4(in.position.xy, 0.0, 1.0);
    out.uv = in.uv;
    return out;
}
)";

const char* kPostMetalFragment = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float2 uResolution;
    float uExposure;
    float uVignette;
};

struct VSOut {
    float4 position [[position]];
    float2 uv;
};

fragment float4 fragmentShader(VSOut in [[stage_in]],
                               texture2d<float> uScene [[texture(0)]],
                               sampler samp [[sampler(0)]],
                               const device Uniforms& uniforms [[buffer(0)]]) {
    float3 c = uScene.sample(samp, in.uv).rgb;
    c *= (1.0 + uniforms.uExposure * 0.35);
    float2 centered = in.uv - 0.5;
    float vignette = 1.0 - uniforms.uVignette * dot(centered, centered);
    return float4(c * vignette, 1.0);
}
)";

const char* kPostGLVertex = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV;

out vec2 vUV;

void main() {
    gl_Position = vec4(aPosition.xy, 0.0, 1.0);
    vUV = aUV;
}
)";

const char* kPostGLFragment = R"(
#version 410 core

in vec2 vUV;

uniform sampler2D uScene;
uniform vec2 uResolution;
uniform float uExposure;
uniform float uVignette;

out vec4 fragColor;

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    c *= (1.0 + uExposure * 0.35);
    vec2 centered = vUV - 0.5;
    float vignette = 1.0 - uVignette * dot(centered, centered);
    fragColor = vec4(c * vignette, 1.0);
}
)";

ion::Matrix4 rotationY(float angle) {
    ion::Matrix4 m = ion::Matrix4::identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    m.m[0] = c;
    m.m[2] = -s;
    m.m[8] = s;
    m.m[10] = c;
    return m;
}

ion::Mesh buildCube(ion::Renderer& renderer, float size,
                    const ion::Vector4& color) {
    const float h = size * 0.5f;
    const ion::Vector3 corners[6][4] = {
        {{h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h}},    // +X
        {{-h, -h, h}, {-h, -h, -h}, {-h, h, -h}, {-h, h, h}},  // -X
        {{-h, h, -h}, {-h, h, h}, {h, h, h}, {h, h, -h}},    // +Y
        {{-h, -h, h}, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}},  // -Y
        {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}},    // +Z
        {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}},  // -Z
    };
    const ion::Vector3 normals[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };
    std::vector<ion::Vertex> verts;
    std::vector<uint32_t> indices;
    for (int face = 0; face < 6; ++face) {
        uint32_t start = (uint32_t)verts.size();
        for (int i = 0; i < 4; ++i) {
            const ion::Vector3& p = corners[face][i];
            verts.push_back({{p.x, p.y, p.z}, color,
                             {(i == 1 || i == 2) ? 1.0f : 0.0f,
                              (i == 2 || i == 3) ? 1.0f : 0.0f},
                             normals[face]});
        }
        indices.push_back(start);
        indices.push_back(start + 1);
        indices.push_back(start + 2);
        indices.push_back(start);
        indices.push_back(start + 2);
        indices.push_back(start + 3);
    }
    return ion::createMesh(renderer, verts.data(), (uint32_t)verts.size(),
                           indices.data(), (uint32_t)indices.size());
}

ion::Mesh buildCheckerFloor(ion::Renderer& renderer, int cells,
                            float cellSize) {
    std::vector<ion::Vertex> verts;
    std::vector<uint32_t> indices;
    const ion::Vector3 dark(0.13f, 0.13f, 0.17f);
    const ion::Vector3 light(0.26f, 0.26f, 0.33f);
    const float half = cellSize * (float)cells * 0.5f;
    for (int i = 0; i < cells; i++) {
        for (int j = 0; j < cells; j++) {
            float x0 = -half + (float)i * cellSize;
            float z0 = -half + (float)j * cellSize;
            ion::Vector3 p0(x0, 0.0f, z0);
            ion::Vector3 p1(x0 + cellSize, 0.0f, z0);
            ion::Vector3 p2(x0 + cellSize, 0.0f, z0 + cellSize);
            ion::Vector3 p3(x0, 0.0f, z0 + cellSize);
            const ion::Vector3& color = ((i + j) % 2 == 0) ? dark : light;
            uint32_t start = (uint32_t)verts.size();
            verts.push_back({{p0.x, p0.y, p0.z}, {color.x, color.y, color.z, 1.0f}, {0, 0}, {0, 1, 0}});
            verts.push_back({{p1.x, p1.y, p1.z}, {color.x, color.y, color.z, 1.0f}, {0, 0}, {0, 1, 0}});
            verts.push_back({{p2.x, p2.y, p2.z}, {color.x, color.y, color.z, 1.0f}, {0, 0}, {0, 1, 0}});
            verts.push_back({{p3.x, p3.y, p3.z}, {color.x, color.y, color.z, 1.0f}, {0, 0}, {0, 1, 0}});
            indices.push_back(start);
            indices.push_back(start + 1);
            indices.push_back(start + 2);
            indices.push_back(start);
            indices.push_back(start + 2);
            indices.push_back(start + 3);
        }
    }
    return ion::createMesh(renderer, verts.data(), (uint32_t)verts.size(),
                           indices.data(), (uint32_t)indices.size());
}

ion::Mesh buildFullscreenMesh(ion::Renderer& renderer) {
    std::vector<ion::Vertex> verts = {
        {{-1, -1, 0}, {1, 1, 1, 1}, {0, 0}, {0, 0, 0}},
        {{3, -1, 0}, {1, 1, 1, 1}, {2, 0}, {0, 0, 0}},
        {{-1, 3, 0}, {1, 1, 1, 1}, {0, 2}, {0, 0, 0}},
    };
    return ion::createMesh(renderer, verts.data(), 3, nullptr, 0);
}

class AppPbr {
public:
    int run(const char* backendName) {
        ion::WindowConfig config;
        config.title = "Ion PBR";
        config.appName = "IonPbr";
        config.width = 1280;
        config.height = 720;

        window_ = ion::Window(config);
        if (!window_.create()) {
            std::printf("failed to create window\n");
            return 1;
        }

        ion::RendererConfig rendererConfig;
        if (std::strcmp(backendName, "metal") == 0) {
            rendererConfig.backend = ion::RendererBackend::Metal;
        } else if (std::strcmp(backendName, "gl") == 0) {
            rendererConfig.backend = ion::RendererBackend::OpenGL;
        } else if (std::strcmp(backendName, "null") == 0) {
            rendererConfig.backend = ion::RendererBackend::Null;
        }

        if (!renderer_.initialize(&window_, rendererConfig)) {
            std::printf("failed to initialize renderer\n");
            window_.destroy();
            return 1;
        }

        const ion::GPUInfo& gpu = renderer_.gpuInfo();
        std::printf("GPU: %s (%s)\n", gpu.name.c_str(), gpu.vendor.c_str());
        bool isMetal = gpu.backend == ion::RendererBackend::Metal;

        auto createShaders = [&](const char* metalVertex,
                                 const char* metalFragment,
                                 const char* glVertex,
                                 const char* glFragment) {
            ion::ShaderSource source;
            source.vertex = isMetal ? metalVertex : glVertex;
            source.fragment = isMetal ? metalFragment : glFragment;
            return renderer_.createShader(source);
        };

        shaders_[Shadow] = createShaders(kShadowMetalVertex,
                                         kShadowMetalFragment,
                                         kShadowGLVertex, kShadowGLFragment);
        shaders_[Pbr] =
            createShaders(kPbrMetalVertex, kPbrMetalFragment, kPbrGLVertex,
                          kPbrGLFragment);
        shaders_[Skybox] = createShaders(kSkyboxMetalVertex,
                                         kSkyboxMetalFragment,
                                         kSkyboxGLVertex, kSkyboxGLFragment);
        shaders_[Post] =
            createShaders(kPostMetalVertex, kPostMetalFragment,
                          kPostGLVertex, kPostGLFragment);
        for (int i = 0; i < kShaderCount; ++i) {
            if (!shaders_[i].isValid()) {
                std::printf("failed to create shader %d\n", i);
                renderer_.shutdown();
                window_.destroy();
                return 1;
            }
        }

        ion::SkyboxConfig skyboxConfig;
        if (std::getenv("ION_PBR_DEBUG_SKY")) {
            skyboxConfig.top = ion::Vector3(1.0f, 0.0f, 0.0f);     // red
            skyboxConfig.horizon = ion::Vector3(1.0f, 1.0f, 1.0f);  // white
            skyboxConfig.bottom = ion::Vector3(0.0f, 1.0f, 0.0f);   // green
        }
        skybox_ = ion::createSkyboxTexture(renderer_, skyboxConfig);

        ion::RenderTargetDesc shadowDesc;
        shadowDesc.width = 1024;
        shadowDesc.height = 1024;
        shadowDesc.format = ion::TextureFormat::Depth;
        shadowDesc.withDepth = true;
        shadowTarget_ = renderer_.createRenderTarget(shadowDesc);

        ion::RenderTargetDesc sceneDesc;
        sceneDesc.width = window_.width();
        sceneDesc.height = window_.height();
        sceneDesc.format = ion::TextureFormat::RGBA16F;
        sceneDesc.withDepth = true;
        sceneTarget_ = renderer_.createRenderTarget(sceneDesc);
        if (!shadowTarget_.isValid() || !sceneTarget_.isValid()) {
            std::printf("failed to create render targets\n");
            renderer_.shutdown();
            window_.destroy();
            return 1;
        }

        camera_.setPosition(ion::Vector3(0.0f, 2.6f, 6.5f));
        camera_.lookAt(ion::Vector3(0.0f, 0.8f, 0.0f));
        camera_.setViewport(window_.width(), window_.height());
        camera_.setFov(1.0f);

        goldCube_ = buildCube(renderer_, 1.5f, {1.0f, 0.72f, 0.10f, 1.0f});
        redCube_ = buildCube(renderer_, 1.5f, {0.85f, 0.18f, 0.18f, 1.0f});
        greenCube_ = buildCube(renderer_, 1.5f, {0.18f, 0.55f, 0.35f, 1.0f});
        floor_ = buildCheckerFloor(renderer_, 12, 1.0f);
        fullscreen_ = buildFullscreenMesh(renderer_);

        // Lighting: one shadow-casting directional light plus two point
        // lights. setLighting uploads the packed arrays.
        lightDir_ = ion::Vector3(-0.5f, -1.0f, -0.35f).normalized();
        lighting_.ambient = ion::Vector3(0.05f, 0.06f, 0.09f);
        lighting_.lightCount = 3;
        lighting_.lights[0].type = ion::LightType::Directional;
        lighting_.lights[0].direction = lightDir_;
        lighting_.lights[0].color = ion::Vector3(1.0f, 0.95f, 0.88f);
        lighting_.lights[0].intensity = 1.15f;
        lighting_.lights[1].type = ion::LightType::Point;
        lighting_.lights[1].position = ion::Vector3(2.6f, 1.6f, 1.8f);
        lighting_.lights[1].color = ion::Vector3(1.0f, 0.45f, 0.15f);
        lighting_.lights[1].intensity = 7.0f;
        lighting_.lights[2].type = ion::LightType::Point;
        lighting_.lights[2].position = ion::Vector3(-2.6f, 1.2f, -1.4f);
        lighting_.lights[2].color = ion::Vector3(0.25f, 0.5f, 1.0f);
        lighting_.lights[2].intensity = 6.0f;

        // Light-space view-projection for the shadow map.
        ion::Vector3 lightEye = ion::Vector3(0.0f, 1.2f, 0.0f) - lightDir_ * 16.0f;
        ion::Matrix4 lightView = ion::Matrix4::lookAt(
            lightEye, ion::Vector3(0.0f, 1.2f, 0.0f), ion::Vector3(0.0f, 1.0f, 0.0f));
        lightViewProj_ = ion::Matrix4::orthographic(-9.0f, 9.0f, -9.0f, 9.0f,
                                                    1.0f, 34.0f) * lightView;

        std::printf("Controls: WASD move | mouse drag look | Space/C up/down | "
                    "1/2/3 cycle exposure | Esc quit\n");

        timer_.reset();
        while (window_.isOpen()) {
            window_.pollEvents();
            float deltaTime = timer_.tick();
            if (ion::input::isKeyPressed(ion::Key::Escape)) {
                break;
            }
            if (ion::input::isKeyPressed(ion::Key::Num1)) {
                exposure_ = 0.0f;
            }
            if (ion::input::isKeyPressed(ion::Key::Num2)) {
                exposure_ = 1.0f;
            }
            if (ion::input::isKeyPressed(ion::Key::Num3)) {
                exposure_ = 2.0f;
            }

            updateCamera_(deltaTime);
            angle_ += deltaTime;

            frames_++;
            elapsed_ += deltaTime;
            if (frames_ % 120 == 0) {
                std::printf("avg %.2f ms/frame, %u tris\n",
                            elapsed_ / (float)frames_ * 1000.0f,
                            renderer_.stats().triangles);
            }

            render();
            ion::input::update();
        }

        ion::destroyMesh(renderer_, fullscreen_);
        ion::destroyMesh(renderer_, floor_);
        ion::destroyMesh(renderer_, greenCube_);
        ion::destroyMesh(renderer_, redCube_);
        ion::destroyMesh(renderer_, goldCube_);
        renderer_.destroyRenderTarget(sceneTarget_);
        renderer_.destroyRenderTarget(shadowTarget_);
        renderer_.destroyTexture(skybox_);
        for (int i = 0; i < kShaderCount; ++i) {
            renderer_.destroyShader(shaders_[i]);
        }
        renderer_.shutdown();
        window_.destroy();
        std::printf("PBR example exited.\n");
        return 0;
    }

private:
    enum ShaderKind {
        Shadow = 0,
        Pbr,
        Skybox,
        Post,
        kShaderCount,
    };

    void updateCamera_(float deltaTime) {
        const float sensitivity = 0.0025f;
        if (ion::input::isMouseDown(ion::MouseButton::Left)) {
            camera_.setYaw(camera_.yaw() -
                           ion::input::mouseDeltaX() * sensitivity);
            camera_.setPitch(camera_.pitch() -
                             ion::input::mouseDeltaY() * sensitivity);
        }

        const float speed = 4.0f;
        ion::Vector3 move;
        if (ion::input::isKeyDown(ion::Key::W)) {
            move.z += 1.0f;
        }
        if (ion::input::isKeyDown(ion::Key::S)) {
            move.z -= 1.0f;
        }
        if (ion::input::isKeyDown(ion::Key::D)) {
            move.x += 1.0f;
        }
        if (ion::input::isKeyDown(ion::Key::A)) {
            move.x -= 1.0f;
        }
        if (ion::input::isKeyDown(ion::Key::Space)) {
            move.y += 1.0f;
        }
        if (ion::input::isKeyDown(ion::Key::C)) {
            move.y -= 1.0f;
        }
        camera_.move(move * (speed * deltaTime));
    }

    struct DrawItem {
        ion::Mesh* mesh;
        ion::Matrix4 transform;
        ion::Vector4 baseColor;
        float metallic;
        float roughness;
    };

    std::vector<DrawItem> frameItems_() {
        std::vector<DrawItem> items;
        items.push_back({&goldCube_,
                         ion::Matrix4::translation(ion::Vector3(-2.2f, 1.5f, 0.0f)) *
                             rotationY(angle_ * 0.6f),
                         {1.0f, 0.72f, 0.10f, 1.0f}, 0.95f, 0.18f});
        items.push_back({&redCube_,
                         ion::Matrix4::translation(ion::Vector3(0.0f, 1.5f, 0.0f)) *
                             rotationY(-angle_ * 0.5f),
                         {0.85f, 0.18f, 0.18f, 1.0f}, 0.10f, 0.62f});
        items.push_back({&greenCube_,
                         ion::Matrix4::translation(ion::Vector3(2.2f, 1.5f, 0.0f)) *
                             rotationY(angle_ * 0.45f),
                         {0.18f, 0.55f, 0.35f, 1.0f}, 0.45f, 0.35f});
        items.push_back({&floor_,
                         ion::Matrix4::scale(ion::Vector3(1.0f, 1.0f, 1.0f)),
                         {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 0.85f});
        return items;
    }

    void drawShadows_() {
        renderer_.useShader(shaders_[Shadow]);
        renderer_.setDepthWrite(true);
        for (const DrawItem& item : frameItems_()) {
            renderer_.setUniform("uLightMVP", lightViewProj_ * item.transform);
            renderer_.setVertexBuffer(item.mesh->vertexBuffer);
            if (item.mesh->indexBuffer.isValid()) {
                renderer_.setIndexBuffer(item.mesh->indexBuffer);
                renderer_.drawIndexed(item.mesh->indexCount);
            } else {
                renderer_.draw(item.mesh->vertexCount);
            }
        }
    }

    void drawScene_() {
        renderer_.useShader(shaders_[Pbr]);
        renderer_.setDepthWrite(true);
        renderer_.setUniform("uLightMVP", lightViewProj_);
        renderer_.setUniform("uCameraPos", camera_.position());
        renderer_.setTexture(0, shadowTarget_.depth);
        ion::setLighting(renderer_, lighting_);

        ion::Matrix4 viewProjection = camera_.viewProjection();
        for (const DrawItem& item : frameItems_()) {
            renderer_.setUniform("uMVP", viewProjection * item.transform);
            renderer_.setUniform("uModel", item.transform);
            renderer_.setUniform("uBaseColor", item.baseColor);
            renderer_.setUniform("uMetalRough",
                                 ion::Vector2(item.metallic, item.roughness));
            renderer_.setVertexBuffer(item.mesh->vertexBuffer);
            if (item.mesh->indexBuffer.isValid()) {
                renderer_.setIndexBuffer(item.mesh->indexBuffer);
                renderer_.drawIndexed(item.mesh->indexCount);
            } else {
                renderer_.draw(item.mesh->vertexCount);
            }
        }
    }

    void render() {
        renderer_.beginFrame();

        if (std::getenv("ION_PBR_DIRECT")) {
            // Debug: render the scene straight to the window (no shadow map,
            // skybox or post) to isolate the offscreen/pass pipeline.
            renderer_.clear(ion::Color(0.04f, 0.05f, 0.08f));
            drawScene_();
            renderer_.endFrame();
            return;
        }

        if (std::getenv("ION_PBR_SKY_ONLY")) {
            // Debug: draw only the skybox quad straight to the window.
            renderer_.clear(ion::Color::black());
            renderer_.setDepthWrite(false);
            renderer_.useShader(shaders_[Skybox]);
            ion::Matrix4 vp = camera_.viewProjection();
            ion::Matrix4 ivp = ion::Matrix4::inverse(vp);
            if (std::getenv("ION_PBR_DEBUG_SKY")) {
                // Sanity-check the inverse on the CPU.
                auto probeDir = [](const ion::Matrix4& inv, float x, float y) {
                    float w = inv.m[3] * x + inv.m[7] * y + inv.m[11] + inv.m[15];
                    float px = (inv.m[0] * x + inv.m[4] * y + inv.m[8] + inv.m[12]) / w;
                    float py = (inv.m[1] * x + inv.m[5] * y + inv.m[9] + inv.m[13]) / w;
                    float pz = (inv.m[2] * x + inv.m[6] * y + inv.m[10] + inv.m[14]) / w;
                    return py;  // world y of the reconstructed far-plane point
                };
                float topY = probeDir(ivp, 0.0f, 1.0f);
                float botY = probeDir(ivp, 0.0f, -1.0f);
                std::printf("skybox CPU check: top-clip worldY=%.3f (expect >2.6), "
                            "bottom-clip worldY=%.3f (expect <2.6)\n",
                            topY, botY);
            }
            renderer_.setUniform("uInvViewProjection", ivp);
            renderer_.setUniform("uCameraPos", camera_.position());
            renderer_.setTexture(0, skybox_);
            renderer_.setVertexBuffer(fullscreen_.vertexBuffer);
            renderer_.draw(fullscreen_.vertexCount);
            renderer_.endFrame();
            return;
        }

        // 1. Shadow map pass (depth-only target).
        renderer_.setRenderTarget(shadowTarget_);
        renderer_.clear(ion::Color::white());
        drawShadows_();

        // 2. Scene pass into the offscreen color target.
        renderer_.setRenderTarget(sceneTarget_);
        renderer_.clear(ion::Color(0.04f, 0.05f, 0.08f));
        drawScene_();

        // 3. Skybox fills the background (depth test, no depth write).
        renderer_.setDepthWrite(false);
        renderer_.useShader(shaders_[Skybox]);
        renderer_.setUniform("uInvViewProjection",
                             ion::Matrix4::inverse(camera_.viewProjection()));
        renderer_.setUniform("uCameraPos", camera_.position());
        renderer_.setTexture(0, skybox_);
        renderer_.setVertexBuffer(fullscreen_.vertexBuffer);
        if (!std::getenv("ION_PBR_NO_SKYBOX")) {
            renderer_.draw(fullscreen_.vertexCount);
        }
        renderer_.setDepthWrite(true);

        // 4. Post-processing pass onto the window.
        renderer_.setDefaultRenderTarget();
        renderer_.clear(ion::Color::black());
        renderer_.useShader(shaders_[Post]);
        renderer_.setUniform("uResolution",
                             ion::Vector2((float)window_.width(),
                                          (float)window_.height()));
        renderer_.setUniform("uExposure", exposure_);
        renderer_.setUniform("uVignette", 0.55f);
        renderer_.setTexture(0, sceneTarget_.color);
        renderer_.setVertexBuffer(fullscreen_.vertexBuffer);
        renderer_.draw(fullscreen_.vertexCount);

        renderer_.endFrame();
    }

    ion::Window window_;
    ion::Renderer renderer_;
    ion::Shader shaders_[kShaderCount];
    ion::Texture skybox_;
    ion::RenderTarget shadowTarget_;
    ion::RenderTarget sceneTarget_;
    ion::Mesh goldCube_;
    ion::Mesh redCube_;
    ion::Mesh greenCube_;
    ion::Mesh floor_;
    ion::Mesh fullscreen_;
    ion::Camera camera_;
    ion::Timer timer_;
    ion::Lighting lighting_;
    ion::Matrix4 lightViewProj_;
    ion::Vector3 lightDir_;
    float angle_ = 0.0f;
    float exposure_ = 0.8f;
    int frames_ = 0;
    float elapsed_ = 0.0f;
};

} // namespace

int main(int argc, char** argv) {
    const char* backend = (argc > 1) ? argv[1] : "metal";
    AppPbr app;
    return app.run(backend);
}
