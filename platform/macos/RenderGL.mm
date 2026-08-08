#include "../../core/RenderBackend.hpp"

#include <ion/core/Log.hpp>
#include <ion/math/Matrix4.hpp>

#include <Cocoa/Cocoa.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#ifndef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9049
#endif

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace ion {

namespace {

struct TextureData {
    GLuint texture = 0;
    bool filterLinear = true;
};

GLenum glFormat(TextureFormat format) {
    switch (format) {
    case TextureFormat::RGBA8:
        return GL_RGBA8;
    case TextureFormat::RGBA16F:
        return GL_RGBA16F;
    case TextureFormat::Depth:
        return GL_DEPTH_COMPONENT32F;
    }
    return GL_RGBA8;
}

} // namespace

class OpenGLBackend : public RenderBackend {
public:
    ~OpenGLBackend() override {
        shutdown();
    }

    bool initialize(void* nativeView, const RendererConfig& config) override {
        if (initialized_) {
            return true;
        }
        @autoreleasepool {
            view_ = (__bridge NSView*)nativeView;
            if (!view_) {
                ION_LOG_ERROR("OpenGL: null native view");
                return false;
            }
            config_ = config;

            std::vector<NSOpenGLPixelFormatAttribute> attrs;
            attrs.push_back(NSOpenGLPFAOpenGLProfile);
            attrs.push_back(NSOpenGLProfileVersion3_2Core);
            attrs.push_back(NSOpenGLPFADoubleBuffer);
            attrs.push_back(NSOpenGLPFAColorSize);
            attrs.push_back(24);
            attrs.push_back(NSOpenGLPFAAlphaSize);
            attrs.push_back(8);
            attrs.push_back(NSOpenGLPFADepthSize);
            attrs.push_back(24);
            if (config_.antialiasSamples > 1) {
                attrs.push_back(NSOpenGLPFAMultisample);
                attrs.push_back(NSOpenGLPFASampleBuffers);
                attrs.push_back(1);
                attrs.push_back(NSOpenGLPFASamples);
                attrs.push_back((NSOpenGLPixelFormatAttribute)config_
                                    .antialiasSamples);
                attrs.push_back(NSOpenGLPFANoRecovery);
            }
            attrs.push_back(0);

            NSOpenGLPixelFormat* pixelFormat =
                [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs.data()];
            if (!pixelFormat) {
                ION_LOG_ERROR("OpenGL: failed to create pixel format");
                return false;
            }
            context_ = [[NSOpenGLContext alloc] initWithFormat:pixelFormat
                                                 shareContext:nil];
            if (!context_) {
                ION_LOG_ERROR("OpenGL: failed to create context");
                return false;
            }
            [context_ setView:view_];
            [view_ setWantsBestResolutionOpenGLSurface:YES];
            [context_ makeCurrentContext];

            glGenVertexArrays(1, &vao_);
            glBindVertexArray(vao_);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (config_.antialiasSamples > 1) {
                glEnable(GL_MULTISAMPLE);
            }
        }
        initialized_ = true;
        return true;
    }

    void shutdown() override {
        @autoreleasepool {
            for (auto& entry : shaders_) {
                glDeleteProgram(entry.second);
            }
            shaders_.clear();
            for (auto& entry : textures_) {
                glDeleteTextures(1, &entry.second.texture);
            }
            textures_.clear();
            for (auto& entry : vertexBuffers_) {
                GLuint buffer = entry.second;
                glDeleteBuffers(1, &buffer);
            }
            vertexBuffers_.clear();
            for (auto& entry : indexBuffers_) {
                GLuint buffer = entry.second;
                glDeleteBuffers(1, &buffer);
            }
            indexBuffers_.clear();
            if (vao_) {
                glDeleteVertexArrays(1, &vao_);
                vao_ = 0;
            }
            context_ = nil;
            view_ = nil;
        }
        initialized_ = false;
    }

    GPUInfo gpuInfo() const override {
        GPUInfo info;
        info.backend = RendererBackend::OpenGL;
        @autoreleasepool {
            if (context_) {
                [context_ makeCurrentContext];
                const GLubyte* renderer = glGetString(GL_RENDERER);
                const GLubyte* vendor = glGetString(GL_VENDOR);
                const GLubyte* version = glGetString(GL_VERSION);
                if (renderer) {
                    info.name = (const char*)renderer;
                }
                if (vendor) {
                    info.vendor = (const char*)vendor;
                }
                if (version) {
                    info.driverVersion = (const char*)version;
                }
                GLint memoryMB = 0;
                const GLubyte* extensions = glGetString(GL_EXTENSIONS);
                if (extensions && strstr((const char*)extensions,
                                         "GL_NVX_gpu_memory_info")) {
                    glGetIntegerv(
                        GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX,
                        &memoryMB);
                }
                info.videoMemoryBytes = (uint64_t)memoryMB * 1024 * 1024;
            }
        }
        return info;
    }

    void beginFrame(uint32_t, uint32_t) override {
        @autoreleasepool {
            [context_ makeCurrentContext];
            NSRect backing = [view_ convertRectToBacking:view_.bounds];
            viewportWidth_ = (GLsizei)backing.size.width;
            viewportHeight_ = (GLsizei)backing.size.height;
            glViewport(0, 0, viewportWidth_, viewportHeight_);
        }
    }

    void endFrame() override {
        @autoreleasepool {
            [context_ makeCurrentContext];
            for (const RenderCommand& command : pendingCommands_) {
                executeCommand_(command);
            }
            pendingCommands_.clear();
            [context_ flushBuffer];
        }
    }

    void execute(const RenderCommand& command) override {
        pendingCommands_.push_back(command);
    }

    uint64_t createShader(const char* vertexSource,
                          const char* fragmentSource) override {
        @autoreleasepool {
            GLuint vs = compileShader_(GL_VERTEX_SHADER, vertexSource);
            GLuint fs = compileShader_(GL_FRAGMENT_SHADER, fragmentSource);
            if (!vs || !fs) {
                if (vs) {
                    glDeleteShader(vs);
                }
                if (fs) {
                    glDeleteShader(fs);
                }
                return 0;
            }
            GLuint program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);

            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (!linked) {
                GLint length = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
                std::vector<char> log(length > 1 ? length : 1);
                glGetProgramInfoLog(program, (GLsizei)log.size(), nullptr,
                                    log.data());
                ION_LOG_ERROR("OpenGL: program link failed: %s", log.data());
                glDeleteProgram(program);
                return 0;
            }

            uint64_t id = nextId_++;
            shaders_[id] = program;
            uniformLocations_[id] = {};
            return id;
        }
    }

    void destroyShader(uint64_t id) override {
        auto it = shaders_.find(id);
        if (it != shaders_.end()) {
            glDeleteProgram(it->second);
            shaders_.erase(it);
        }
        uniformLocations_.erase(id);
    }

    uint64_t createTexture(const TextureDesc& desc, const void* pixels) override {
        @autoreleasepool {
            [context_ makeCurrentContext];
            GLuint texture = 0;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, glFormat(desc.format),
                         (GLsizei)desc.width, (GLsizei)desc.height, 0,
                         desc.format == TextureFormat::Depth ? GL_DEPTH_COMPONENT
                                                             : GL_RGBA,
                         desc.format == TextureFormat::RGBA8
                             ? GL_UNSIGNED_BYTE
                             : GL_FLOAT,
                         pixels);
            GLenum filter = desc.filterLinear ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            desc.generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR
                                                 : filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (desc.generateMipmaps) {
                glGenerateMipmap(GL_TEXTURE_2D);
            }

            uint64_t id = nextId_++;
            textures_[id] = {texture, desc.filterLinear};
            return id;
        }
    }

    void destroyTexture(uint64_t id) override {
        auto it = textures_.find(id);
        if (it != textures_.end()) {
            glDeleteTextures(1, &it->second.texture);
            textures_.erase(it);
        }
    }

    uint64_t createVertexBuffer(uint32_t sizeBytes,
                                const void* data) override {
        @autoreleasepool {
            [context_ makeCurrentContext];
            GLuint buffer = 0;
            glGenBuffers(1, &buffer);
            glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glBufferData(GL_ARRAY_BUFFER, sizeBytes, data, GL_STATIC_DRAW);
            uint64_t id = nextId_++;
            vertexBuffers_[id] = buffer;
            return id;
        }
    }

    void destroyVertexBuffer(uint64_t id) override {
        auto it = vertexBuffers_.find(id);
        if (it != vertexBuffers_.end()) {
            GLuint buffer = it->second;
            glDeleteBuffers(1, &buffer);
            vertexBuffers_.erase(it);
        }
    }

    void updateVertexBuffer(uint64_t id, uint32_t offsetBytes,
                            uint32_t sizeBytes, const void* data) override {
        @autoreleasepool {
            auto it = vertexBuffers_.find(id);
            if (it != vertexBuffers_.end()) {
                [context_ makeCurrentContext];
                glBindBuffer(GL_ARRAY_BUFFER, it->second);
                glBufferSubData(GL_ARRAY_BUFFER, offsetBytes, sizeBytes, data);
            }
        }
    }

    uint64_t createIndexBuffer(uint32_t count, bool is16Bit,
                               const void* data) override {
        @autoreleasepool {
            [context_ makeCurrentContext];
            GLuint buffer = 0;
            glGenBuffers(1, &buffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         count * (is16Bit ? sizeof(uint16_t)
                                          : sizeof(uint32_t)),
                         data, GL_STATIC_DRAW);
            uint64_t id = nextId_++;
            indexBuffers_[id] = buffer;
            indexBufferIs16Bit_[id] = is16Bit;
            return id;
        }
    }

    void destroyIndexBuffer(uint64_t id) override {
        auto it = indexBuffers_.find(id);
        if (it != indexBuffers_.end()) {
            GLuint buffer = it->second;
            glDeleteBuffers(1, &buffer);
            indexBuffers_.erase(it);
        }
        indexBufferIs16Bit_.erase(id);
    }

    void updateIndexBuffer(uint64_t id, uint32_t offsetBytes,
                           uint32_t sizeBytes, const void* data) override {
        @autoreleasepool {
            auto it = indexBuffers_.find(id);
            if (it != indexBuffers_.end()) {
                [context_ makeCurrentContext];
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second);
                glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offsetBytes, sizeBytes,
                                data);
            }
        }
    }

private:
    GLuint compileShader_(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint length = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::vector<char> log(length > 1 ? length : 1);
            glGetShaderInfoLog(shader, (GLsizei)log.size(), nullptr, log.data());
            ION_LOG_ERROR("OpenGL: shader compile failed: %s", log.data());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    void executeCommand_(const RenderCommand& command) {
        switch (command.type) {
        case RenderCommandType::Clear:
            glClearColor(command.clearColor.r, command.clearColor.g,
                         command.clearColor.b, command.clearColor.a);
            glClear(GL_COLOR_BUFFER_BIT);
            break;
        case RenderCommandType::UseShader: {
            auto it = shaders_.find(command.shaderId);
            if (it != shaders_.end()) {
                activeProgram_ = it->second;
                glUseProgram(activeProgram_);
            }
            break;
        }
        case RenderCommandType::SetTexture: {
            auto it = textures_.find(command.textureId);
            if (it == textures_.end()) {
                break;
            }
            glActiveTexture(GL_TEXTURE0 + command.textureSlot);
            glBindTexture(GL_TEXTURE_2D, it->second.texture);
            break;
        }
        case RenderCommandType::BindVertexBuffer: {
            auto it = vertexBuffers_.find(command.vertexBufferId);
            if (it == vertexBuffers_.end()) {
                break;
            }
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, it->second);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36,
                                  (const void*)0);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 36,
                                  (const void*)12);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 36,
                                  (const void*)28);
            break;
        }
        case RenderCommandType::BindIndexBuffer: {
            auto it = indexBuffers_.find(command.indexBufferId);
            if (it != indexBuffers_.end()) {
                auto bitIt = indexBufferIs16Bit_.find(command.indexBufferId);
                indexIs16Bit_ =
                    bitIt != indexBufferIs16Bit_.end() && bitIt->second;
                glBindVertexArray(vao_);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second);
            }
            break;
        }
        case RenderCommandType::SetUniformFloat:
        case RenderCommandType::SetUniformVec2:
        case RenderCommandType::SetUniformVec3:
        case RenderCommandType::SetUniformVec4:
        case RenderCommandType::SetUniformMat4: {
            if (!activeProgram_) {
                break;
            }
            GLint location = uniformLocation_(activeProgram_,
                                              command.uniformName.c_str());
            if (location < 0) {
                break;
            }
            const float* v = command.uniformData;
            switch (command.type) {
            case RenderCommandType::SetUniformFloat:
                glUniform1f(location, v[0]);
                break;
            case RenderCommandType::SetUniformVec2:
                glUniform2f(location, v[0], v[1]);
                break;
            case RenderCommandType::SetUniformVec3:
                glUniform3f(location, v[0], v[1], v[2]);
                break;
            case RenderCommandType::SetUniformVec4:
                glUniform4f(location, v[0], v[1], v[2], v[3]);
                break;
            case RenderCommandType::SetUniformMat4:
                glUniformMatrix4fv(location, 1, GL_FALSE, v);
                break;
            default:
                break;
            }
            break;
        }
        case RenderCommandType::Draw:
            glBindVertexArray(vao_);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)command.count);
            break;
        case RenderCommandType::DrawIndexed: {
            glBindVertexArray(vao_);
            GLenum type = indexIs16Bit_ ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
            GLuint elementSize = indexIs16Bit_ ? 2 : 4;
            glDrawElements(GL_TRIANGLES, (GLsizei)command.count, type,
                           (const void*)((uintptr_t)command.startIndex *
                                         elementSize));
            break;
        }
        }
    }

    GLint uniformLocation_(GLuint program, const char* name) {
        auto& cache = uniformLocations_[program];
        auto it = cache.find(name);
        if (it != cache.end()) {
            return it->second;
        }
        GLint location = glGetUniformLocation(program, name);
        cache[name] = location;
        return location;
    }

    NSView* view_ = nil;
    NSOpenGLContext* context_ = nil;
    RendererConfig config_;
    GLuint vao_ = 0;
    GLsizei viewportWidth_ = 0;
    GLsizei viewportHeight_ = 0;
    GLuint activeProgram_ = 0;
    uint64_t nextId_ = 1;

    std::unordered_map<uint64_t, GLuint> shaders_;
    std::unordered_map<uint64_t,
                       std::unordered_map<std::string, GLint>>
        uniformLocations_;
    std::unordered_map<uint64_t, TextureData> textures_;
    std::unordered_map<uint64_t, GLuint> vertexBuffers_;
    std::unordered_map<uint64_t, GLuint> indexBuffers_;
    std::unordered_map<uint64_t, bool> indexBufferIs16Bit_;
    std::vector<RenderCommand> pendingCommands_;
    bool indexIs16Bit_ = true;
    bool initialized_ = false;
};

} // namespace ion

namespace ion {
ion::RenderBackend* createOpenGLBackend() {
    return new ion::OpenGLBackend();
}
} // namespace ion
