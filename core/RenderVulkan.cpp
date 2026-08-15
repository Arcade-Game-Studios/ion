//
// Vulkan render backend.
//
// Implements the RenderBackend interface on top of the Vulkan API for
// Windows and Linux (X11). GLSL shaders are compiled to SPIR-V at runtime
// with libshaderc (loaded dynamically; shader creation falls back to a
// failure return when the library is not available).
//
// Non-opaque GLSL uniforms are gathered into a single std140 uniform block
// (binding 0); samplers are assigned bindings 1..N in declaration order so a
// fixed descriptor set layout can serve the whole pipeline.
//

#include "RenderBackend.hpp"

#include <ion/core/Log.hpp>

#include <vulkan/vulkan.h>

#ifdef __linux__
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#elif defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Dynamic loading of the shared library helper (for libshaderc)
// ---------------------------------------------------------------------------

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace {
void* ionLoadLibrary(const char* name) {
    return (void*)LoadLibraryA(name);
}
void* ionGetProcAddress(void* handle, const char* name) {
    return (void*)GetProcAddress((HMODULE)handle, name);
}
void ionUnloadLibrary(void* handle) {
    if (handle) {
        FreeLibrary((HMODULE)handle);
    }
}
} // namespace
#else
#include <dlfcn.h>
namespace {
void* ionLoadLibrary(const char* name) {
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
}
void* ionGetProcAddress(void* handle, const char* name) {
    return handle ? dlsym(handle, name) : nullptr;
}
void ionUnloadLibrary(void* handle) {
    if (handle) {
        dlclose(handle);
    }
}
} // namespace
#endif

namespace ion {

namespace {

// ---------------------------------------------------------------------------
// libshaderc runtime interface (C API, stable)
// ---------------------------------------------------------------------------

struct Shaderc {
    void* handle = nullptr;

    using CompilerInit = void* (*)(void);
    using CompilerRelease = void (*)(void*);
    using OptionsInit = void* (*)(void);
    using OptionsRelease = void (*)(void*);
    using SetSourceLang = void (*)(void*, int);
    using SetTargetEnv = void (*)(void*, int, uint32_t);
    using SetBoolOption = void (*)(void*, int);
    using CompileIntoSpv = void* (*)(void*, const char*, size_t, int,
                                     const char*, const char*, void*);
    using ResultLength = size_t (*)(void*);
    using ResultBytes = const char* (*)(void*);
    using ResultError = const char* (*)(void*);
    using ResultRelease = void (*)(void*);

    CompilerInit compilerInitialize = nullptr;
    CompilerRelease compilerRelease = nullptr;
    OptionsInit optionsInitialize = nullptr;
    OptionsRelease optionsRelease = nullptr;
    SetSourceLang setSourceLanguage = nullptr;
    SetTargetEnv setTargetEnv = nullptr;
    SetBoolOption setRelaxedRules = nullptr;
    SetBoolOption setTargetSpirv = nullptr;
    CompileIntoSpv compileIntoSpv = nullptr;
    ResultLength resultLength = nullptr;
    ResultBytes resultBytes = nullptr;
    ResultError resultError = nullptr;
    ResultRelease resultRelease = nullptr;

    bool load() {
        if (handle) {
            return true;
        }
        // Also attempt the dev-package soname so the feature works on
        // systems with the full shaderc dev install.
        const char* names[] = {"libshaderc_shared.so.1", "libshaderc_shared.so",
                               "shaderc_shared.dll", "libshaderc_shared.dylib"};
        for (const char* name : names) {
            handle = ionLoadLibrary(name);
            if (handle) {
                break;
            }
        }
        if (!handle) {
            return false;
        }
        compilerInitialize =
            (CompilerInit)ionGetProcAddress(handle, "shaderc_compiler_initialize");
        compilerRelease =
            (CompilerRelease)ionGetProcAddress(handle, "shaderc_compiler_release");
        optionsInitialize = (OptionsInit)ionGetProcAddress(
            handle, "shaderc_compile_options_initialize");
        optionsRelease = (OptionsRelease)ionGetProcAddress(
            handle, "shaderc_compile_options_release");
        setSourceLanguage = (SetSourceLang)ionGetProcAddress(
            handle, "shaderc_compile_options_set_source_language");
        setTargetEnv = (SetTargetEnv)ionGetProcAddress(
            handle, "shaderc_compile_options_set_target_env");
        setRelaxedRules = (SetBoolOption)ionGetProcAddress(
            handle, "shaderc_compile_options_set_vulkan_rules_relaxed");
        setTargetSpirv = (SetBoolOption)ionGetProcAddress(
            handle, "shaderc_compile_options_set_target_spirv");
        compileIntoSpv = (CompileIntoSpv)ionGetProcAddress(
            handle, "shaderc_compile_into_spv");
        resultLength =
            (ResultLength)ionGetProcAddress(handle, "shaderc_result_get_length");
        resultBytes =
            (ResultBytes)ionGetProcAddress(handle, "shaderc_result_get_bytes");
        resultError = (ResultError)ionGetProcAddress(
            handle, "shaderc_result_get_error_message");
        resultRelease =
            (ResultRelease)ionGetProcAddress(handle, "shaderc_result_release");

        bool complete = compilerInitialize && compilerRelease &&
                        optionsInitialize && optionsRelease && compileIntoSpv &&
                        resultLength && resultBytes && resultError &&
                        resultRelease;
        if (!complete) {
            unload();
            return false;
        }
        return true;
    }

    void unload() {
        ionUnloadLibrary(handle);
        handle = nullptr;
    }
};

Shaderc& shaderc() {
    static Shaderc library;
    return library;
}

// ---------------------------------------------------------------------------
// GLSL uniform parsing and Vulkan-ification
// ---------------------------------------------------------------------------

struct UniformInfo {
    std::string name;
    std::string type;
    uint32_t arrayCount = 0;  // 0 = not an array
    uint32_t offset = 0;      // UBO byte offset (std140), valid for non-opaque
    uint32_t size = 0;        // UBO byte size, valid for non-opaque
    bool isSampler = false;
};

struct ParsedLayout {
    std::vector<UniformInfo> uniforms;
    std::vector<std::string> samplers;
    uint32_t uboSize = 0;
};

std::string trimWhitespace(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) {
        return "";
    }
    size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

bool isSamplerType(const std::string& type) {
    return type == "sampler2D" || type == "samplerCube" ||
           type == "sampler2DArray" || type == "sampler3D" ||
           type == "sampler1D" || type == "sampler2DShadow" ||
           type == "sampler2DMS" || type == "sampler2DMSArray" ||
           type == "isampler2D" || type == "usampler2D" ||
           type == "isamplerCube" || type == "usamplerCube" ||
           type == "samplerCubeArray";
}

struct TypeLayout {
    uint32_t size = 0;
    uint32_t align = 0;
};

TypeLayout typeLayout(const std::string& type) {
    if (type == "float" || type == "int" || type == "uint" ||
        type == "bool") {
        return {4, 4};
    }
    if (type == "vec2" || type == "ivec2" || type == "uvec2" ||
        type == "bvec2") {
        return {8, 8};
    }
    if (type == "vec3" || type == "ivec3" || type == "uvec3" ||
        type == "bvec3") {
        return {12, 16};
    }
    if (type == "vec4" || type == "ivec4" || type == "uvec4" ||
        type == "bvec4") {
        return {16, 16};
    }
    if (type == "mat4") {
        return {64, 16};
    }
    if (type == "mat3") {
        return {48, 16};
    }
    if (type == "mat2") {
        return {32, 16};
    }
    return {16, 16};
}

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Parses "uniform <type> <name>[N];" (with or without a leading layout(...)).
// Returns false when the line is not a uniform declaration.
bool parseUniformDeclaration(const std::string& raw, UniformInfo& out) {
    std::string s = trimWhitespace(raw);
    if (s.rfind("layout(", 0) == 0) {
        // Strip an existing layout qualifier (only meaningful when it does
        // not carry a binding, which is handled by the caller).
        std::string::size_type close = s.find(')');
        if (close == std::string::npos) {
            return false;
        }
        s = trimWhitespace(s.substr(close + 1));
    }
    if (s.rfind("uniform", 0) != 0) {
        return false;
    }
    s = s.substr(7);  // "uniform"

    size_t pos = s.find_first_not_of(" \t");
    if (pos == std::string::npos) {
        return false;
    }
    s = s.substr(pos);

    size_t end = 0;
    while (end < s.size() &&
           (isalnum((unsigned char)s[end]) || s[end] == '_')) {
        ++end;
    }
    if (end == 0) {
        return false;
    }
    out.type = s.substr(0, end);
    s = s.substr(end);

    pos = s.find_first_not_of(" \t");
    if (pos == std::string::npos) {
        return false;
    }
    s = s.substr(pos);

    end = 0;
    while (end < s.size() &&
           (isalnum((unsigned char)s[end]) || s[end] == '_')) {
        ++end;
    }
    if (end == 0) {
        return false;
    }
    out.name = s.substr(0, end);
    s = s.substr(end);

    pos = s.find_first_not_of(" \t");
    if (pos == std::string::npos) {
        return false;
    }
    s = s.substr(pos);

    out.arrayCount = 0;
    if (s[0] == '[') {
        uint32_t count = 0;
        size_t i = 1;
        while (i < s.size() && isdigit((unsigned char)s[i])) {
            count = count * 10 + (uint32_t)(s[i] - '0');
            ++i;
        }
        if (i >= s.size() || s[i] != ']') {
            return false;
        }
        out.arrayCount = count;
        s = s.substr(i + 1);
        s = trimWhitespace(s);
    }

    if (s.empty() || s[0] != ';') {
        return false;
    }
    return true;
}

void collectUniforms(const char* source, ParsedLayout& layout) {
    if (!source) {
        return;
    }
    std::string text = source;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;

        std::string trimmed = trimWhitespace(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        // Skip declarations that already carry an explicit binding.
        if (trimmed.find("binding") != std::string::npos) {
            continue;
        }
        if (trimmed.rfind("layout(", 0) == 0) {
            // Rewind to the statement start for the generic parser.
            std::string::size_type close = trimmed.find(')');
            if (close == std::string::npos ||
                trimmed.substr(close + 1).find("uniform") ==
                    std::string::npos) {
                continue;
            }
            trimmed = trimmed.substr(close + 1);
        }
        if (trimmed.rfind("uniform", 0) != 0) {
            continue;
        }
        UniformInfo info;
        if (!parseUniformDeclaration(trimmed, info)) {
            continue;
        }
        if (isSamplerType(info.type)) {
            bool found = false;
            for (const std::string& name : layout.samplers) {
                if (name == info.name) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                layout.samplers.push_back(info.name);
            }
        } else {
            bool found = false;
            for (const UniformInfo& u : layout.uniforms) {
                if (u.name == info.name) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                layout.uniforms.push_back(std::move(info));
            }
        }
    }
}

void assignUniformOffsets(ParsedLayout& layout) {
    uint32_t offset = 0;
    for (UniformInfo& u : layout.uniforms) {
        TypeLayout type = typeLayout(u.type);
        uint32_t size = type.size;
        if (u.arrayCount > 0) {
            uint32_t stride = alignUp(type.size, 16);
            size = stride * u.arrayCount;
        }
        offset = alignUp(offset, type.align);
        u.offset = offset;
        u.size = size;
        offset += size;
    }
    layout.uboSize = alignUp(offset, 16);
}

//
// Rewrites a GLSL source for Vulkan consumption:
//  - the version is normalized to #version 450 so explicit bindings are
//    valid and no relaxed-rules fallback is needed,
//  - every non-opaque bare uniform is moved into a single std140 uniform
//    block "IonDefaultBlock" at binding 0 (members are accessed directly by
//    name, so shader bodies do not need to change),
//  - samplers receive explicit layout(binding=N) qualifiers in declaration
//    order (binding 1..N),
//  - user-defined in/out variables without a location qualifier receive one
//    (SPIR-V requires locations on all user-defined interface variables).
// Shaders that already use explicit bindings/locations are preserved.
//
std::string makeVulkanGLSL(const char* source, const ParsedLayout& layout) {
    std::string text = source ? source : "";
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) {
            lines.push_back(text.substr(pos));
            break;
        }
        lines.push_back(text.substr(pos, eol - pos));
        pos = eol + 1;
    }

    std::string blockMembers;
    for (const UniformInfo& u : layout.uniforms) {
        blockMembers += "    " + u.type + " " + u.name;
        if (u.arrayCount > 0) {
            blockMembers += "[" + std::to_string(u.arrayCount) + "]";
        }
        blockMembers += ";\n";
    }
    if (!blockMembers.empty()) {
        blockMembers = "layout(binding = 0, std140) uniform IonDefaultBlock {\n" +
                       blockMembers + "};\n";
    }

    bool hasVersion = false;
    std::vector<std::string> output;
    size_t mainLine = lines.size();
    uint32_t inLocation = 0;
    uint32_t outLocation = 0;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string line = lines[i];
        std::string trimmed = trimWhitespace(line);

        if (trimmed.rfind("#version", 0) == 0) {
            hasVersion = true;
            output.push_back("#version 450 core");
            continue;
        }
        if (trimmed.rfind("void main", 0) == 0 && mainLine == lines.size()) {
            mainLine = output.size();
            output.push_back(line);
            continue;
        }
        if (trimmed.empty() || trimmed[0] == '#') {
            output.push_back(line);
            continue;
        }
        if (trimmed.rfind("layout(binding", 0) == 0 ||
            trimmed.rfind("layout (binding", 0) == 0) {
            output.push_back(line);
            continue;
        }

        bool hadLayout = trimmed.rfind("layout(", 0) == 0;
        std::string stmt = trimmed;
        if (hadLayout) {
            std::string::size_type close = stmt.find(')');
            if (close == std::string::npos) {
                output.push_back(line);
                continue;
            }
            stmt = trimWhitespace(stmt.substr(close + 1));
        }

        if (stmt.rfind("uniform", 0) == 0) {
            UniformInfo info;
            if (!parseUniformDeclaration(stmt, info)) {
                output.push_back(line);
                continue;
            }

            if (isSamplerType(info.type)) {
                // Assign binding by declaration order in the merged layout.
                uint32_t binding = 1;
                for (const std::string& name : layout.samplers) {
                    if (name == info.name) {
                        break;
                    }
                    ++binding;
                }
                std::string array;
                if (info.arrayCount > 0) {
                    array = "[" + std::to_string(info.arrayCount) + "]";
                }
                output.push_back("layout(binding = " + std::to_string(binding) +
                                 ") uniform " + info.type + " " + info.name +
                                 array + ";");
                continue;
            }

            // Non-opaque uniform: gathered into the block, emitted before
            // main(). Skip the original declaration.
            continue;
        }

        // Interface variables need explicit locations for SPIR-V. Variables
        // that already carry a layout qualifier are assumed to have one.
        if (stmt.rfind("in ", 0) == 0 || stmt.rfind("out ", 0) == 0) {
            uint32_t location = (stmt.rfind("in ", 0) == 0) ? inLocation++
                                                            : outLocation++;
            if (!hadLayout) {
                output.push_back("layout(location = " +
                                 std::to_string(location) + ") " + stmt);
            } else {
                output.push_back(line);
            }
            continue;
        }

        output.push_back(line);
    }

    // Insert the block before main() when present, otherwise at the end.
    if (!blockMembers.empty()) {
        if (mainLine != lines.size() && mainLine < output.size()) {
            output.insert(output.begin() + (long)mainLine, blockMembers);
        } else {
            output.push_back(blockMembers);
        }
    }

    std::string result;
    for (const std::string& line : output) {
        result += line + "\n";
    }
    if (!hasVersion) {
        result = "#version 450\n" + result;
    }
    return result;
}

//
// Compiles a GLSL stage to SPIR-V. Returns empty vector on failure.
//
std::vector<uint32_t> compileStage(const std::string& source, int shaderKind,
                                   const std::string& entryPoint) {
    Shaderc& s = shaderc();
    if (!s.load()) {
        return {};
    }

    void* compiler = s.compilerInitialize();
    if (!compiler) {
        return {};
    }
    void* options = s.optionsInitialize();
    if (options) {
        // 0 = shaderc_source_language_glsl
        if (s.setSourceLanguage) {
            s.setSourceLanguage(options, 0);
        }
        // 0 = shaderc_target_env_vulkan, version = shaderc_env_version
        // (1 << 22 = Vulkan 1.0).
        if (s.setTargetEnv) {
            s.setTargetEnv(options, 0, 4194304u);
        }
        // 0x010000 = SPIR-V 1.0.
        if (s.setTargetSpirv) {
            s.setTargetSpirv(options, 0x010000);
        }
        if (s.setRelaxedRules) {
            s.setRelaxedRules(options, 1);
        }
    }

    void* result =
        s.compileIntoSpv(compiler, source.data(), source.size(), shaderKind,
                         "ion", entryPoint.c_str(), options);
    std::vector<uint32_t> spirv;
    if (result) {
        size_t byteCount = s.resultLength(result);
        if (byteCount > 0 && (byteCount % 4) == 0) {
            const char* bytes = s.resultBytes(result);
            spirv.resize(byteCount / 4);
            std::memcpy(spirv.data(), bytes, byteCount);
        } else {
            const char* message = s.resultError(result);
            ION_LOG_ERROR("Shader compilation failed: %s",
                          message ? message : "unknown error");
        }
        s.resultRelease(result);
    }

    if (options) {
        s.optionsRelease(options);
    }
    s.compilerRelease(compiler);
    return spirv;
}

} // namespace

// ---------------------------------------------------------------------------
// Vulkan backend
// ---------------------------------------------------------------------------

class VulkanBackend : public RenderBackend {
public:
    ~VulkanBackend() override {
        shutdown();
    }

    // ------------------------------------------------------------------
    // Initialize / shutdown
    // ------------------------------------------------------------------

    bool initialize(void* nativeView, const RendererConfig& config,
                    void* nativeDisplay) override {
        if (initialized_) {
            return true;
        }
        if (!nativeView) {
            ION_LOG_ERROR("Vulkan backend requires a native window handle");
            return false;
        }

        vsync_ = config.vsync;
        windowHandle_ = nativeView;
        displayHandle_ = nativeDisplay;

        if (!createInstance_()) {
            return false;
        }
        if (!createSurface_()) {
            ION_LOG_ERROR("Failed to create Vulkan surface");
            shutdown();
            return false;
        }
        if (!pickPhysicalDevice_()) {
            ION_LOG_ERROR("No suitable Vulkan device found");
            shutdown();
            return false;
        }
        if (!createDevice_()) {
            ION_LOG_ERROR("Failed to create Vulkan device");
            shutdown();
            return false;
        }
        if (!createSwapchainResources_()) {
            ION_LOG_ERROR("Failed to create Vulkan swapchain");
            shutdown();
            return false;
        }
        if (!createDescriptorLayout_()) {
            ION_LOG_ERROR("Failed to create Vulkan descriptor layout");
            shutdown();
            return false;
        }
        if (!createFrameResources_()) {
            ION_LOG_ERROR("Failed to create Vulkan frame resources");
            shutdown();
            return false;
        }

        depthWriteDynamic_ = fetchDynamicStateExtension_();
        fillGpuInfo_();

        initialized_ = true;
        ION_LOG_INFO("Vulkan backend initialized (%s)", gpuInfo_.name.c_str());
        return true;
    }

    void shutdown() override {
        if (device_) {
            vkDeviceWaitIdle(device_);
        }
        destroyFrameResources_();
        destroyAllTargets_();
        destroyAllTextures_();
        destroyAllShaders_();
        destroyAllBuffers_();
        destroySwapchainResources_();
        destroyDescriptorLayout_();
        if (device_) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (surface_) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
        initialized_ = false;
    }

    GPUInfo gpuInfo() const override {
        return gpuInfo_;
    }

    // ------------------------------------------------------------------
    // Frame lifecycle
    // ------------------------------------------------------------------

    void beginFrame(uint32_t width, uint32_t height) override {
        if (!initialized_) {
            return;
        }
        Frame* frame = &frames_[frameIndex_];

        if ((width != 0 && height != 0) &&
            (width != swapchainExtent_.width ||
             height != swapchainExtent_.height)) {
            recreateSwapchain_();
        }

        vkWaitForFences(device_, 1, &frame->inFlight, VK_TRUE, UINT64_MAX);

        VkResult acquire = vkAcquireNextImageKHR(
            device_, swapchain_, UINT64_MAX, frame->imageAvailable,
            VK_NULL_HANDLE, &imageIndex_);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain_();
            acquire = vkAcquireNextImageKHR(
                device_, swapchain_, UINT64_MAX, frame->imageAvailable,
                VK_NULL_HANDLE, &imageIndex_);
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            frameValid_ = false;
            return;
        }
        frameValid_ = true;

        vkResetFences(device_, 1, &frame->inFlight);
        vkResetCommandBuffer(frame->commandBuffer, 0);
        // Note: no vkResetDescriptorPool here. Resetting a pool implicitly
        // frees all descriptor sets allocated from it, so we would have to
        // re-allocate the set every frame. The set is fully overwritten via
        // vkUpdateDescriptorSets before use, so a reset is unnecessary.

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(frame->commandBuffer, &beginInfo);

        // Bind the per-frame uniform arena at binding 0 (dynamic offsets are
        // chosen per draw, see executeDraw_).
        frame->uboOffset = 0;
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = frame->uboArena.buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = kUboSize;
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame->descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

        // Reset per-frame draw state.
        currentShader_ = nullptr;
        currentTargetId_ = 0;
        for (uint32_t i = 0; i < kMaxSamplerBindings; ++i) {
            currentTextures_[i] = nullptr;
        }
        currentVertexBuffer_ = VK_NULL_HANDLE;
        currentIndexBuffer_ = VK_NULL_HANDLE;
        clearColor_ = VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}};
        renderPassActive_ = false;
        drawDepthWrite_ = true;
    }

    void endFrame() override {
        if (!initialized_) {
            return;
        }
        Frame* frame = &frames_[frameIndex_];
        if (!frameValid_) {
            frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
            return;
        }

        if (renderPassActive_) {
            endRenderPass_();
        }

        VkResult record = vkEndCommandBuffer(frame->commandBuffer);
        if (record != VK_SUCCESS) {
            ION_LOG_ERROR("Failed to record Vulkan command buffer");
            return;
        }

        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frame->imageAvailable;
        VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &frame->commandBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &frame->renderFinished;
        vkQueueSubmit(graphicsQueue_, 1, &submit, frame->inFlight);

        VkPresentInfoKHR present = {};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &frame->renderFinished;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &imageIndex_;
        VkResult result = vkQueuePresentKHR(presentQueue_, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR ||
            result == VK_SUBOPTIMAL_KHR) {
            recreateSwapchain_();
        }

        frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
    }

    // ------------------------------------------------------------------
    // Commands
    // ------------------------------------------------------------------

    void execute(const RenderCommand& command) override {
        if (!initialized_) {
            return;
        }
        if (!frameValid_) {
            return;
        }
        switch (command.type) {
        case RenderCommandType::Clear:
            clearColor_.float32[0] = command.clearColor.r;
            clearColor_.float32[1] = command.clearColor.g;
            clearColor_.float32[2] = command.clearColor.b;
            clearColor_.float32[3] = command.clearColor.a;
            break;

        case RenderCommandType::UseShader:
            currentShader_ = findShader_(command.shaderId);
            if (currentShader_ && currentShader_->layout.uboSize > 0 &&
                !currentShader_->uniformCache) {
                currentShader_->uniformCache.reset(
                    new uint8_t[currentShader_->layout.uboSize]());
            }
            break;

        case RenderCommandType::SetTexture:
            executeSetTexture_(command);
            break;

        case RenderCommandType::BindVertexBuffer:
            currentVertexBuffer_ = findBuffer_(command.vertexBufferId);
            break;

        case RenderCommandType::BindIndexBuffer:
            currentIndexBuffer_ = findBuffer_(command.indexBufferId);
            break;

        case RenderCommandType::SetUniformFloat:
            writeUniform_(command, 4);
            break;
        case RenderCommandType::SetUniformVec2:
            writeUniform_(command, 8);
            break;
        case RenderCommandType::SetUniformVec3:
            writeUniform_(command, 12);
            break;
        case RenderCommandType::SetUniformVec4:
            writeUniform_(command, 16);
            break;
        case RenderCommandType::SetUniformMat4:
            writeUniform_(command, 64);
            break;
        case RenderCommandType::SetUniformVec4Array:
            writeUniformArray_(command);
            break;

        case RenderCommandType::SetRenderTarget:
            if (renderPassActive_) {
                endRenderPass_();
            }
            currentTargetId_ = command.targetId;
            break;

        case RenderCommandType::SetDepthWrite:
            drawDepthWrite_ = command.count != 0;
            break;

        case RenderCommandType::Draw:
            executeDraw_(command, false);
            break;

        case RenderCommandType::DrawIndexed:
            executeDraw_(command, true);
            break;
        }
    }

    // ------------------------------------------------------------------
    // Shaders
    // ------------------------------------------------------------------

    uint64_t createShader(const char* vertexSource,
                          const char* fragmentSource) override {
        if (!initialized_ || !vertexSource || !fragmentSource) {
            return 0;
        }
        if (!shaderc().load()) {
            ION_LOG_WARN(
                "libshaderc not available; cannot compile Vulkan shaders");
            return 0;
        }

        // Build a merged uniform/sampler layout from both stages so the UBO
        // layout is identical across the pipeline.
        ParsedLayout layout;
        collectUniforms(vertexSource, layout);
        collectUniforms(fragmentSource, layout);
        assignUniformOffsets(layout);

        std::string vertexVk = makeVulkanGLSL(vertexSource, layout);
        std::string fragmentVk = makeVulkanGLSL(fragmentSource, layout);

        std::vector<uint32_t> vertexSpirv =
            compileStage(vertexVk, 0, "main");  // shaderc_glsl_vertex_shader
        std::vector<uint32_t> fragmentSpirv =
            compileStage(fragmentVk, 1, "main");  // glsl fragment shader
        if (vertexSpirv.empty() || fragmentSpirv.empty()) {
            return 0;
        }

        VkShaderModule vertexModule = createShaderModule_(vertexSpirv);
        VkShaderModule fragmentModule = createShaderModule_(fragmentSpirv);
        if (!vertexModule || !fragmentModule) {
            if (vertexModule) {
                vkDestroyShaderModule(device_, vertexModule, nullptr);
            }
            if (fragmentModule) {
                vkDestroyShaderModule(device_, fragmentModule, nullptr);
            }
            return 0;
        }

        std::unique_ptr<ShaderData> shader(new ShaderData());
        shader->layout = layout;
        shader->vertexModule = vertexModule;
        shader->fragmentModule = fragmentModule;
        shader->uboSize = layout.uboSize;
        if (layout.uboSize > 0) {
            shader->uniformCache.reset(new uint8_t[layout.uboSize]());
        }

        uint64_t id = nextId_++;
        shaders_[id] = std::move(shader);
        return id;
    }

    void destroyShader(uint64_t id) override {
        auto it = shaders_.find(id);
        if (it == shaders_.end()) {
            return;
        }
        if (currentShader_ == it->second.get()) {
            currentShader_ = nullptr;
        }
        for (auto& pair : it->second->pipelines) {
            vkDestroyPipeline(device_, pair.second, nullptr);
        }
        it->second->pipelines.clear();
        if (it->second->vertexModule) {
            vkDestroyShaderModule(device_, it->second->vertexModule, nullptr);
        }
        if (it->second->fragmentModule) {
            vkDestroyShaderModule(device_, it->second->fragmentModule, nullptr);
        }
        shaders_.erase(it);
    }

    // ------------------------------------------------------------------
    // Textures
    // ------------------------------------------------------------------

    uint64_t createTexture(const TextureDesc& desc, const void* pixels) override {
        if (!initialized_ || !pixels || desc.width == 0 || desc.height == 0) {
            return 0;
        }
        VkFormat format = vkFormat_(desc.format);
        VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT;

        std::unique_ptr<TextureData> texture(new TextureData());
        if (!createImage_(desc.width, desc.height, format,
                          VK_IMAGE_TILING_OPTIMAL, usage,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1,
                          &texture->image, &texture->memory)) {
            return 0;
        }
        texture->format = format;
        texture->width = desc.width;
        texture->height = desc.height;
        texture->layout = VK_IMAGE_LAYOUT_UNDEFINED;

        uploadPixels_(*texture, 0, 1, pixels);

        texture->sampler =
            createSampler_(desc.filterLinear ? VK_FILTER_LINEAR
                                            : VK_FILTER_NEAREST);
        texture->view = createImageView_(texture->image, format,
                                         VK_IMAGE_VIEW_TYPE_2D,
                                         VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
        if (!texture->sampler || !texture->view) {
            return 0;
        }

        uint64_t id = nextId_++;
        textures_[id] = std::move(texture);
        return id;
    }

    void destroyTexture(uint64_t id) override {
        auto it = textures_.find(id);
        if (it == textures_.end()) {
            return;
        }
        for (uint32_t i = 0; i < kMaxSamplerBindings; ++i) {
            if (currentTextures_[i] == it->second.get()) {
                currentTextures_[i] = nullptr;
            }
        }
        destroyTextureData_(it->second.get());
        textures_.erase(it);
    }

    uint64_t createCubemap(const TextureDesc& desc,
                           const void* const faces[6]) override {
        if (!initialized_ || !faces || desc.width == 0 || desc.height == 0) {
            return 0;
        }
        VkFormat format = vkFormat_(desc.format);
        std::unique_ptr<TextureData> texture(new TextureData());
        if (!createImage_(desc.width, desc.height, format,
                          VK_IMAGE_TILING_OPTIMAL,
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 6,
                          &texture->image, &texture->memory)) {
            return 0;
        }
        texture->format = format;
        texture->width = desc.width;
        texture->height = desc.height;
        texture->layout = VK_IMAGE_LAYOUT_UNDEFINED;

        for (int i = 0; i < 6; ++i) {
            if (faces[i]) {
                uploadPixels_(*texture, (uint32_t)i, 6, faces[i]);
            }
        }

        texture->sampler =
            createSampler_(desc.filterLinear ? VK_FILTER_LINEAR
                                            : VK_FILTER_NEAREST);
        texture->view =
            createImageView_(texture->image, format, VK_IMAGE_VIEW_TYPE_CUBE,
                             VK_IMAGE_ASPECT_COLOR_BIT, 1, 6);
        if (!texture->sampler || !texture->view) {
            return 0;
        }

        uint64_t id = nextId_++;
        textures_[id] = std::move(texture);
        return id;
    }

    // ------------------------------------------------------------------
    // Render targets
    // ------------------------------------------------------------------

    RenderTargetCreateInfo createRenderTarget(
        const RenderTargetDesc& desc) override {
        RenderTargetCreateInfo info;
        if (!initialized_) {
            return info;
        }

        std::unique_ptr<RenderTargetData> target(new RenderTargetData());
        target->width = desc.width;
        target->height = desc.height;
        target->colorFormat = desc.format != TextureFormat::Depth
                                  ? vkFormat_(desc.format)
                                  : VK_FORMAT_UNDEFINED;
        target->withDepth = desc.withDepth;
        target->isDepthOnly = desc.format == TextureFormat::Depth;

        VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                       VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                       VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        uint64_t colorId = 0;
        if (!target->isDepthOnly) {
            std::unique_ptr<TextureData> color(new TextureData());
            if (!createImage_(desc.width, desc.height, target->colorFormat,
                              VK_IMAGE_TILING_OPTIMAL, colorUsage,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1,
                              &color->image, &color->memory)) {
                return info;
            }
            color->format = target->colorFormat;
            color->width = desc.width;
            color->height = desc.height;
            color->layout = VK_IMAGE_LAYOUT_UNDEFINED;
            color->sampler = createSampler_(VK_FILTER_LINEAR);
            color->view =
                createImageView_(color->image, target->colorFormat,
                                 VK_IMAGE_VIEW_TYPE_2D,
                                 VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
            colorId = nextId_++;
            textures_[colorId] = std::move(color);
            target->colorTextureId = colorId;
        }

        uint64_t depthId = 0;
        if (target->withDepth) {
            std::unique_ptr<TextureData> depth(new TextureData());
            if (!createImage_(desc.width, desc.height, depthFormat_,
                              VK_IMAGE_TILING_OPTIMAL, depthUsage,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1,
                              &depth->image, &depth->memory)) {
                return info;
            }
            depth->format = depthFormat_;
            depth->width = desc.width;
            depth->height = desc.height;
            depth->layout = VK_IMAGE_LAYOUT_UNDEFINED;
            depth->view =
                createImageView_(depth->image, depthFormat_,
                                 VK_IMAGE_VIEW_TYPE_2D,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1);
            depthId = nextId_++;
            textures_[depthId] = std::move(depth);
            target->depthTextureId = depthId;
        }

        if (!createRenderPassForTarget_(*target)) {
            return info;
        }
        if (!createFramebufferForTarget_(*target)) {
            return info;
        }

        uint64_t targetId = nextId_++;
        renderTargets_[targetId] = std::move(target);

        info.targetId = targetId;
        info.colorTextureId = colorId;
        info.depthTextureId = depthId;
        return info;
    }

    void destroyRenderTarget(uint64_t id) override {
        auto it = renderTargets_.find(id);
        if (it == renderTargets_.end()) {
            return;
        }
        if (currentTargetId_ == id) {
            currentTargetId_ = 0;
        }
        if (it->second->framebuffer) {
            vkDestroyFramebuffer(device_, it->second->framebuffer, nullptr);
        }
        if (it->second->renderPass) {
            vkDestroyRenderPass(device_, it->second->renderPass, nullptr);
        }
        if (it->second->colorTextureId) {
            destroyTexture(it->second->colorTextureId);
        }
        if (it->second->depthTextureId) {
            destroyTexture(it->second->depthTextureId);
        }
        renderTargets_.erase(it);
    }

    // ------------------------------------------------------------------
    // Buffers
    // ------------------------------------------------------------------

    uint64_t createVertexBuffer(uint32_t sizeBytes,
                                const void* data) override {
        return createBuffer_(sizeBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             data);
    }

    void destroyVertexBuffer(uint64_t id) override {
        destroyBuffer_(id);
    }

    void updateVertexBuffer(uint64_t id, uint32_t offsetBytes,
                            uint32_t sizeBytes, const void* data) override {
        updateBuffer_(id, offsetBytes, sizeBytes, data);
    }

    uint64_t createIndexBuffer(uint32_t count, bool is16Bit,
                               const void* data) override {
        uint64_t id = createBuffer_(count * (is16Bit ? 2u : 4u),
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, data);
        if (id) {
            if (auto* buffer = findBuffer_(id)) {
                buffer->is16Bit = is16Bit;
            }
        }
        return id;
    }

    void destroyIndexBuffer(uint64_t id) override {
        destroyBuffer_(id);
    }

    void updateIndexBuffer(uint64_t id, uint32_t offsetBytes,
                           uint32_t sizeBytes, const void* data) override {
        updateBuffer_(id, offsetBytes, sizeBytes, data);
    }

private:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kMaxSamplerBindings = 8;
    static constexpr uint32_t kUboSize = 4096;
    static constexpr uint32_t kUboArenaBytes = 1 * 1024 * 1024;

    struct TextureData {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct BufferData {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint32_t size = 0;
        bool is16Bit = false;
    };

    struct ShaderData {
        ParsedLayout layout;
        uint32_t uboSize = 0;
        VkShaderModule vertexModule = VK_NULL_HANDLE;
        VkShaderModule fragmentModule = VK_NULL_HANDLE;
        std::unordered_map<VkRenderPass, VkPipeline> pipelines;
        std::unique_ptr<uint8_t[]> uniformCache;
    };

    struct RenderTargetData {
        uint32_t width = 0;
        uint32_t height = 0;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        bool withDepth = false;
        bool isDepthOnly = false;
        uint64_t colorTextureId = 0;
        uint64_t depthTextureId = 0;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    struct Frame {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        struct {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            void* mapped = nullptr;
        } uboArena;
        uint32_t uboOffset = 0;
    };

    // ---- initialization helpers ----

    bool createInstance_() {
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Ion";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
        appInfo.pEngineName = "Ion Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 2, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        std::vector<const char*> extensions;
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef __linux__
        extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#elif defined(_WIN32)
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

        std::vector<const char*> layers;
#ifdef NDEBUG
        (void)0;
#else
        if (hasValidationLayer_()) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }
#endif

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = (uint32_t)extensions.size();
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = (uint32_t)layers.size();
        createInfo.ppEnabledLayerNames =
            layers.empty() ? nullptr : layers.data();

        if (vkCreateInstance(&createInfo, nullptr, &instance_) !=
            VK_SUCCESS) {
            ION_LOG_ERROR("vkCreateInstance failed");
            return false;
        }
        return true;
    }

    bool hasValidationLayer_() {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> layers(count);
        vkEnumerateInstanceLayerProperties(&count, layers.data());
        for (const VkLayerProperties& layer : layers) {
            if (std::strcmp(layer.layerName,
                            "VK_LAYER_KHRONOS_validation") == 0) {
                return true;
            }
        }
        return false;
    }

    bool createSurface_() {
#ifdef __linux__
        VkXlibSurfaceCreateInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        info.dpy = (Display*)displayHandle_;
        info.window = (::Window)(uintptr_t)windowHandle_;
        return vkCreateXlibSurfaceKHR(instance_, &info, nullptr, &surface_) ==
               VK_SUCCESS;
#elif defined(_WIN32)
        VkWin32SurfaceCreateInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        info.hinstance = (HINSTANCE)displayHandle_;
        info.hwnd = (HWND)windowHandle_;
        return vkCreateWin32SurfaceKHR(instance_, &info, nullptr, &surface_) ==
               VK_SUCCESS;
#else
        return false;
#endif
    }

    bool pickPhysicalDevice_() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        if (count == 0) {
            return false;
        }
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        int bestScore = -1;
        for (VkPhysicalDevice device : devices) {
            if (!deviceSuitable_(device)) {
                continue;
            }
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            int score = 0;
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                score = 10000;
            } else if (props.deviceType ==
                       VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                score = 1000;
            }
            if (score > bestScore) {
                bestScore = score;
                physicalDevice_ = device;
                deviceProps_ = props;
            }
        }
        if (bestScore < 0) {
            return false;
        }
        return true;
    }

    bool deviceSuitable_(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies_(device);
        if (!indices.complete) {
            return false;
        }
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount,
                                             nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount,
                                             available.data());
        bool swapchain = false;
        for (const VkExtensionProperties& ext : available) {
            if (std::strcmp(ext.extensionName,
                            VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                swapchain = true;
                break;
            }
        }
        return swapchain;
    }

    struct QueueFamilyIndices {
        uint32_t graphics = 0;
        uint32_t present = 0;
        bool complete = false;
    };

    QueueFamilyIndices findQueueFamilies_(VkPhysicalDevice device) {
        QueueFamilyIndices indices;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count,
                                                 families.data());
        for (uint32_t i = 0; i < count; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphics = i;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_,
                                                 &presentSupport);
            if (presentSupport == VK_TRUE) {
                indices.present = i;
            }
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
                presentSupport == VK_TRUE) {
                indices.graphics = i;
                indices.present = i;
                break;
            }
        }
        indices.complete = true;
        return indices;
    }

    bool createDevice_() {
        queueIndices_ = findQueueFamilies_(physicalDevice_);

        float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        std::vector<uint32_t> uniqueQueues = {queueIndices_.graphics};
        if (queueIndices_.present != queueIndices_.graphics) {
            uniqueQueues.push_back(queueIndices_.present);
        }
        for (uint32_t family : uniqueQueues) {
            VkDeviceQueueCreateInfo queueInfo = {};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;
            queueInfos.push_back(queueInfo);
        }

        std::vector<const char*> extensions;
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        extensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);

        VkPhysicalDeviceFeatures features = {};
        features.samplerAnisotropy = VK_TRUE;
        features.fillModeNonSolid = VK_FALSE;

        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynStateFeatures = {};
        dynStateFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        dynStateFeatures.extendedDynamicState = VK_TRUE;

        VkPhysicalDeviceDescriptorIndexingFeatures descIndexingFeatures = {};
        descIndexingFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        descIndexingFeatures.descriptorIndexing = VK_TRUE;
        descIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind =
            VK_TRUE;
        descIndexingFeatures.descriptorBindingUniformBufferUpdateAfterBind =
            VK_TRUE;
        dynStateFeatures.pNext = &descIndexingFeatures;

        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &dynStateFeatures;
        createInfo.queueCreateInfoCount = (uint32_t)queueInfos.size();
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = &features;
        createInfo.enabledExtensionCount = (uint32_t)extensions.size();
        createInfo.ppEnabledExtensionNames = extensions.data();

        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) !=
            VK_SUCCESS) {
            return false;
        }

        vkGetDeviceQueue(device_, queueIndices_.graphics, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, queueIndices_.present, 0, &presentQueue_);

        // Verify the extended dynamic state extension was enabled.
        extendedDynamicState_ = false;
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr,
                                             &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr,
                                             &extCount, available.data());
        for (const VkExtensionProperties& ext : available) {
            if (std::strcmp(ext.extensionName,
                            VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME) ==
                0) {
                extendedDynamicState_ = true;
                break;
            }
        }
        return true;
    }

    bool fetchDynamicStateExtension_() {
        if (!extendedDynamicState_) {
            return false;
        }
        cmdSetDepthWriteEnableEXT_ =
            (PFN_vkCmdSetDepthWriteEnableEXT)vkGetDeviceProcAddr(
                device_, "vkCmdSetDepthWriteEnableEXT");
        return cmdSetDepthWriteEnableEXT_ != nullptr;
    }

    VkSurfaceFormatKHR chooseSurfaceFormat_() {
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count,
                                             nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count,
                                             formats.data());
        for (const VkSurfaceFormatKHR& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
        return formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
    }

    VkPresentModeKHR choosePresentMode_() {
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_,
                                                  &count, nullptr);
        std::vector<VkPresentModeKHR> modes(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_,
                                                  &count, modes.data());
        if (vsync_) {
            for (VkPresentModeKHR mode : modes) {
                if (mode == VK_PRESENT_MODE_FIFO_KHR) {
                    return mode;
                }
            }
        } else {
            for (VkPresentModeKHR mode : modes) {
                if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                    return mode;
                }
            }
            for (VkPresentModeKHR mode : modes) {
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    return mode;
                }
            }
        }
        return modes.empty() ? VK_PRESENT_MODE_FIFO_KHR : modes[0];
    }

    VkExtent2D chooseExtent_(const VkSurfaceCapabilitiesKHR& caps,
                             uint32_t width, uint32_t height) {
        if (caps.currentExtent.width != UINT32_MAX) {
            return caps.currentExtent;
        }
        VkExtent2D extent = {width, height};
        extent.width = std::max(caps.minImageExtent.width,
                                std::min(caps.maxImageExtent.width, extent.width));
        extent.height = std::max(caps.minImageExtent.height,
                                 std::min(caps.maxImageExtent.height, extent.height));
        return extent;
    }

    void chooseDepthFormat_() {
        const VkFormat candidates[] = {
            VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
        for (VkFormat format : candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, format,
                                                &props);
            if (props.optimalTilingFeatures &
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depthFormat_ = format;
                return;
            }
        }
    }

    bool createSwapchainResources_() {
        chooseDepthFormat_();
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_,
                                                  &caps);
        surfaceFormat_ = chooseSurfaceFormat_();
        presentMode_ = choosePresentMode_();
        uint32_t width = caps.currentExtent.width != UINT32_MAX
                             ? caps.currentExtent.width
                             : 1280;
        uint32_t height = caps.currentExtent.height != UINT32_MAX
                              ? caps.currentExtent.height
                              : 720;
        swapchainExtent_ = chooseExtent_(caps, width, height);

        uint32_t imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
            imageCount = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat_.format;
        createInfo.imageColorSpace = surfaceFormat_.colorSpace;
        createInfo.imageExtent = swapchainExtent_;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilyIndices[] = {queueIndices_.graphics,
                                         queueIndices_.present};
        if (queueIndices_.graphics != queueIndices_.present) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }
        createInfo.preTransform = caps.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode_;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) !=
            VK_SUCCESS) {
            return false;
        }

        uint32_t imageViewCount = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &imageViewCount, nullptr);
        std::vector<VkImage> images(imageViewCount);
        vkGetSwapchainImagesKHR(device_, swapchain_, &imageViewCount,
                                images.data());
        swapchainImages_ = images;
        swapchainImageViews_.resize(imageViewCount);
        for (uint32_t i = 0; i < imageViewCount; ++i) {
            swapchainImageViews_[i] =
                createImageView_(swapchainImages_[i], surfaceFormat_.format,
                                 VK_IMAGE_VIEW_TYPE_2D,
                                 VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
            if (!swapchainImageViews_[i]) {
                return false;
            }
        }

        // Depth buffer for the swapchain pass.
        if (!depthImage_) {
            if (!createImage_(swapchainExtent_.width, swapchainExtent_.height,
                              depthFormat_, VK_IMAGE_TILING_OPTIMAL,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1,
                              &depthImage_, &depthImageMemory_)) {
                return false;
            }
            depthImageView_ =
                createImageView_(depthImage_, depthFormat_,
                                 VK_IMAGE_VIEW_TYPE_2D,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1);
        }

        if (!swapchainRenderPass_) {
            swapchainRenderPass_ = createSwapchainRenderPass_();
        }
        if (!swapchainRenderPass_) {
            return false;
        }

        createSwapchainFramebuffers_();
        return true;
    }

    void createSwapchainFramebuffers_() {
        for (VkFramebuffer fb : swapchainFramebuffers_) {
            if (fb) {
                vkDestroyFramebuffer(device_, fb, nullptr);
            }
        }
        swapchainFramebuffers_.resize(swapchainImageViews_.size());
        for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
            std::vector<VkImageView> attachments = {swapchainImageViews_[i]};
            if (depthImageView_) {
                attachments.push_back(depthImageView_);
            }
            VkFramebufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = swapchainRenderPass_;
            info.attachmentCount = (uint32_t)attachments.size();
            info.pAttachments = attachments.data();
            info.width = swapchainExtent_.width;
            info.height = swapchainExtent_.height;
            info.layers = 1;
            vkCreateFramebuffer(device_, &info, nullptr,
                                &swapchainFramebuffers_[i]);
        }
    }

    VkRenderPass createSwapchainRenderPass_() {
        VkAttachmentDescription color = {};
        color.format = surfaceFormat_.format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depth = {};
        depth.format = depthFormat_;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        std::vector<VkAttachmentDescription> attachments = {color};
        if (depthImageView_) {
            attachments.push_back(depth);
        }

        VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        if (depthImageView_) {
            subpass.pDepthStencilAttachment = &depthRef;
        }

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = (uint32_t)attachments.size();
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device_, &info, nullptr, &renderPass) !=
            VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        return renderPass;
    }

    bool createRenderPassForTarget_(RenderTargetData& target) {
        std::vector<VkAttachmentDescription> attachments;

        if (!target.isDepthOnly) {
            VkAttachmentDescription color = {};
            color.format = target.colorFormat;
            color.samples = VK_SAMPLE_COUNT_1_BIT;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            attachments.push_back(color);
        }

        VkAttachmentReference colorRef = {};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef = {};
        if (target.withDepth) {
            VkAttachmentDescription depth = {};
            depth.format = depthFormat_;
            depth.samples = VK_SAMPLE_COUNT_1_BIT;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            attachments.push_back(depth);
            depthRef.attachment = (uint32_t)attachments.size() - 1;
            depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        if (!target.isDepthOnly) {
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;
        }
        if (target.withDepth) {
            subpass.pDepthStencilAttachment = &depthRef;
        }

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = (uint32_t)attachments.size();
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;

        return vkCreateRenderPass(device_, &info, nullptr,
                                  &target.renderPass) == VK_SUCCESS;
    }

    bool createFramebufferForTarget_(RenderTargetData& target) {
        std::vector<VkImageView> attachments;
        if (!target.isDepthOnly) {
            auto it = textures_.find(target.colorTextureId);
            if (it == textures_.end() || !it->second->view) {
                return false;
            }
            attachments.push_back(it->second->view);
        }
        if (target.withDepth) {
            auto it = textures_.find(target.depthTextureId);
            if (it == textures_.end() || !it->second->view) {
                return false;
            }
            attachments.push_back(it->second->view);
        }

        VkFramebufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = target.renderPass;
        info.attachmentCount = (uint32_t)attachments.size();
        info.pAttachments = attachments.data();
        info.width = target.width;
        info.height = target.height;
        info.layers = 1;
        return vkCreateFramebuffer(device_, &info, nullptr,
                                   &target.framebuffer) == VK_SUCCESS;
    }

    void destroySwapchainResources_() {
        if (device_) {
            vkDeviceWaitIdle(device_);
        }
        for (VkFramebuffer fb : swapchainFramebuffers_) {
            if (fb) {
                vkDestroyFramebuffer(device_, fb, nullptr);
            }
        }
        swapchainFramebuffers_.clear();
        for (VkImageView view : swapchainImageViews_) {
            if (view) {
                vkDestroyImageView(device_, view, nullptr);
            }
        }
        swapchainImageViews_.clear();
        swapchainImages_.clear();
        if (depthImageView_) {
            vkDestroyImageView(device_, depthImageView_, nullptr);
            depthImageView_ = VK_NULL_HANDLE;
        }
        if (depthImageMemory_) {
            vkFreeMemory(device_, depthImageMemory_, nullptr);
            depthImageMemory_ = VK_NULL_HANDLE;
        }
        if (depthImage_) {
            vkDestroyImage(device_, depthImage_, nullptr);
            depthImage_ = VK_NULL_HANDLE;
        }
        if (swapchainRenderPass_) {
            vkDestroyRenderPass(device_, swapchainRenderPass_, nullptr);
            swapchainRenderPass_ = VK_NULL_HANDLE;
        }
        if (swapchain_) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
        if (oldSwapchain_) {
            vkDestroySwapchainKHR(device_, oldSwapchain_, nullptr);
            oldSwapchain_ = VK_NULL_HANDLE;
        }
    }

    bool recreateSwapchain_() {
        destroySwapchainResources_();
        if (!createSwapchainResources_()) {
            ION_LOG_ERROR("Failed to recreate Vulkan swapchain");
            return false;
        }
        return true;
    }

    // ---- descriptor layout / frames ----

    bool createDescriptorLayout_() {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.push_back({0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                            VK_SHADER_STAGE_VERTEX_BIT |
                                VK_SHADER_STAGE_FRAGMENT_BIT,
                            nullptr});
        for (uint32_t i = 0; i < kMaxSamplerBindings; ++i) {
            bindings.push_back({1 + i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
        }

        VkDescriptorSetLayoutCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        info.bindingCount = (uint32_t)bindings.size();
        info.pBindings = bindings.data();

        // Sampler bindings are written while the set is already bound in the
        // recording command buffer (see executeSetTexture_), so they must
        // support UPDATE_AFTER_BIND. Binding 0 (UBO_DYNAMIC) cannot use the
        // flag, but it is only written in beginFrame, before any binds.
        std::vector<VkDescriptorBindingFlags> bindingFlags(
            bindings.size(), 0);
        for (uint32_t i = 0; i < kMaxSamplerBindings; ++i) {
            bindingFlags[1 + i] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        }
        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {};
        bindingFlagsInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        bindingFlagsInfo.bindingCount = (uint32_t)bindingFlags.size();
        bindingFlagsInfo.pBindingFlags = bindingFlags.data();
        info.pNext = &bindingFlagsInfo;
        if (vkCreateDescriptorSetLayout(device_, &info, nullptr,
                                        &descriptorSetLayout_) != VK_SUCCESS) {
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineInfo.setLayoutCount = 1;
        pipelineInfo.pSetLayouts = &descriptorSetLayout_;
        return vkCreatePipelineLayout(device_, &pipelineInfo, nullptr,
                                      &pipelineLayout_) == VK_SUCCESS;
    }

    void destroyDescriptorLayout_() {
        if (pipelineLayout_) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        if (descriptorSetLayout_) {
            vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
            descriptorSetLayout_ = VK_NULL_HANDLE;
        }
    }

    bool createFrameResources_() {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueIndices_.graphics;
        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) !=
            VK_SUCCESS) {
            return false;
        }

        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            Frame& frame = frames_[i];

            VkCommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = commandPool_;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            if (vkAllocateCommandBuffers(device_, &allocInfo,
                                         &frame.commandBuffer) != VK_SUCCESS) {
                return false;
            }

            VkSemaphoreCreateInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                              &frame.imageAvailable);
            vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                              &frame.renderFinished);

            VkFenceCreateInfo fenceInfo = {};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vkCreateFence(device_, &fenceInfo, nullptr, &frame.inFlight);

            // Descriptor pool + set for this frame.
            std::vector<VkDescriptorPoolSize> poolSizes;
            poolSizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1});
            poolSizes.push_back(
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 kMaxSamplerBindings});
            VkDescriptorPoolCreateInfo descPoolInfo = {};
            descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descPoolInfo.flags =
                VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
                VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            descPoolInfo.maxSets = 1;
            descPoolInfo.poolSizeCount = (uint32_t)poolSizes.size();
            descPoolInfo.pPoolSizes = poolSizes.data();
            if (vkCreateDescriptorPool(device_, &descPoolInfo, nullptr,
                                       &frame.descriptorPool) != VK_SUCCESS) {
                return false;
            }

            VkDescriptorSetAllocateInfo descSetInfo = {};
            descSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descSetInfo.descriptorPool = frame.descriptorPool;
            descSetInfo.descriptorSetCount = 1;
            descSetInfo.pSetLayouts = &descriptorSetLayout_;
            if (vkAllocateDescriptorSets(device_, &descSetInfo,
                                         &frame.descriptorSet) != VK_SUCCESS) {
                return false;
            }

            // Per-frame uniform arena (dynamic offsets carve out per-draw
            // regions, see executeDraw_).
            if (!createBuffer_(kUboArenaBytes,
                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr,
                               &frame.uboArena.buffer,
                               &frame.uboArena.memory)) {
                return false;
            }
            vkMapMemory(device_, frame.uboArena.memory, 0, kUboArenaBytes, 0,
                        &frame.uboArena.mapped);
        }
        return true;
    }

    void destroyFrameResources_() {
        if (!device_) {
            return;
        }
        for (Frame& frame : frames_) {
            if (frame.uboArena.mapped) {
                vkUnmapMemory(device_, frame.uboArena.memory);
                frame.uboArena.mapped = nullptr;
            }
            if (frame.uboArena.buffer) {
                vkDestroyBuffer(device_, frame.uboArena.buffer, nullptr);
            }
            if (frame.uboArena.memory) {
                vkFreeMemory(device_, frame.uboArena.memory, nullptr);
            }
            if (frame.descriptorPool) {
                vkDestroyDescriptorPool(device_, frame.descriptorPool, nullptr);
            }
            if (frame.inFlight) {
                vkDestroyFence(device_, frame.inFlight, nullptr);
            }
            if (frame.renderFinished) {
                vkDestroySemaphore(device_, frame.renderFinished, nullptr);
            }
            if (frame.imageAvailable) {
                vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
            }
        }
        if (commandPool_) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
    }

    // ---- resource helpers ----

    VkFormat vkFormat_(TextureFormat format) const {
        switch (format) {
        case TextureFormat::RGBA8:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA16F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::Depth:
            return depthFormat_;
        }
        return VK_FORMAT_R8G8B8A8_UNORM;
    }

    uint32_t findMemoryType_(uint32_t typeFilter,
                             VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) ==
                    properties) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    bool createBuffer_(uint32_t sizeBytes, VkBufferUsageFlags usage,
                       const void* data, VkBuffer* buffer,
                       VkDeviceMemory* memory,
                       VkMemoryPropertyFlags properties =
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
        VkBufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = sizeBytes;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &info, nullptr, buffer) != VK_SUCCESS) {
            return false;
        }
        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device_, *buffer, &memReqs);
        uint32_t typeIndex = findMemoryType_(memReqs.memoryTypeBits, properties);
        if (typeIndex == UINT32_MAX) {
            return false;
        }
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = typeIndex;
        if (vkAllocateMemory(device_, &allocInfo, nullptr, memory) !=
            VK_SUCCESS) {
            return false;
        }
        vkBindBufferMemory(device_, *buffer, *memory, 0);
        if (data && sizeBytes > 0 &&
            (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            void* mapped = nullptr;
            vkMapMemory(device_, *memory, 0, sizeBytes, 0, &mapped);
            std::memcpy(mapped, data, sizeBytes);
            vkUnmapMemory(device_, *memory);
        }
        return true;
    }

    bool createImage_(uint32_t width, uint32_t height, VkFormat format,
                      VkImageTiling tiling, VkImageUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      uint32_t arrayLayers, VkImage* image,
                      VkDeviceMemory* memory) {
        VkImageCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent.width = width;
        info.extent.height = height;
        info.extent.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = arrayLayers;
        info.format = format;
        info.tiling = tiling;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = usage;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (arrayLayers == 6) {
            info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }
        if (vkCreateImage(device_, &info, nullptr, image) != VK_SUCCESS) {
            return false;
        }
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device_, *image, &memReqs);
        uint32_t typeIndex = findMemoryType_(memReqs.memoryTypeBits, properties);
        if (typeIndex == UINT32_MAX) {
            return false;
        }
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = typeIndex;
        if (vkAllocateMemory(device_, &allocInfo, nullptr, memory) !=
            VK_SUCCESS) {
            return false;
        }
        vkBindImageMemory(device_, *image, *memory, 0);
        return true;
    }

    VkImageView createImageView_(VkImage image, VkFormat format,
                                 VkImageViewType type,
                                 VkImageAspectFlags aspect,
                                 uint32_t mipLevels,
                                 uint32_t arrayLayers) {
        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = type;
        info.format = format;
        info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.subresourceRange.aspectMask = aspect;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = mipLevels;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = arrayLayers;
        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(device_, &info, nullptr, &view) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        return view;
    }

    VkSampler createSampler_(VkFilter filter) {
        VkSamplerCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = filter;
        info.minFilter = filter;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.anisotropyEnable = VK_FALSE;
        info.maxAnisotropy = 1.0f;
        info.maxLod = 0.0f;
        VkSampler sampler = VK_NULL_HANDLE;
        if (vkCreateSampler(device_, &info, nullptr, &sampler) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        return sampler;
    }

    VkShaderModule createShaderModule_(const std::vector<uint32_t>& spirv) {
        VkShaderModuleCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = spirv.size() * sizeof(uint32_t);
        info.pCode = spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &info, nullptr, &module) !=
            VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        return module;
    }

    void transitionImage_(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkImageAspectFlags aspect, uint32_t layerCount,
                          VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                          VkPipelineStageFlags srcStage,
                          VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = layerCount;
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);
    }

    // Uploads one face/layer of a texture using a single-time command
    // buffer, then transitions it to the shader-read layout.
    void uploadPixels_(TextureData& texture, uint32_t layer,
                       uint32_t layerCount, const void* pixels) {
        uint32_t bytesPerPixel = 4;
        if (texture.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
            bytesPerPixel = 8;
        }
        uint32_t sizeBytes = texture.width * texture.height * bytesPerPixel;

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        if (!createBuffer_(sizeBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels,
                           &staging, &stagingMemory,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            return;
        }

        VkCommandBuffer cmd = beginSingleTimeCommands_();
        if (texture.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            transitionImage_(cmd, texture.image, texture.layout,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_ASPECT_COLOR_BIT, layerCount, 0,
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT);
            texture.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }

        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {texture.width, texture.height, 1};
        vkCmdCopyBufferToImage(cmd, staging, texture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);

        transitionImage_(cmd, texture.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_IMAGE_ASPECT_COLOR_BIT, layerCount,
                         VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        texture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        endSingleTimeCommands_(cmd);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, stagingMemory, nullptr);
    }

    VkCommandBuffer beginSingleTimeCommands_() {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool_;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device_, &allocInfo, &cmd);
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);
        return cmd;
    }

    void endSingleTimeCommands_(VkCommandBuffer cmd) {
        vkEndCommandBuffer(cmd);
        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue_, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue_);
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    }

    // ---- execute helpers ----

    ShaderData* findShader_(uint64_t id) {
        auto it = shaders_.find(id);
        return it == shaders_.end() ? nullptr : it->second.get();
    }

    BufferData* findBuffer_(uint64_t id) {
        auto it = buffers_.find(id);
        return it == buffers_.end() ? nullptr : it->second.get();
    }

    TextureData* findTexture_(uint64_t id) {
        auto it = textures_.find(id);
        return it == textures_.end() ? nullptr : it->second.get();
    }

    void writeUniform_(const RenderCommand& command, uint32_t bytes) {
        if (!currentShader_ || !currentShader_->uniformCache) {
            return;
        }
        for (const UniformInfo& u : currentShader_->layout.uniforms) {
            if (u.name == command.uniformName) {
                uint32_t copyBytes = std::min(bytes, u.size);
                std::memcpy(currentShader_->uniformCache.get() + u.offset,
                            command.uniformData, copyBytes);
                return;
            }
        }
    }

    void writeUniformArray_(const RenderCommand& command) {
        if (!currentShader_ || !currentShader_->uniformCache) {
            return;
        }
        for (const UniformInfo& u : currentShader_->layout.uniforms) {
            if (u.name == command.uniformName) {
                uint32_t copyBytes =
                    std::min(command.uniformCount * 16u, u.size);
                std::memcpy(currentShader_->uniformCache.get() + u.offset,
                            command.uniformArrayData, copyBytes);
                return;
            }
        }
    }

    void executeSetTexture_(const RenderCommand& command) {
        if (command.textureSlot < 0 ||
            command.textureSlot >= (int)kMaxSamplerBindings) {
            return;
        }
        TextureData* texture = findTexture_(command.textureId);
        if (!texture) {
            return;
        }
        currentTextures_[command.textureSlot] = texture;

        VkDescriptorImageInfo imageInfo = {};
        imageInfo.sampler = texture->sampler;
        imageInfo.imageView = texture->view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frames_[frameIndex_].descriptorSet;
        write.dstBinding = 1 + (uint32_t)command.textureSlot;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    VkCommandBuffer currentCommandBuffer_() {
        return frames_[frameIndex_].commandBuffer;
    }

    // Lazily begins the render pass for the current target. Attachment
    // layout transitions are handled by the render pass itself (initialLayout
    // is UNDEFINED so previous contents are discarded).
    void beginRenderPass_() {
        if (renderPassActive_) {
            return;
        }
        VkCommandBuffer cmd = currentCommandBuffer_();
        VkRenderPass renderPass = swapchainRenderPass_;
        VkFramebuffer framebuffer = swapchainFramebuffers_[imageIndex_];
        VkExtent2D extent = swapchainExtent_;
        VkClearValue clearValues[2] = {};
        uint32_t clearCount = 1;
        clearValues[0].color = clearColor_;

        if (currentTargetId_ != 0) {
            auto it = renderTargets_.find(currentTargetId_);
            if (it != renderTargets_.end()) {
                renderPass = it->second->renderPass;
                framebuffer = it->second->framebuffer;
                extent.width = it->second->width;
                extent.height = it->second->height;
                if (it->second->isDepthOnly) {
                    clearValues[0].depthStencil = {1.0f, 0};
                } else if (it->second->withDepth) {
                    clearValues[1].depthStencil = {1.0f, 0};
                    clearCount = 2;
                }
            }
        } else {
            clearValues[1].depthStencil = {1.0f, 0};
            clearCount = 2;
        }

        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = renderPass;
        info.framebuffer = framebuffer;
        info.renderArea.offset = {0, 0};
        info.renderArea.extent = extent;
        info.clearValueCount = clearCount;
        info.pClearValues = clearValues;
        vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);

        renderPassActive_ = true;
        activeRenderPass_ = renderPass;
    }

    void endRenderPass_() {
        if (!renderPassActive_) {
            return;
        }
        vkCmdEndRenderPass(currentCommandBuffer_());
        renderPassActive_ = false;
        activeRenderPass_ = VK_NULL_HANDLE;
    }

    VkPipeline pipelineForShader_(ShaderData* shader,
                                  VkRenderPass renderPass) {
        auto it = shader->pipelines.find(renderPass);
        if (it != shader->pipelines.end()) {
            return it->second;
        }
        VkPipeline pipeline = createGraphicsPipeline_(*shader, renderPass);
        shader->pipelines[renderPass] = pipeline;
        return pipeline;
    }

    VkPipeline createGraphicsPipeline_(ShaderData& shader,
                                       VkRenderPass renderPass) {
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = shader.vertexModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = shader.fragmentModule;
        stages[1].pName = "main";

        // Standard interleaved vertex format (see ion::Vertex).
        VkVertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.stride = 48;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attributes[4] = {};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12};
        attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, 28};
        attributes[3] = {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 40};

        VkPipelineVertexInputStateCreateInfo vertexInput = {};
        vertexInput.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 4;
        vertexInput.pVertexAttributeDescriptions = attributes;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample = {};
        multisample.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencil.minDepthBounds = 0.0f;
        depthStencil.maxDepthBounds = 1.0f;

        VkPipelineColorBlendAttachmentState colorBlend = {};
        colorBlend.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        // Offscreen targets use alpha blending (matching the Metal backend);
        // the swapchain pass does not.
        if (renderPass != swapchainRenderPass_) {
            colorBlend.blendEnable = VK_TRUE;
            colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlend.dstColorBlendFactor =
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlend.dstAlphaBlendFactor =
                VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }

        VkPipelineColorBlendStateCreateInfo colorBlendState = {};
        colorBlendState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendState.attachmentCount = 1;
        colorBlendState.pAttachments = &colorBlend;

        // Use dynamic viewport/scissor and, when available, dynamic depth
        // write enable.
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        if (depthWriteDynamic_) {
            dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE);
        }
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &colorBlendState;
        info.pDynamicState = &dynamicState;
        info.layout = pipelineLayout_;
        info.renderPass = renderPass;
        info.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info,
                                      nullptr, &pipeline) != VK_SUCCESS) {
            ION_LOG_ERROR("Failed to create Vulkan graphics pipeline");
            return VK_NULL_HANDLE;
        }
        return pipeline;
    }

    void executeDraw_(const RenderCommand& command, bool indexed) {
        if (!currentShader_) {
            return;
        }
        beginRenderPass_();

        VkCommandBuffer cmd = currentCommandBuffer_();
        VkPipeline pipeline =
            pipelineForShader_(currentShader_, activeRenderPass_);
        if (!pipeline) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)swapchainExtent_.width;
        viewport.height = (float)swapchainExtent_.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        if (currentTargetId_ != 0) {
            auto it = renderTargets_.find(currentTargetId_);
            if (it != renderTargets_.end()) {
                viewport.width = (float)it->second->width;
                viewport.height = (float)it->second->height;
            }
        }
        VkRect2D scissor = {{0, 0},
                            {(uint32_t)viewport.width,
                             (uint32_t)viewport.height}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        if (depthWriteDynamic_ && cmdSetDepthWriteEnableEXT_) {
            cmdSetDepthWriteEnableEXT_(cmd, drawDepthWrite_ ? VK_TRUE
                                                           : VK_FALSE);
        }

        // Upload the active shader's uniforms into the per-frame arena and
        // select the region with a dynamic offset.
        Frame& frame = frames_[frameIndex_];
        uint32_t uboOffset = frame.uboOffset;
        if (currentShader_->uniformCache && currentShader_->uboSize > 0) {
            if (uboOffset + kUboSize > kUboArenaBytes) {
                ION_LOG_WARN(
                    "Per-frame uniform arena exhausted; uniform upload "
                    "skipped");
                uboOffset = 0;
            } else {
                std::memcpy((uint8_t*)frame.uboArena.mapped + uboOffset,
                            currentShader_->uniformCache.get(),
                            currentShader_->uboSize);
            }
        }
        frame.uboOffset = uboOffset + kUboSize;

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_, 0, 1, &frame.descriptorSet,
                                1, &uboOffset);

        if (currentVertexBuffer_) {
            VkBuffer vertexBuffers[1] = {currentVertexBuffer_->buffer};
            VkDeviceSize offsets[1] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        }

        if (indexed && currentIndexBuffer_) {
            vkCmdBindIndexBuffer(
                cmd, currentIndexBuffer_->buffer, 0,
                currentIndexBuffer_->is16Bit ? VK_INDEX_TYPE_UINT16
                                             : VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, command.count, 1, command.startIndex, 0, 0);
        } else {
            vkCmdDraw(cmd, command.count, 1, 0, 0);
        }
    }

    // ---- buffer backing (vertex/index) ----

    uint64_t createBuffer_(uint32_t sizeBytes, VkBufferUsageFlags usage,
                           const void* data) {
        if (!initialized_ || sizeBytes == 0) {
            return 0;
        }
        std::unique_ptr<BufferData> buffer(new BufferData());
        if (!createBuffer_(sizeBytes, usage, data, &buffer->buffer,
                           &buffer->memory)) {
            return 0;
        }
        buffer->size = sizeBytes;
        uint64_t id = nextId_++;
        buffers_[id] = std::move(buffer);
        return id;
    }

    void destroyBuffer_(uint64_t id) {
        auto it = buffers_.find(id);
        if (it == buffers_.end()) {
            return;
        }
        if (currentVertexBuffer_ == it->second.get()) {
            currentVertexBuffer_ = nullptr;
        }
        if (currentIndexBuffer_ == it->second.get()) {
            currentIndexBuffer_ = nullptr;
        }
        vkDestroyBuffer(device_, it->second->buffer, nullptr);
        vkFreeMemory(device_, it->second->memory, nullptr);
        buffers_.erase(it);
    }

    void updateBuffer_(uint64_t id, uint32_t offsetBytes, uint32_t sizeBytes,
                       const void* data) {
        auto it = buffers_.find(id);
        if (it == buffers_.end() || !data) {
            return;
        }
        BufferData& buffer = *it->second;
        if (offsetBytes + sizeBytes > buffer.size) {
            return;
        }
        void* mapped = nullptr;
        vkMapMemory(device_, buffer.memory, offsetBytes, sizeBytes, 0, &mapped);
        std::memcpy(mapped, data, sizeBytes);
        vkUnmapMemory(device_, buffer.memory);
    }

    // ---- cleanup ----

    void destroyTextureData_(TextureData* texture) {
        if (texture->sampler) {
            vkDestroySampler(device_, texture->sampler, nullptr);
        }
        if (texture->view) {
            vkDestroyImageView(device_, texture->view, nullptr);
        }
        if (texture->image) {
            vkDestroyImage(device_, texture->image, nullptr);
        }
        if (texture->memory) {
            vkFreeMemory(device_, texture->memory, nullptr);
        }
        texture->sampler = VK_NULL_HANDLE;
        texture->view = VK_NULL_HANDLE;
        texture->image = VK_NULL_HANDLE;
        texture->memory = VK_NULL_HANDLE;
    }

    void destroyAllTextures_() {
        for (auto& pair : textures_) {
            destroyTextureData_(pair.second.get());
        }
        textures_.clear();
    }

    void destroyAllTargets_() {
        for (auto& pair : renderTargets_) {
            if (pair.second->framebuffer) {
                vkDestroyFramebuffer(device_, pair.second->framebuffer,
                                     nullptr);
            }
            if (pair.second->renderPass) {
                vkDestroyRenderPass(device_, pair.second->renderPass, nullptr);
            }
        }
        renderTargets_.clear();
    }

    void destroyAllShaders_() {
        for (auto& pair : shaders_) {
            for (auto& pipeline : pair.second->pipelines) {
                vkDestroyPipeline(device_, pipeline.second, nullptr);
            }
            if (pair.second->vertexModule) {
                vkDestroyShaderModule(device_, pair.second->vertexModule,
                                      nullptr);
            }
            if (pair.second->fragmentModule) {
                vkDestroyShaderModule(device_, pair.second->fragmentModule,
                                      nullptr);
            }
        }
        shaders_.clear();
    }

    void destroyAllBuffers_() {
        for (auto& pair : buffers_) {
            vkDestroyBuffer(device_, pair.second->buffer, nullptr);
            vkFreeMemory(device_, pair.second->memory, nullptr);
        }
        buffers_.clear();
    }

    void fillGpuInfo_() {
        gpuInfo_.backend = RendererBackend::Vulkan;
        gpuInfo_.name = deviceProps_.deviceName;
        switch (deviceProps_.vendorID) {
        case 0x10DE:
            gpuInfo_.vendor = "NVIDIA";
            break;
        case 0x1002:
            gpuInfo_.vendor = "AMD";
            break;
        case 0x8086:
            gpuInfo_.vendor = "Intel";
            break;
        default:
            gpuInfo_.vendor = "Unknown";
            break;
        }
        char version[32];
        std::snprintf(version, sizeof(version), "%u.%u.%u",
                      VK_API_VERSION_MAJOR(deviceProps_.apiVersion),
                      VK_API_VERSION_MINOR(deviceProps_.apiVersion),
                      VK_API_VERSION_PATCH(deviceProps_.apiVersion));
        gpuInfo_.driverVersion = version;

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
            if (memProps.memoryHeaps[i].flags &
                VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                gpuInfo_.videoMemoryBytes =
                    memProps.memoryHeaps[i].size;
            }
        }
    }

    // ---- state ----

    bool initialized_ = false;
    bool vsync_ = true;

    void* windowHandle_ = nullptr;
    void* displayHandle_ = nullptr;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties deviceProps_ = {};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    QueueFamilyIndices queueIndices_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSwapchainKHR oldSwapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surfaceFormat_ = {};
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D swapchainExtent_ = {};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> swapchainFramebuffers_;
    VkRenderPass swapchainRenderPass_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;

    Frame frames_[kFramesInFlight] = {};
    uint32_t frameIndex_ = 0;
    uint32_t imageIndex_ = 0;
    bool frameValid_ = false;

    bool extendedDynamicState_ = false;
    bool depthWriteDynamic_ = false;
    PFN_vkCmdSetDepthWriteEnableEXT cmdSetDepthWriteEnableEXT_ = nullptr;

    // Per-frame draw state (reset each beginFrame).
    ShaderData* currentShader_ = nullptr;
    uint64_t currentTargetId_ = 0;
    TextureData* currentTextures_[kMaxSamplerBindings] = {};
    BufferData* currentVertexBuffer_ = nullptr;
    BufferData* currentIndexBuffer_ = nullptr;
    VkClearColorValue clearColor_ = {};
    bool renderPassActive_ = false;
    VkRenderPass activeRenderPass_ = VK_NULL_HANDLE;
    bool drawDepthWrite_ = true;

    uint64_t nextId_ = 1;
    GPUInfo gpuInfo_;

    std::unordered_map<uint64_t, std::unique_ptr<ShaderData>> shaders_;
    std::unordered_map<uint64_t, std::unique_ptr<TextureData>> textures_;
    std::unordered_map<uint64_t, std::unique_ptr<RenderTargetData>>
        renderTargets_;
    std::unordered_map<uint64_t, std::unique_ptr<BufferData>> buffers_;
};

RenderBackend* createVulkanBackend() {
    return new VulkanBackend();
}

} // namespace ion