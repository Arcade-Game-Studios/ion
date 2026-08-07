#include <ion/core/Timer.hpp>
#include <ion/core/Version.hpp>
#include <ion/platform/Input.hpp>
#include <ion/platform/Window.hpp>
#include <ion/render/Camera.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/Vertex.hpp>

#include <cstdio>
#include <cstring>

namespace {

const char* kMetalVertexShader = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 uMVP;
    float4 tint;
};

struct VSIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
};

struct VSOut {
    float4 position [[position]];
    float4 color;
    float2 uv;
};

vertex VSOut vertexShader(VSIn in [[stage_in]],
                          const device Uniforms& uniforms [[buffer(0)]]) {
    VSOut out;
    out.position = uniforms.uMVP * float4(in.position, 1.0);
    out.color = in.color;
    out.uv = in.uv;
    return out;
}
)";

const char* kMetalFragmentShader = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 uMVP;
    float4 tint;
};

struct VSOut {
    float4 position [[position]];
    float4 color;
    float2 uv;
};

fragment float4 fragmentShader(VSOut in [[stage_in]],
                               const device Uniforms& uniforms [[buffer(0)]],
                               texture2d<float> tex [[texture(0)]],
                               sampler samp [[sampler(0)]]) {
    return in.color * uniforms.tint * tex.sample(samp, in.uv);
}
)";

const char* kGLSLVertexShader = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;

uniform mat4 uMVP;
uniform vec4 tint;

out vec4 vColor;
out vec2 vUV;

void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    vColor = aColor;
    vUV = aUV;
}
)";

const char* kGLSLFragmentShader = R"(
#version 410 core

in vec4 vColor;
in vec2 vUV;

uniform vec4 tint;
uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    fragColor = vColor * tint * texture(uTexture, vUV);
}
)";

ion::Matrix4 rotationZ(float angle) {
    ion::Matrix4 m = ion::Matrix4::identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    m.m[0] = c;
    m.m[4] = -s;
    m.m[1] = s;
    m.m[5] = c;
    return m;
}

class RenderApp {
public:
    int run(const char* backendName) {
        ion::WindowConfig config;
        config.title = "Ion 0.1.1 Pure Metal";
        config.appName = "IonRender";
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
        std::printf("Driver: %s | Video memory: %.2f GB\n",
                    gpu.driverVersion.c_str(),
                    (double)gpu.videoMemoryBytes / (1024.0 * 1024.0 * 1024.0));

        if (gpu.backend == ion::RendererBackend::Null) {
            std::printf("Running with the Null backend: no draw calls executed\n");
        }

        bool isMetal = gpu.backend == ion::RendererBackend::Metal;
        ion::ShaderSource source;
        source.vertex = isMetal ? kMetalVertexShader : kGLSLVertexShader;
        source.fragment =
            isMetal ? kMetalFragmentShader : kGLSLFragmentShader;
        shader_ = renderer_.createShader(source);
        if (!shader_.isValid()) {
            std::printf("failed to create shader\n");
            renderer_.shutdown();
            window_.destroy();
            return 1;
        }

        camera_.setPosition(ion::Vector3(0.0f, 0.0f, 3.0f));
        camera_.lookAt(ion::Vector3(0.0f, 0.0f, 0.0f), ion::Vector3(0, 1, 0));

        createTriangle_();
        createQuad_();
        createTexture_();

        timer_.reset();
        while (window_.isOpen()) {
            window_.pollEvents();
            float deltaTime = timer_.tick();
            if (ion::input::isKeyPressed(ion::Key::Escape)) {
                break;
            }

            frames_++;
            elapsed_ += deltaTime;
            if (frames_ % 60 == 0) {
                std::printf("avg %.2f ms/frame, window %ux%u\n",
                            elapsed_ / (float)frames_ * 1000.0f,
                            window_.width(), window_.height());
            }

            angle_ += deltaTime;

            float aspect = (float)window_.width() /
                           (float)std::max(1u, window_.height());
            camera_.setPerspective(1.2f, aspect, 0.1f, 100.0f);

            render();
        }

        renderer_.destroyShader(shader_);
        renderer_.destroyVertexBuffer(triangleVB_);
        renderer_.destroyVertexBuffer(quadVB_);
        renderer_.destroyIndexBuffer(quadIB_);
        renderer_.destroyTexture(checker_);
        renderer_.shutdown();
        window_.destroy();
        std::printf("Render example exited.\n");
        return 0;
    }

private:
    void createTriangle_() {
        const ion::Vertex triangle[3] = {
            {{-0.8f, -0.8f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{0.8f, -0.8f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{0.0f, 0.8f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        };
        triangleVB_ =
            renderer_.createVertexBuffer(sizeof(triangle), triangle);
    }

    void createQuad_() {
        const float half = 0.8f;
        const ion::Vertex quad[4] = {
            {{-half, -half, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
            {{half, -half, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
            {{half, half, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
            {{-half, half, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        };
        const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        quadVB_ = renderer_.createVertexBuffer(sizeof(quad), quad);
        quadIB_ = renderer_.createIndexBuffer(6, true, indices);
    }

    void createTexture_() {
        constexpr int size = 8;
        uint32_t pixels[size * size];
        const uint32_t colors[4] = {0xFFFFFFFF, 0xFF0000FF, 0xFF00FF00,
                                    0xFFFF0000};
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                int block = (x / 4) + 2 * (y / 4);
                pixels[y * size + x] = colors[block];
            }
        }
        ion::TextureDesc desc;
        desc.width = size;
        desc.height = size;
        desc.filterLinear = true;
        checker_ = renderer_.createTexture(desc, pixels);
    }

    void render() {
        ion::Matrix4 projection = camera_.projection();
        ion::Matrix4 view = camera_.view();

        renderer_.beginFrame();
        renderer_.clear(ion::Color(0.08f, 0.09f, 0.12f));

        renderer_.useShader(shader_);
        renderer_.setTexture(0, checker_);

        ion::Matrix4 triModel =
            rotationZ(angle_ * 0.8f) *
            ion::Matrix4::translation(ion::Vector3(-1.2f, 0.0f, 0.0f));
        ion::Matrix4 triMVP = projection * (view * triModel);
        renderer_.setUniform("uMVP", triMVP);
        renderer_.setUniform("tint", ion::Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        renderer_.setVertexBuffer(triangleVB_);
        renderer_.draw(3);

        ion::Matrix4 quadModel =
            rotationZ(-angle_) *
            ion::Matrix4::translation(ion::Vector3(1.2f, 0.0f, 0.0f));
        ion::Matrix4 quadMVP = projection * (view * quadModel);
        renderer_.setUniform("uMVP", quadMVP);
        renderer_.setUniform("tint", ion::Vector4(1.0f, 0.6f, 0.2f, 1.0f));
        renderer_.setVertexBuffer(quadVB_);
        renderer_.setIndexBuffer(quadIB_);
        renderer_.drawIndexed(6);

        renderer_.endFrame();
    }

    ion::Window window_;
    ion::Renderer renderer_;
    ion::Camera camera_;
    ion::Shader shader_;
    ion::VertexBuffer triangleVB_;
    ion::VertexBuffer quadVB_;
    ion::IndexBuffer quadIB_;
    ion::Texture checker_;
    ion::Timer timer_;
    float angle_ = 0.0f;
    int frames_ = 0;
    float elapsed_ = 0.0f;
};

} // namespace

int main(int argc, char** argv) {
    const char* backend = argc > 1 ? argv[1] : "auto";
    std::printf("Ion Engine v%s initializing\n", ion::VERSION_STRING);
    RenderApp app;
    return app.run(backend);
}
