#include <ion/core/Timer.hpp>
#include <ion/platform/Input.hpp>
#include <ion/platform/Window.hpp>
#include <ion/render/Camera.hpp>
#include <ion/render/Model.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/Vertex.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const char* kMetalVertexShader = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 uMVP;
    float4x4 uModel;
    float4 uLightDir;
    float2 uLightParams;
    float4 uBaseColor;
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
    float2 uv;
    float3 worldNormal;
};

vertex VSOut vertexShader(VSIn in [[stage_in]],
                          const device Uniforms& uniforms [[buffer(0)]]) {
    VSOut out;
    out.position = uniforms.uMVP * float4(in.position, 1.0);
    out.color = in.color;
    out.uv = in.uv;
    out.worldNormal = (uniforms.uModel * float4(in.normal, 0.0)).xyz;
    return out;
}
)";

const char* kMetalFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 uMVP;
    float4x4 uModel;
    float4 uLightDir;
    float2 uLightParams;
    float4 uBaseColor;
};

struct VSOut {
    float4 position [[position]];
    float4 color;
    float2 uv;
    float3 worldNormal;
};

fragment float4 fragmentShader(VSOut in [[stage_in]],
                               texture2d<float> tex [[texture(0)]],
                               sampler samp [[sampler(0)]],
                               const device Uniforms& uniforms [[buffer(0)]]) {
    float4 albedo = in.color * uniforms.uBaseColor * tex.sample(samp, in.uv);
    float3 n = normalize(in.worldNormal);
    float diffuse = max(dot(n, uniforms.uLightDir.xyz), 0.0);
    float intensity = uniforms.uLightParams.x +
                      uniforms.uLightParams.y * diffuse;
    return float4(albedo.rgb * intensity, albedo.a);
}
)";

const char* kGLSLVertexShader = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform vec4 uLightDir;
uniform vec2 uLightParams;
uniform vec4 uBaseColor;

out vec4 vColor;
out vec2 vUV;
out vec3 vWorldNormal;

void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    vColor = aColor;
    vUV = aUV;
    vWorldNormal = mat3(uModel) * aNormal;
}
)";

const char* kGLSLFragmentShader = R"(
#version 410 core

in vec4 vColor;
in vec2 vUV;
in vec3 vWorldNormal;

uniform sampler2D uTexture;
uniform vec4 uLightDir;
uniform vec2 uLightParams;
uniform vec4 uBaseColor;

out vec4 fragColor;

void main() {
    vec4 albedo = vColor * uBaseColor * texture(uTexture, vUV);
    vec3 n = normalize(vWorldNormal);
    float diffuse = max(dot(n, uLightDir.xyz), 0.0);
    float intensity = uLightParams.x + uLightParams.y * diffuse;
    fragColor = vec4(albedo.rgb * intensity, albedo.a);
}
)";

ion::Matrix4 rotationX(float angle) {
    ion::Matrix4 m = ion::Matrix4::identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    m.m[5] = c;
    m.m[6] = s;
    m.m[9] = -s;
    m.m[10] = c;
    return m;
}

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

void addQuad(std::vector<ion::Vertex>& verts, std::vector<uint16_t>& indices,
             const ion::Vector3& a, const ion::Vector3& b,
             const ion::Vector3& c, const ion::Vector3& d,
             const ion::Vector3& normal) {
    uint16_t start = (uint16_t)verts.size();
    auto push = [&verts, &normal](const ion::Vector3& p) {
        verts.push_back(ion::Vertex{{p.x, p.y, p.z},
                                    {1.0f, 1.0f, 1.0f, 1.0f},
                                    {0.0f, 0.0f},
                                    {normal.x, normal.y, normal.z}});
    };
    push(a);
    push(b);
    push(c);
    push(d);
    indices.push_back(start);
    indices.push_back(start + 1);
    indices.push_back(start + 2);
    indices.push_back(start);
    indices.push_back(start + 2);
    indices.push_back(start + 3);
}

ion::Mesh buildCheckerFloor(ion::Renderer& renderer, int cells,
                            float cellSize) {
    std::vector<ion::Vertex> verts;
    std::vector<uint16_t> indices;
    const ion::Vector3 dark(0.16f, 0.16f, 0.20f);
    const ion::Vector3 light(0.30f, 0.30f, 0.38f);
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
            uint16_t start = (uint16_t)verts.size();
            ion::Vector3 normal(0.0f, 1.0f, 0.0f);
            verts.push_back(ion::Vertex{{p0.x, p0.y, p0.z},
                                        {color.x, color.y, color.z, 1.0f},
                                        {0.0f, 0.0f},
                                        {normal.x, normal.y, normal.z}});
            verts.push_back(ion::Vertex{{p1.x, p1.y, p1.z},
                                        {color.x, color.y, color.z, 1.0f},
                                        {0.0f, 0.0f},
                                        {normal.x, normal.y, normal.z}});
            verts.push_back(ion::Vertex{{p2.x, p2.y, p2.z},
                                        {color.x, color.y, color.z, 1.0f},
                                        {0.0f, 0.0f},
                                        {normal.x, normal.y, normal.z}});
            verts.push_back(ion::Vertex{{p3.x, p3.y, p3.z},
                                        {color.x, color.y, color.z, 1.0f},
                                        {0.0f, 0.0f},
                                        {normal.x, normal.y, normal.z}});
            indices.push_back(start);
            indices.push_back(start + 1);
            indices.push_back(start + 2);
            indices.push_back(start);
            indices.push_back(start + 2);
            indices.push_back(start + 3);
        }
    }
    std::vector<uint32_t> indices32;
    indices32.reserve(indices.size());
    for (uint16_t i : indices) {
        indices32.push_back(i);
    }
    return ion::createMesh(renderer, verts.data(), (uint32_t)verts.size(),
                           indices32.data(), (uint32_t)indices32.size());
}

class AppModel {
public:
    int run(const char* backendName) {
        ion::WindowConfig config;
        config.title = "Ion Model";
        config.appName = "IonModel";
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
        ion::ShaderSource source;
        source.vertex = isMetal ? kMetalVertexShader : kGLSLVertexShader;
        source.fragment = isMetal ? kMetalFragmentShader : kGLSLFragmentShader;
        shader_ = renderer_.createShader(source);
        if (!shader_.isValid()) {
            std::printf("failed to create shader\n");
            renderer_.shutdown();
            window_.destroy();
            return 1;
        }

        uint8_t whitePixels[4] = {255, 255, 255, 255};
        ion::TextureDesc whiteDesc;
        whiteDesc.width = 1;
        whiteDesc.height = 1;
        whiteDesc.filterLinear = true;
        white_ = renderer_.createTexture(whiteDesc, whitePixels);

        camera_.setPosition(ion::Vector3(0.0f, 2.4f, 7.0f));
        camera_.lookAt(ion::Vector3(0.0f, 0.8f, 0.0f));
        camera_.setViewport(window_.width(), window_.height());
        camera_.setFov(1.1f);

        if (!loadModels_()) {
            std::printf("failed to load models\n");
            renderer_.destroyTexture(white_);
            renderer_.destroyShader(shader_);
            renderer_.shutdown();
            window_.destroy();
            return 1;
        }
        floor_ = buildCheckerFloor(renderer_, 12, 1.0f);

        std::printf("Controls: WASD move | mouse drag look | Space/C up/down | Esc quit\n");

        timer_.reset();
        while (window_.isOpen()) {
            window_.pollEvents();
            float deltaTime = timer_.tick();
            if (ion::input::isKeyPressed(ion::Key::Escape)) {
                break;
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

        objModel_.destroy(renderer_);
        gltfModel_.destroy(renderer_);
        glbModel_.destroy(renderer_);
        ion::destroyMesh(renderer_, floor_);
        renderer_.destroyTexture(white_);
        renderer_.destroyShader(shader_);
        renderer_.shutdown();
        window_.destroy();
        std::printf("Model example exited.\n");
        return 0;
    }

private:
    bool loadModels_() {
        const std::string assets = std::string(MODEL_ASSET_DIR);
        bool ok = true;
        ok &= ion::loadModel(renderer_, assets + "/cube.obj", objModel_);
        ok &= ion::loadModel(renderer_, assets + "/cube.gltf", gltfModel_);
        ok &= ion::loadModel(renderer_, assets + "/cube.glb", glbModel_);
        std::printf("OBJ: %zu parts, glTF: %zu parts, GLB: %zu parts\n",
                    objModel_.parts.size(), gltfModel_.parts.size(),
                    glbModel_.parts.size());
        return ok;
    }

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

    void drawModel(const ion::Model& model, const ion::Matrix4& modelMatrix,
                   const ion::Matrix4& viewProjection) {
        for (const ion::ModelPart& part : model.parts) {
            ion::Matrix4 world = modelMatrix * part.transform;
            ion::Matrix4 mvp = viewProjection * world;
            renderer_.setUniform("uMVP", mvp);
            renderer_.setUniform("uModel", world);
            renderer_.setUniform("uBaseColor", ion::Vector4(part.material.baseColor.r, part.material.baseColor.g, part.material.baseColor.b, part.material.baseColor.a));
            renderer_.setTexture(
                0, part.material.texture.isValid() ? part.material.texture
                                                   : white_);
            renderer_.setVertexBuffer(part.mesh.vertexBuffer);
            if (part.mesh.indexBuffer.isValid()) {
                renderer_.setIndexBuffer(part.mesh.indexBuffer);
                renderer_.drawIndexed(part.mesh.indexCount);
            } else {
                renderer_.draw(part.mesh.vertexCount);
            }
        }
    }

    void render() {
        ion::Matrix4 viewProjection = camera_.viewProjection();

        renderer_.beginFrame();
        renderer_.clear(ion::Color(0.06f, 0.07f, 0.10f));
        renderer_.useShader(shader_);
        renderer_.setUniform("uLightDir",
                             ion::Vector3(0.3f, 0.8f, 0.4f).normalized());
        renderer_.setUniform("uLightParams", ion::Vector2(0.18f, 0.82f));

        drawModel(
            objModel_,
            ion::Matrix4::translation(ion::Vector3(-2.4f, 1.0f, 0.0f)) *
                rotationY(angle_ * 0.7f) * rotationX(angle_ * 0.3f),
            viewProjection);

        drawModel(gltfModel_,
                  ion::Matrix4::translation(ion::Vector3(0.0f, 1.0f, 0.0f)) *
                      rotationY(angle_ * 0.5f) * rotationX(angle_ * 0.6f),
                  viewProjection);

        drawModel(glbModel_,
                  ion::Matrix4::translation(ion::Vector3(2.4f, 1.0f, 0.0f)) *
                      rotationY(-angle_ * 0.8f) * rotationX(angle_ * 0.4f),
                  viewProjection);

        ion::ModelPart floorPart;
        floorPart.mesh = floor_;
        drawModelForPart(floorPart, ion::Matrix4::identity(),
                         viewProjection);

        renderer_.endFrame();
    }

    void drawModelForPart(const ion::ModelPart& part,
                          const ion::Matrix4& modelMatrix,
                          const ion::Matrix4& viewProjection) {
        ion::Matrix4 mvp = viewProjection * modelMatrix;
        renderer_.setUniform("uMVP", mvp);
        renderer_.setUniform("uModel", modelMatrix);
        renderer_.setUniform("uBaseColor", ion::Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        renderer_.setTexture(0, white_);
        renderer_.setVertexBuffer(part.mesh.vertexBuffer);
        renderer_.setIndexBuffer(part.mesh.indexBuffer);
        renderer_.drawIndexed(part.mesh.indexCount);
    }

    ion::Window window_;
    ion::Renderer renderer_;
    ion::Shader shader_;
    ion::Texture white_;
    ion::Camera camera_;
    ion::Timer timer_;
    ion::Model objModel_;
    ion::Model gltfModel_;
    ion::Model glbModel_;
    ion::Mesh floor_;
    float angle_ = 0.0f;
    int frames_ = 0;
    float elapsed_ = 0.0f;
};

} // namespace

int main(int argc, char** argv) {
    const char* backend = (argc > 1) ? argv[1] : "metal";
    AppModel app;
    return app.run(backend);
}
