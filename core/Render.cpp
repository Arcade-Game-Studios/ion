#include <ion/render/Renderer.hpp>

#include <ion/core/Log.hpp>
#include <ion/platform/Window.hpp>

#include "RenderBackend.hpp"

#include <cstring>
#include <vector>

namespace ion {

#ifdef __APPLE__
extern RenderBackend* createMetalBackend();
extern RenderBackend* createOpenGLBackend();
#endif
extern RenderBackend* createNullBackend();

namespace {

RenderBackend* createBackend(RendererBackend backend) {
#ifdef __APPLE__
    if (backend == RendererBackend::Metal) {
        return createMetalBackend();
    }
    if (backend == RendererBackend::OpenGL) {
        return createOpenGLBackend();
    }
    if (backend == RendererBackend::Automatic) {
        RenderBackend* metal = createMetalBackend();
        if (metal != nullptr) {
            return metal;
        }
        return createOpenGLBackend();
    }
#else
    if (backend == RendererBackend::Metal || backend == RendererBackend::OpenGL) {
        ION_LOG_WARN("Renderer backend not supported on this platform, "
                     "falling back to the Null backend");
        return createNullBackend();
    }
#endif
    return createNullBackend();
}

} // namespace

class Renderer::Impl {
public:
    ~Impl() {
        shutdown();
    }

    bool initialize(Window* window, const RendererConfig& config) {
        if (backend) {
            return true;
        }

        RendererBackend requested = config.backend;
        if (requested == RendererBackend::Automatic) {
#ifdef __APPLE__
            requested = RendererBackend::Metal;
#else
            requested = RendererBackend::Null;
#endif
        }

        backend = createBackend(requested);
        if (!backend) {
            ION_LOG_ERROR("Failed to create render backend");
            return false;
        }

        if (requested != RendererBackend::Null && !window) {
            ION_LOG_ERROR("Renderer requires a window");
            shutdown();
            return false;
        }

        void* nativeView = window ? window->nativeHandle() : nullptr;
        if (!backend->initialize(nativeView, config)) {
            ION_LOG_ERROR("Renderer backend initialization failed");
            shutdown();
            return false;
        }

        this->window = window;
        gpuInfo = backend->gpuInfo();
        commands.reserve(256);
        ION_LOG_INFO("Renderer initialized: %s %s (vendor: %s)",
                     gpuInfo.name.c_str(), gpuInfo.driverVersion.c_str(),
                     gpuInfo.vendor.c_str());
        initialized = true;
        return true;
    }

    void shutdown() {
        if (backend) {
            backend->shutdown();
            delete backend;
            backend = nullptr;
        }
        initialized = false;
    }

    void beginFrame() {
        commands.clear();
        if (!backend || !initialized) {
            return;
        }
        uint32_t width = window ? window->width() : 0;
        uint32_t height = window ? window->height() : 0;
        backend->beginFrame(width, height);
    }

    void endFrame() {
        if (!backend || !initialized) {
            return;
        }
        for (const RenderCommand& command : commands) {
            backend->execute(command);
        }
        commands.clear();
        backend->endFrame();
    }

    void record(const RenderCommand& command) {
        commands.push_back(command);
    }

    Window* window = nullptr;
    RenderBackend* backend = nullptr;
    GPUInfo gpuInfo;
    std::vector<RenderCommand> commands;
    bool initialized = false;
};

Renderer::Renderer() : impl_(new Impl()) {
}

Renderer::~Renderer() {
    delete impl_;
}

Renderer::Renderer(Renderer&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

Renderer& Renderer::operator=(Renderer&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

bool Renderer::initialize(Window* window, const RendererConfig& config) {
    if (!impl_) {
        return false;
    }
    return impl_->initialize(window, config);
}

void Renderer::shutdown() {
    if (impl_) {
        impl_->shutdown();
    }
}

bool Renderer::isInitialized() const {
    return impl_ && impl_->initialized;
}

void Renderer::beginFrame() {
    if (impl_) {
        impl_->beginFrame();
    }
}

void Renderer::endFrame() {
    if (impl_) {
        impl_->endFrame();
    }
}

void Renderer::clear(const Color& color) {
    if (!impl_) {
        return;
    }
    RenderCommand command;
    command.type = RenderCommandType::Clear;
    command.clearColor = color;
    impl_->record(command);
}

Shader Renderer::createShader(const ShaderSource& source) {
    if (!impl_ || !impl_->backend) {
        return {};
    }
    Shader shader;
    shader.id = impl_->backend->createShader(source.vertex, source.fragment);
    return shader;
}

void Renderer::destroyShader(Shader& shader) {
    if (!impl_ || !impl_->backend || !shader.isValid()) {
        return;
    }
    impl_->backend->destroyShader(shader.id);
    shader.id = 0;
}

void Renderer::useShader(const Shader& shader) {
    if (!impl_ || !shader.isValid()) {
        return;
    }
    RenderCommand command;
    command.type = RenderCommandType::UseShader;
    command.shaderId = shader.id;
    impl_->record(command);
}

Texture Renderer::createTexture(const TextureDesc& desc, const void* pixels) {
    if (!impl_ || !impl_->backend) {
        return {};
    }
    Texture texture;
    texture.desc = desc;
    texture.id = impl_->backend->createTexture(desc, pixels);
    return texture;
}

void Renderer::destroyTexture(Texture& texture) {
    if (!impl_ || !impl_->backend || !texture.isValid()) {
        return;
    }
    impl_->backend->destroyTexture(texture.id);
    texture.id = 0;
}

void Renderer::setTexture(int slot, const Texture& texture) {
    if (!impl_ || !texture.isValid()) {
        return;
    }
    RenderCommand command;
    command.type = RenderCommandType::SetTexture;
    command.textureSlot = slot;
    command.textureId = texture.id;
    impl_->record(command);
}

VertexBuffer Renderer::createVertexBuffer(uint32_t sizeBytes, const void* data) {
    if (!impl_ || !impl_->backend) {
        return {};
    }
    VertexBuffer buffer;
    buffer.size = sizeBytes;
    buffer.id = impl_->backend->createVertexBuffer(sizeBytes, data);
    return buffer;
}

void Renderer::destroyVertexBuffer(VertexBuffer& buffer) {
    if (!impl_ || !impl_->backend || !buffer.isValid()) {
        return;
    }
    impl_->backend->destroyVertexBuffer(buffer.id);
    buffer.id = 0;
}

void Renderer::updateVertexBuffer(const VertexBuffer& buffer,
                                  uint32_t offsetBytes, uint32_t sizeBytes,
                                  const void* data) {
    if (!impl_ || !impl_->backend || !buffer.isValid()) {
        return;
    }
    impl_->backend->updateVertexBuffer(buffer.id, offsetBytes, sizeBytes, data);
}

void Renderer::setVertexBuffer(const VertexBuffer& buffer) {
    if (!impl_ || !buffer.isValid()) {
        return;
    }
    RenderCommand command;
    command.type = RenderCommandType::BindVertexBuffer;
    command.vertexBufferId = buffer.id;
    impl_->record(command);
}

IndexBuffer Renderer::createIndexBuffer(uint32_t count, bool is16Bit,
                                        const void* data) {
    if (!impl_ || !impl_->backend) {
        return {};
    }
    IndexBuffer buffer;
    buffer.count = count;
    buffer.is16Bit = is16Bit;
    buffer.id = impl_->backend->createIndexBuffer(count, is16Bit, data);
    return buffer;
}

void Renderer::destroyIndexBuffer(IndexBuffer& buffer) {
    if (!impl_ || !impl_->backend || !buffer.isValid()) {
        return;
    }
    impl_->backend->destroyIndexBuffer(buffer.id);
    buffer.id = 0;
}

void Renderer::setIndexBuffer(const IndexBuffer& buffer) {
    if (!impl_ || !buffer.isValid()) {
        return;
    }
    RenderCommand command;
    command.type = RenderCommandType::BindIndexBuffer;
    command.indexBufferId = buffer.id;
    command.count = buffer.count;
    impl_->record(command);
}

void Renderer::setUniform(const char* name, float value) {
    RenderCommand command;
    command.type = RenderCommandType::SetUniformFloat;
    command.uniformName = name;
    fillUniformData(command, &value, sizeof(value));
    impl_->record(command);
}

void Renderer::setUniform(const char* name, const Vector2& value) {
    RenderCommand command;
    command.type = RenderCommandType::SetUniformVec2;
    command.uniformName = name;
    fillUniformData(command, &value, sizeof(value));
    impl_->record(command);
}

void Renderer::setUniform(const char* name, const Vector3& value) {
    RenderCommand command;
    command.type = RenderCommandType::SetUniformVec3;
    command.uniformName = name;
    fillUniformData(command, &value, sizeof(value));
    impl_->record(command);
}

void Renderer::setUniform(const char* name, const Vector4& value) {
    RenderCommand command;
    command.type = RenderCommandType::SetUniformVec4;
    command.uniformName = name;
    fillUniformData(command, &value, sizeof(value));
    impl_->record(command);
}

void Renderer::setUniform(const char* name, const Matrix4& value) {
    RenderCommand command;
    command.type = RenderCommandType::SetUniformMat4;
    command.uniformName = name;
    fillUniformData(command, value.m, sizeof(value.m));
    impl_->record(command);
}

void Renderer::draw(uint32_t vertexCount) {
    RenderCommand command;
    command.type = RenderCommandType::Draw;
    command.count = vertexCount;
    impl_->record(command);
}

void Renderer::drawIndexed(uint32_t indexCount, uint32_t startIndex) {
    RenderCommand command;
    command.type = RenderCommandType::DrawIndexed;
    command.count = indexCount;
    command.startIndex = startIndex;
    impl_->record(command);
}

const GPUInfo& Renderer::gpuInfo() const {
    static const GPUInfo empty;
    return impl_ ? impl_->gpuInfo : empty;
}

size_t Renderer::recordedCommandCount() const {
    return impl_ ? impl_->commands.size() : 0;
}

const RenderCommand* Renderer::recordedCommands() const {
    return impl_ ? impl_->commands.data() : nullptr;
}

namespace {

//
// Null backend: executes recorded commands as no-ops. Used for testing the
// command recording path without a window or GPU.
//
class NullBackend : public RenderBackend {
public:
    bool initialize(void*, const RendererConfig&) override {
        return true;
    }

    void shutdown() override {}

    GPUInfo gpuInfo() const override {
        GPUInfo info;
        info.backend = RendererBackend::Null;
        info.vendor = "Ion";
        info.name = "Null Renderer";
        info.driverVersion = "0.1";
        return info;
    }

    void beginFrame(uint32_t, uint32_t) override {}
    void endFrame() override {}

    void execute(const RenderCommand&) override {}

    uint64_t createShader(const char*, const char*) override {
        return 1;
    }

    void destroyShader(uint64_t) override {}

    uint64_t createTexture(const TextureDesc&, const void*) override {
        return 1;
    }

    void destroyTexture(uint64_t) override {}

    uint64_t createVertexBuffer(uint32_t, const void*) override {
        return 1;
    }

    void destroyVertexBuffer(uint64_t) override {}

    void updateVertexBuffer(uint64_t, uint32_t, uint32_t, const void*) override {
    }

    uint64_t createIndexBuffer(uint32_t, bool, const void*) override {
        return 1;
    }

    void destroyIndexBuffer(uint64_t) override {}
};

} // namespace

RenderBackend* createNullBackend() {
    return new NullBackend();
}

} // namespace ion
