#pragma once

#include <ion/math/Matrix4.hpp>
#include <ion/math/Vector2.hpp>
#include <ion/math/Vector3.hpp>
#include <ion/math/Vector4.hpp>
#include <ion/render/Buffer.hpp>
#include <ion/render/RenderCommand.hpp>
#include <ion/render/RenderTarget.hpp>
#include <ion/render/Shader.hpp>
#include <ion/render/Texture.hpp>

#include <cstdint>
#include <string>

namespace ion {

class Window;

enum class RendererBackend {
    Automatic,
    Metal,
    OpenGL,
    Vulkan,
    Null,
};

struct RendererConfig {
    RendererBackend backend = RendererBackend::Automatic;
    bool vsync = true;
    uint32_t antialiasSamples = 1;
};

struct GPUInfo {
    RendererBackend backend = RendererBackend::Null;
    std::string vendor;
    std::string name;
    std::string driverVersion;
    uint64_t videoMemoryBytes = 0;
};

//
// Per-frame renderer statistics. Reset at beginFrame() and accumulated as
// commands are recorded during the frame; stats() returns the values
// accumulated so far in the current frame.
//
struct RendererStats {
    uint32_t drawCalls = 0;     // Draw/DrawIndexed commands recorded
    uint32_t triangles = 0;     // triangles issued by draw commands
    uint32_t vertices = 0;      // vertices / indexed vertices issued
    uint32_t commandCount = 0;  // total render commands recorded
};

//
// Renderer is the renderer abstraction layer. It records a per-frame list of
// render commands which are executed by the active backend (Metal or OpenGL
// on macOS; other platforms can use the Null backend for testing).
//
// The Null backend requires no window or GPU and accepts a null window
// pointer in initialize().
//
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool initialize(Window* window, const RendererConfig& config = {});
    void shutdown();
    bool isInitialized() const;

    void beginFrame();
    void endFrame();
    void clear(const Color& color);

    // Shaders
    Shader createShader(const ShaderSource& source);
    void destroyShader(Shader& shader);
    void useShader(const Shader& shader);

    // Textures
    Texture createTexture(const TextureDesc& desc, const void* pixels);
    void destroyTexture(Texture& texture);
    void setTexture(int slot, const Texture& texture);

    // Creates a cubemap texture from six RGBA8 face buffers (order: +X, -X,
    // +Y, -Y, +Z, -Z). Each face must contain width*height*4 bytes.
    Texture createCubemap(const TextureDesc& desc, const void* const faces[6]);

    // Render targets (offscreen color and/or depth buffers). After
    // setRenderTarget the renderer draws into the target; subsequent
    // setDefaultRenderTarget() restores the window. The target's color
    // texture can be bound with setTexture (e.g. for post-processing).
    RenderTarget createRenderTarget(const RenderTargetDesc& desc);
    void destroyRenderTarget(RenderTarget& target);
    void setRenderTarget(const RenderTarget& target);
    void setDefaultRenderTarget();

    // Controls depth writes for the current draw state. Disable for
    // background passes such as skyboxes that must not occlude the scene.
    void setDepthWrite(bool enabled);

    // Vertex buffers
    VertexBuffer createVertexBuffer(uint32_t sizeBytes, const void* data);
    void destroyVertexBuffer(VertexBuffer& buffer);
    void updateVertexBuffer(const VertexBuffer& buffer, uint32_t offsetBytes,
                            uint32_t sizeBytes, const void* data);
    void setVertexBuffer(const VertexBuffer& buffer);

    // Index buffers
    IndexBuffer createIndexBuffer(uint32_t count, bool is16Bit,
                                  const void* data);
    void destroyIndexBuffer(IndexBuffer& buffer);
    void updateIndexBuffer(const IndexBuffer& buffer, uint32_t offsetBytes,
                           uint32_t sizeBytes, const void* data);
    void setIndexBuffer(const IndexBuffer& buffer);

    // Uniforms (name must match a uniform declared by the active shader)
    void setUniform(const char* name, float value);
    void setUniform(const char* name, const Vector2& value);
    void setUniform(const char* name, const Vector3& value);
    void setUniform(const char* name, const Vector4& value);
    void setUniform(const char* name, const Matrix4& value);

    // Sets a vec4 array uniform (up to 8 elements, matching kMaxLights).
    void setUniform(const char* name, const float* values,
                    uint32_t vec4Count);

    // Draw
    void draw(uint32_t vertexCount);
    void drawIndexed(uint32_t indexCount, uint32_t startIndex = 0);

    const GPUInfo& gpuInfo() const;

    // Statistics for the current frame (reset each beginFrame).
    const RendererStats& stats() const;

    // Number of commands recorded since the last beginFrame (testing/debug).
    size_t recordedCommandCount() const;
    const RenderCommand* recordedCommands() const;

private:
    class Impl;
    Impl* impl_ = nullptr;
};

} // namespace ion
