#pragma once

//
// Internal renderer backend interface. Implemented per platform by the
// Metal and OpenGL backends. Consumers of the public API should not include
// this header directly.
//

#include <ion/render/RenderCommand.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/Texture.hpp>

namespace ion {

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual bool initialize(void* nativeView, const RendererConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual GPUInfo gpuInfo() const = 0;

    virtual void beginFrame(uint32_t width, uint32_t height) = 0;
    virtual void endFrame() = 0;
    virtual void execute(const RenderCommand& command) = 0;

    virtual uint64_t createShader(const char* vertexSource,
                                  const char* fragmentSource) = 0;
    virtual void destroyShader(uint64_t id) = 0;

    virtual uint64_t createTexture(const TextureDesc& desc,
                                   const void* pixels) = 0;
    virtual void destroyTexture(uint64_t id) = 0;

    virtual uint64_t createVertexBuffer(uint32_t sizeBytes,
                                        const void* data) = 0;
    virtual void destroyVertexBuffer(uint64_t id) = 0;
    virtual void updateVertexBuffer(uint64_t id, uint32_t offsetBytes,
                                    uint32_t sizeBytes, const void* data) = 0;

    virtual uint64_t createIndexBuffer(uint32_t count, bool is16Bit,
                                       const void* data) = 0;
    virtual void destroyIndexBuffer(uint64_t id) = 0;
};

} // namespace ion
