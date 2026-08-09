#include <ion/core/Timer.hpp>
#include <ion/platform/Input.hpp>
#include <ion/platform/Window.hpp>
#include <ion/render/Camera.hpp>
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
};

struct VSIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
};

struct VSOut {
    float4 position [[position]];
    float4 color;
};

vertex VSOut vertexShader(VSIn in [[stage_in]],
                          const device Uniforms& uniforms [[buffer(0)]]) {
    VSOut out;
    out.position = uniforms.uMVP * float4(in.position, 1.0);
    out.color = in.color;
    return out;
}
)";

const char* kMetalFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 uMVP;
};

struct VSOut {
    float4 position [[position]];
    float4 color;
};

fragment float4 fragmentShader(VSOut in [[stage_in]]) {
    return in.color;
}
)";

const char* kGLSLVertexShader = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;

uniform mat4 uMVP;

out vec4 vColor;

void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    vColor = aColor;
}
)";

const char* kGLSLFragmentShader = R"(
#version 410 core

in vec4 vColor;

out vec4 fragColor;

void main() {
    fragColor = vColor;
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

struct Mesh {
    ion::VertexBuffer vertexBuffer;
    ion::IndexBuffer indexBuffer;
    uint32_t indexCount = 0;
};

const ion::Vector3 kLightDir = ion::Vector3(0.3f, 0.8f, 0.4f).normalized();
constexpr float kAmbient = 0.18f;
constexpr float kDiffuse = 0.82f;

ion::Vector4 lit(const ion::Vector4& base, const ion::Vector3& normal) {
    float lambert = std::max(ion::Vector3::dot(normal, kLightDir), 0.0f);
    float f = kAmbient + lambert * kDiffuse;
    return ion::Vector4(base.x * f, base.y * f, base.z * f, 1.0f);
}

ion::Vector3 orientedNormal(const ion::Vector3& a, const ion::Vector3& b,
                            const ion::Vector3& c, const ion::Vector3& hint) {
    ion::Vector3 n = ion::Vector3::cross(b - a, c - a).normalized();
    if (ion::Vector3::dot(n, hint) < 0.0f) {
        n = n * -1.0f;
    }
    return n;
}

void addTri(std::vector<ion::Vertex>& verts, std::vector<uint16_t>& indices,
            const ion::Vector3& a, const ion::Vector3& b, const ion::Vector3& c,
            const ion::Vector4& base, const ion::Vector3& hint) {
    ion::Vector3 n = orientedNormal(a, b, c, hint);
    ion::Vector4 color = lit(base, n);
    uint16_t start = (uint16_t)verts.size();
    verts.push_back(
        ion::Vertex{{a.x, a.y, a.z}, {color.x, color.y, color.z, color.w},
                    {0.0f, 0.0f}});
    verts.push_back(
        ion::Vertex{{b.x, b.y, b.z}, {color.x, color.y, color.z, color.w},
                    {0.0f, 0.0f}});
    verts.push_back(
        ion::Vertex{{c.x, c.y, c.z}, {color.x, color.y, color.z, color.w},
                    {0.0f, 0.0f}});
    indices.push_back(start);
    indices.push_back(start + 1);
    indices.push_back(start + 2);
}

void addQuad(std::vector<ion::Vertex>& verts, std::vector<uint16_t>& indices,
             const ion::Vector3& a, const ion::Vector3& b,
             const ion::Vector3& c, const ion::Vector3& d,
             const ion::Vector4& base, const ion::Vector3& hint) {
    addTri(verts, indices, a, b, c, base, hint);
    addTri(verts, indices, a, c, d, base, hint);
}

Mesh createMesh(ion::Renderer& renderer, const std::vector<ion::Vertex>& verts,
                const std::vector<uint16_t>& indices) {
    Mesh mesh;
    mesh.vertexBuffer = renderer.createVertexBuffer(
        (uint32_t)(verts.size() * sizeof(ion::Vertex)), verts.data());
    mesh.indexBuffer =
        renderer.createIndexBuffer((uint32_t)indices.size(), true, indices.data());
    mesh.indexCount = (uint32_t)indices.size();
    return mesh;
}

Mesh buildCube(ion::Renderer& renderer, float half,
               const ion::Vector4 colors[6]) {
    std::vector<ion::Vertex> verts;
    std::vector<uint16_t> indices;
    const float h = half;
    const ion::Vector3 normal[6] = {
        {1.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f}};
    const ion::Vector3 corners[6][4] = {
        {{h, -h, -h}, {h, h, -h}, {h, h, h}, {h, -h, h}},
        {{-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-h, -h, -h}},
        {{-h, h, -h}, {h, h, -h}, {h, h, h}, {-h, h, h}},
        {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}},
        {{h, -h, h}, {h, h, h}, {-h, h, h}, {-h, -h, h}},
        {{-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {h, -h, -h}},
    };
    for (int f = 0; f < 6; f++) {
        addQuad(verts, indices, corners[f][0], corners[f][1], corners[f][2],
                corners[f][3], colors[f], normal[f]);
    }
    return createMesh(renderer, verts, indices);
}

Mesh buildPyramid(ion::Renderer& renderer, float base, float height,
                  const ion::Vector4& sideColor,
                  const ion::Vector4& baseColor) {
    std::vector<ion::Vertex> verts;
    std::vector<uint16_t> indices;
    const ion::Vector3 apex(0.0f, height, 0.0f);
    const ion::Vector3 corners[4] = {
        {base, 0.0f, base}, {-base, 0.0f, base}, {-base, 0.0f, -base},
        {base, 0.0f, -base}};
    for (int i = 0; i < 4; i++) {
        const ion::Vector3& c0 = corners[i];
        const ion::Vector3& c1 = corners[(i + 1) % 4];
        ion::Vector3 hint((c0.x + c1.x) * 0.5f, 0.0f, (c0.z + c1.z) * 0.5f);
        addTri(verts, indices, apex, c0, c1, sideColor, hint);
    }
    addQuad(verts, indices, corners[0], corners[1], corners[2], corners[3],
            baseColor, ion::Vector3(0.0f, -1.0f, 0.0f));
    return createMesh(renderer, verts, indices);
}

Mesh buildCheckerFloor(ion::Renderer& renderer, int cells, float cellSize) {
    std::vector<ion::Vertex> verts;
    std::vector<uint16_t> indices;
    const ion::Vector4 dark(0.16f, 0.16f, 0.20f, 1.0f);
    const ion::Vector4 light(0.24f, 0.24f, 0.30f, 1.0f);
    const float half = cellSize * (float)cells * 0.5f;
    for (int i = 0; i < cells; i++) {
        for (int j = 0; j < cells; j++) {
            float x0 = -half + (float)i * cellSize;
            float z0 = -half + (float)j * cellSize;
            ion::Vector3 p0(x0, 0.0f, z0);
            ion::Vector3 p1(x0 + cellSize, 0.0f, z0);
            ion::Vector3 p2(x0 + cellSize, 0.0f, z0 + cellSize);
            ion::Vector3 p3(x0, 0.0f, z0 + cellSize);
            const ion::Vector4& color = ((i + j) % 2 == 0) ? dark : light;
            addQuad(verts, indices, p0, p1, p2, p3, color,
                    ion::Vector3(0.0f, 1.0f, 0.0f));
        }
    }
    return createMesh(renderer, verts, indices);
}

class App3D {
public:
    int run(const char* backendName) {
        ion::WindowConfig config;
        config.title = "Ion 3D";
        config.appName = "Ion3D";
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

        camera_.setPosition(ion::Vector3(0.0f, 2.4f, 7.0f));
        camera_.lookAt(ion::Vector3(0.0f, 0.8f, 0.0f));
        camera_.setViewport(window_.width(), window_.height());
        camera_.setFov(1.1f);

        buildScene_();

        std::printf("Controls: WASD move | mouse drag look | Space/C move up/down | Esc quit\n");

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

        destroyScene_();
        renderer_.destroyShader(shader_);
        renderer_.shutdown();
        window_.destroy();
        std::printf("3D example exited.\n");
        return 0;
    }

private:
    void buildScene_() {
        const ion::Vector4 cubeColors[6] = {
            {0.85f, 0.24f, 0.22f, 1.0f},  // +X red
            {0.94f, 0.64f, 0.12f, 1.0f},  // -X orange
            {0.24f, 0.78f, 0.36f, 1.0f},  // +Y green
            {0.55f, 0.36f, 0.70f, 1.0f},  // -Y purple
            {0.20f, 0.55f, 0.94f, 1.0f},  // +Z blue
            {0.30f, 0.85f, 0.86f, 1.0f},  // -Z cyan
        };
        cube_ = buildCube(renderer_, 0.8f, cubeColors);

        pyramid_ = buildPyramid(renderer_, 0.7f, 1.5f,
                                ion::Vector4(0.92f, 0.85f, 0.25f, 1.0f),
                                ion::Vector4(0.42f, 0.28f, 0.10f, 1.0f));

        floor_ = buildCheckerFloor(renderer_, 12, 1.0f);

        const ion::Vector4 decor[4] = {
            {0.20f, 0.70f, 0.45f, 1.0f}, {0.75f, 0.35f, 0.65f, 1.0f},
            {0.95f, 0.45f, 0.20f, 1.0f}, {0.30f, 0.60f, 0.85f, 1.0f},
        };
        decor_ = buildCube(renderer_, 0.3f, decor);

        decorPositions_[0] = ion::Vector3(1.9f, 0.3f, -1.6f);
        decorPositions_[1] = ion::Vector3(-1.8f, 0.3f, -2.4f);
        decorPositions_[2] = ion::Vector3(2.4f, 0.3f, 1.2f);
        decorPositions_[3] = ion::Vector3(-2.5f, 0.3f, 0.6f);
    }

    void destroyScene_() {
        renderer_.destroyVertexBuffer(cube_.vertexBuffer);
        renderer_.destroyIndexBuffer(cube_.indexBuffer);
        renderer_.destroyVertexBuffer(pyramid_.vertexBuffer);
        renderer_.destroyIndexBuffer(pyramid_.indexBuffer);
        renderer_.destroyVertexBuffer(floor_.vertexBuffer);
        renderer_.destroyIndexBuffer(floor_.indexBuffer);
        renderer_.destroyVertexBuffer(decor_.vertexBuffer);
        renderer_.destroyIndexBuffer(decor_.indexBuffer);
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

    void drawMesh(const Mesh& mesh, const ion::Matrix4& model,
                  const ion::Matrix4& viewProjection) {
        ion::Matrix4 mvp = viewProjection * model;
        renderer_.setUniform("uMVP", mvp);
        renderer_.setVertexBuffer(mesh.vertexBuffer);
        renderer_.setIndexBuffer(mesh.indexBuffer);
        renderer_.drawIndexed(mesh.indexCount);
    }

    void render() {
        ion::Matrix4 viewProjection = camera_.viewProjection();

        renderer_.beginFrame();
        renderer_.clear(ion::Color(0.06f, 0.07f, 0.10f));
        renderer_.useShader(shader_);

        drawMesh(floor_, ion::Matrix4::identity(), viewProjection);

        ion::Matrix4 cubeModel =
            ion::Matrix4::translation(ion::Vector3(0.0f, 0.8f, 0.0f)) *
            rotationY(angle_ * 0.6f) * rotationX(angle_ * 0.35f);
        drawMesh(cube_, cubeModel, viewProjection);

        ion::Matrix4 pyramidModel =
            ion::Matrix4::translation(ion::Vector3(1.6f, 0.0f, -1.0f)) *
            rotationY(-angle_ * 0.5f);
        drawMesh(pyramid_, pyramidModel, viewProjection);

        for (int i = 0; i < 4; i++) {
            drawMesh(decor_,
                     ion::Matrix4::translation(decorPositions_[i]),
                     viewProjection);
        }

        renderer_.endFrame();
    }

    ion::Window window_;
    ion::Renderer renderer_;
    ion::Shader shader_;
    ion::Camera camera_;
    ion::Timer timer_;
    Mesh cube_;
    Mesh pyramid_;
    Mesh floor_;
    Mesh decor_;
    ion::Vector3 decorPositions_[4];
    float angle_ = 0.0f;
    int frames_ = 0;
    float elapsed_ = 0.0f;
};

} // namespace

int main(int argc, char** argv) {
    const char* backend = (argc > 1) ? argv[1] : "metal";
    App3D app;
    return app.run(backend);
}
