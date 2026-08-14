#include "../../core/RenderBackend.hpp"

#include <ion/core/Log.hpp>
#include <ion/math/Matrix4.hpp>

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace ion {

namespace {

struct UniformSlot {
    uint32_t offset = 0;
    uint32_t size = 0;
};

// Color attachment format of the currently bound render target. Selects which
// pipeline variant a shader uses (a pipeline's color format must match the
// active render pass's attachment format).
enum class TargetColorFormat : int {
    Swapchain,  // BGRA8Unorm (the window's CAMetalLayer)
    RGBA8,      // RGBA8Unorm
    RGBA16F,    // RGBA16Float
    DepthOnly,  // no color attachment (shadow maps)
};

struct ShaderData {
    std::unordered_map<int, id<MTLRenderPipelineState>> pipelines;
    std::unordered_map<std::string, UniformSlot> uniforms;
    uint32_t uniformSize = 0;
};

uint32_t uniformSizeForDataType(MTLDataType type) {
    switch (type) {
    case MTLDataTypeFloat:
    case MTLDataTypeInt:
    case MTLDataTypeUInt:
    case MTLDataTypeBool:
        return 4;
    case MTLDataTypeFloat2:
        return 8;
    case MTLDataTypeFloat3:
        return 12;
    case MTLDataTypeFloat4:
        return 16;
    case MTLDataTypeFloat2x2:
        return 16;
    case MTLDataTypeFloat2x3:
    case MTLDataTypeFloat3x2:
        return 24;
    case MTLDataTypeFloat3x3:
        return 36;
    case MTLDataTypeFloat2x4:
    case MTLDataTypeFloat4x2:
        return 32;
    case MTLDataTypeFloat3x4:
    case MTLDataTypeFloat4x3:
        return 48;
    case MTLDataTypeFloat4x4:
        return 64;
    default:
        return 0;
    }
}

MTLPixelFormat metalPixelFormat(TextureFormat format) {
    switch (format) {
    case TextureFormat::RGBA8:
        return MTLPixelFormatRGBA8Unorm;
    case TextureFormat::RGBA16F:
        return MTLPixelFormatRGBA16Float;
    case TextureFormat::Depth:
        return MTLPixelFormatDepth32Float;
    }
    return MTLPixelFormatRGBA8Unorm;
}

} // namespace

class MetalBackend : public RenderBackend {
public:
    ~MetalBackend() override {
        shutdown();
    }

    bool initialize(void* nativeView, const RendererConfig& config) override {
        if (initialized_) {
            return true;
        }
        @autoreleasepool {
            device_ = MTLCreateSystemDefaultDevice();
            if (!device_) {
                ION_LOG_ERROR("Metal: no system default device");
                return false;
            }
            commandQueue_ = [device_ newCommandQueue];
            config_ = config;

            view_ = (__bridge NSView*)nativeView;
            if (!view_) {
                ION_LOG_ERROR("Metal: null native view");
                return false;
            }

            [view_ setWantsLayer:YES];
            layer_ = [CAMetalLayer layer];
            layer_.device = device_;
            layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
            layer_.framebufferOnly = YES;
            layer_.contentsScale = [view_.window backingScaleFactor] > 0.0f
                                       ? [view_.window backingScaleFactor]
                                       : 1.0f;
            view_.layer = layer_;

            samplerLinear_ = createSampler_(YES);
            samplerNearest_ = createSampler_(NO);

            MTLDepthStencilDescriptor* dsDesc =
                [MTLDepthStencilDescriptor new];
            dsDesc.depthCompareFunction = MTLCompareFunctionLessEqual;
            dsDesc.depthWriteEnabled = YES;
            depthState_ = [device_ newDepthStencilStateWithDescriptor:dsDesc];

            MTLDepthStencilDescriptor* dsNoWriteDesc =
                [MTLDepthStencilDescriptor new];
            dsNoWriteDesc.depthCompareFunction = MTLCompareFunctionLessEqual;
            dsNoWriteDesc.depthWriteEnabled = NO;
            depthStateNoWrite_ =
                [device_ newDepthStencilStateWithDescriptor:dsNoWriteDesc];
        }
        initialized_ = true;
        return true;
    }

    void shutdown() override {
        if (view_) {
            @autoreleasepool {
                view_.layer = nil;
            }
        }
        shaders_.clear();
        textureData_.clear();
        renderTargets_.clear();
        vertexBuffers_.clear();
        indexBufferData_.clear();
        commandQueue_ = nil;
        depthTexture_ = nil;
        depthState_ = nil;
        depthStateNoWrite_ = nil;
        device_ = nil;
        view_ = nil;
        initialized_ = false;
    }

    GPUInfo gpuInfo() const override {
        GPUInfo info;
        info.backend = RendererBackend::Metal;
        @autoreleasepool {
            if (device_) {
                info.name = device_.name ? [device_.name UTF8String] : "";
                if ([device_ respondsToSelector:@selector(vendorName)]) {
                    NSString* vendorName = [(id)device_ valueForKey:@"vendorName"];
                    if (vendorName) {
                        info.vendor = [vendorName UTF8String];
                    }
                }
                info.videoMemoryBytes = device_.recommendedMaxWorkingSetSize;
            }
        }
        return info;
    }

    void beginFrame(uint32_t, uint32_t) override {
        @autoreleasepool {
            if (!layer_) {
                return;
            }
            double scale = layer_.contentsScale;
            if (scale <= 0.0) {
                scale = 1.0;
            }
            CGSize pixelSize = CGSizeMake(
                (CGFloat)((NSInteger)(layer_.bounds.size.width * scale)),
                (CGFloat)((NSInteger)(layer_.bounds.size.height * scale)));
            if (pixelSize.width <= 0 || pixelSize.height <= 0) {
                drawable_ = nil;
                return;
            }
            layer_.drawableSize = pixelSize;
            if (config_.antialiasSamples > 1) {
                uint64_t sizeKey = ((uint64_t)(uint32_t)pixelSize.width << 32) |
                                   (uint32_t)pixelSize.height;
                if (msaaSizeKey_ != sizeKey) {
                    MTLTextureDescriptor* desc =
                        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
                                                   MTLPixelFormatBGRA8Unorm
                                            width:(NSUInteger)pixelSize.width
                                            height:(NSUInteger)pixelSize.height
                                            mipmapped:NO];
                    desc.sampleCount = config_.antialiasSamples;
                    desc.textureType = MTLTextureType2DMultisample;
                    desc.storageMode = MTLStorageModePrivate;
                    desc.usage = MTLTextureUsageRenderTarget;
                    msaaTexture_ = [device_ newTextureWithDescriptor:desc];
                    msaaSizeKey_ = sizeKey;
                }
            }
            uint64_t sizeKey = ((uint64_t)(uint32_t)pixelSize.width << 32) |
                               (uint32_t)pixelSize.height;
            if (depthSizeKey_ != sizeKey) {
                MTLTextureDescriptor* depthDesc =
                    [MTLTextureDescriptor
                        texture2DDescriptorWithPixelFormat:
                            MTLPixelFormatDepth32Float
                                                       width:(NSUInteger)
                                                                pixelSize.width
                                                       height:(NSUInteger)
                                                                pixelSize.height
                                                       mipmapped:NO];
                if (config_.antialiasSamples > 1) {
                    depthDesc.textureType = MTLTextureType2DMultisample;
                    depthDesc.sampleCount = config_.antialiasSamples;
                } else {
                    depthDesc.textureType = MTLTextureType2D;
                }
                depthDesc.storageMode = MTLStorageModePrivate;
                depthDesc.usage = MTLTextureUsageRenderTarget;
                depthTexture_ = [device_ newTextureWithDescriptor:depthDesc];
                depthSizeKey_ = sizeKey;
            }
            drawable_ = [layer_ nextDrawable];
        }
    }

    void endFrame() override {
        @autoreleasepool {
            if (pendingCommands_.empty() && drawable_) {
                // Nothing recorded; still present a cleared frame.
                pendingCommands_.push_back(RenderCommand());
            }
            if (pendingCommands_.empty()) {
                return;
            }
            id<MTLCommandBuffer> commandBuffer = [commandQueue_ commandBuffer];
            commandBuffer.label = @"Ion Render Frame";

            // Split the command list into render passes at SetRenderTarget
            // boundaries. targetId 0 is the swapchain (window drawable).
            struct Segment {
                size_t begin = 0;
                size_t end = 0;
                uint64_t targetId = 0;
                Color clearColor = Color::black();
            };
            std::vector<Segment> segments;
            Color runningClear = currentClear_;
            uint64_t currentTarget = 0;
            size_t segmentStart = 0;
            for (size_t i = 0; i < pendingCommands_.size(); ++i) {
                const RenderCommand& c = pendingCommands_[i];
                if (c.type == RenderCommandType::SetRenderTarget) {
                    segments.push_back(
                        {segmentStart, i, currentTarget, runningClear});
                    currentTarget = c.targetId;
                    segmentStart = i + 1;
                } else if (c.type == RenderCommandType::Clear) {
                    runningClear = c.clearColor;
                }
            }
            segments.push_back(
                {segmentStart, pendingCommands_.size(), currentTarget,
                 runningClear});

            bool presented = false;
            for (const Segment& segment : segments) {
                if (segment.begin >= segment.end) {
                    continue;
                }
                bool isSwapchain = segment.targetId == 0;
                if (isSwapchain && !drawable_) {
                    continue;
                }
                id<MTLTexture> colorTexture = nil;
                id<MTLTexture> depthTexture = nil;
                MTLPixelFormat colorFormat = MTLPixelFormatBGRA8Unorm;
                bool hasColor = true;
                bool msaa = false;
                if (isSwapchain) {
                    colorTexture = msaaTexture_ ? msaaTexture_
                                                : drawable_.texture;
                    depthTexture = depthTexture_;
                    msaa = msaaTexture_ != nil;
                } else {
                    auto it = renderTargets_.find(segment.targetId);
                    if (it == renderTargets_.end()) {
                        continue;
                    }
                    RenderTargetData& rt = it->second;
                    if (rt.desc.format == TextureFormat::Depth) {
                        hasColor = false;
                        depthTexture = rt.depth;
                    } else {
                        colorTexture = rt.color;
                        colorFormat =
                            rt.desc.format == TextureFormat::RGBA16F
                                ? MTLPixelFormatRGBA16Float
                                : MTLPixelFormatRGBA8Unorm;
                        depthTexture = rt.depth;
                    }
                }
                currentColorFormat_ = hasColor
                                         ? (colorFormat ==
                                                    MTLPixelFormatRGBA16Float
                                                ? TargetColorFormat::RGBA16F
                                                : (isSwapchain
                                                       ? TargetColorFormat::
                                                             Swapchain
                                                       : TargetColorFormat::
                                                             RGBA8))
                                         : TargetColorFormat::DepthOnly;

                MTLRenderPassDescriptor* pass =
                    [MTLRenderPassDescriptor renderPassDescriptor];
                if (hasColor) {
                    pass.colorAttachments[0].texture = colorTexture;
                    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                    pass.colorAttachments[0].clearColor = MTLClearColorMake(
                        segment.clearColor.r, segment.clearColor.g,
                        segment.clearColor.b, segment.clearColor.a);
                    if (msaa) {
                        pass.colorAttachments[0].resolveTexture =
                            drawable_.texture;
                        pass.colorAttachments[0].storeAction =
                            MTLStoreActionStoreAndMultisampleResolve;
                    } else {
                        pass.colorAttachments[0].storeAction =
                            MTLStoreActionStore;
                    }
                }
                if (depthTexture) {
                    pass.depthAttachment.texture = depthTexture;
                    pass.depthAttachment.clearDepth = 1.0;
                    pass.depthAttachment.loadAction = MTLLoadActionClear;
                    pass.depthAttachment.storeAction = isSwapchain
                                                           ? MTLStoreActionDontCare
                                                           : MTLStoreActionStore;
                }

                id<MTLRenderCommandEncoder> encoder =
                    [commandBuffer renderCommandEncoderWithDescriptor:pass];
                [encoder setDepthStencilState:depthState_];
                for (size_t i = segment.begin; i < segment.end; ++i) {
                    executeCommand_(encoder, pendingCommands_[i]);
                }
                [encoder endEncoding];

                if (isSwapchain) {
                    [commandBuffer presentDrawable:drawable_];
                    presented = true;
                }
            }
            if (!presented && drawable_) {
                [commandBuffer presentDrawable:drawable_];
            }
            [commandBuffer commit];
            drawable_ = nil;
            pendingCommands_.clear();
        }
    }

    void execute(const RenderCommand& command) override {
        pendingCommands_.push_back(command);
    }

    uint64_t createShader(const char* vertexSource,
                          const char* fragmentSource) override {
        @autoreleasepool {
            NSError* error = nil;
            id<MTLLibrary> vertexLib =
                [device_ newLibraryWithSource:[NSString
                                                  stringWithUTF8String:vertexSource]
                                      options:nil
                                        error:&error];
            if (!vertexLib) {
                ION_LOG_ERROR("Metal: vertex shader compile failed: %s",
                              error.localizedDescription.UTF8String);
                return 0;
            }
            id<MTLLibrary> fragmentLib =
                [device_ newLibraryWithSource:[NSString
                                                  stringWithUTF8String:
                                                      fragmentSource]
                                      options:nil
                                        error:&error];
            if (!fragmentLib) {
                ION_LOG_ERROR("Metal: fragment shader compile failed: %s",
                              error.localizedDescription.UTF8String);
                return 0;
            }
            id<MTLFunction> vertexFn =
                [vertexLib newFunctionWithName:@"vertexShader"];
            if (!vertexFn) {
                vertexFn = [vertexLib newFunctionWithName:@"main"];
            }
            id<MTLFunction> fragmentFn =
                [fragmentLib newFunctionWithName:@"fragmentShader"];
            if (!fragmentFn) {
                fragmentFn = [fragmentLib newFunctionWithName:@"main"];
            }
            if (!vertexFn || !fragmentFn) {
                ION_LOG_ERROR(
                    "Metal: shader entry point 'vertexShader'/'fragmentShader' "
                    "not found");
                return 0;
            }

            MTLVertexDescriptor* vd = [MTLVertexDescriptor new];
            vd.attributes[0].format = MTLVertexFormatFloat3;
            vd.attributes[0].offset = 0;
            vd.attributes[0].bufferIndex = 30;
            vd.attributes[1].format = MTLVertexFormatFloat4;
            vd.attributes[1].offset = 12;
            vd.attributes[1].bufferIndex = 30;
            vd.attributes[2].format = MTLVertexFormatFloat2;
            vd.attributes[2].offset = 28;
            vd.attributes[2].bufferIndex = 30;
            vd.attributes[3].format = MTLVertexFormatFloat3;
            vd.attributes[3].offset = 40;
            vd.attributes[3].bufferIndex = 30;
            vd.layouts[30].stride = 48;
            vd.layouts[30].stepFunction = MTLVertexStepFunctionPerVertex;

            ShaderData data;
            NSError* pipelineError = nil;
            data.pipelines[(int)TargetColorFormat::Swapchain] =
                buildPipeline_(vertexFn, fragmentFn, vd,
                               MTLPixelFormatBGRA8Unorm, false, 0,
                               &pipelineError);
            data.pipelines[(int)TargetColorFormat::RGBA8] =
                buildPipeline_(vertexFn, fragmentFn, vd,
                               MTLPixelFormatRGBA8Unorm, true, 1,
                               &pipelineError);
            data.pipelines[(int)TargetColorFormat::RGBA16F] =
                buildPipeline_(vertexFn, fragmentFn, vd,
                               MTLPixelFormatRGBA16Float, true, 1,
                               &pipelineError);
            data.pipelines[(int)TargetColorFormat::DepthOnly] =
                buildPipeline_(vertexFn, fragmentFn, vd,
                               MTLPixelFormatInvalid, true, 1,
                               &pipelineError);
            bool anyPipeline = false;
            for (const auto& entry : data.pipelines) {
                anyPipeline = anyPipeline || entry.second != nil;
            }
            if (!anyPipeline) {
                ION_LOG_ERROR("Metal: pipeline state creation failed: %s",
                              pipelineError.localizedDescription.UTF8String);
                return 0;
            }
            reflectUniforms_(vertexFn, data);

            uint64_t id = nextId_++;
            shaders_.emplace(id, data);
            return id;
        }
    }

    void destroyShader(uint64_t id) override {
        shaders_.erase(id);
        if (id == activeShaderId_) {
            activeShaderId_ = 0;
            activeShader_ = nullptr;
        }
    }

    uint64_t createTexture(const TextureDesc& desc, const void* pixels) override {
        @autoreleasepool {
            MTLTextureDescriptor* tdesc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
                                           metalPixelFormat(desc.format)
                                                                    width:desc.width
                                                                    height:desc.height
                                                                    mipmapped:
                                                                        desc
                                                                            .generateMipmaps];
            tdesc.usage = MTLTextureUsageShaderRead;
            if (desc.generateMipmaps) {
                tdesc.usage |= MTLTextureUsageShaderWrite;
            }
            id<MTLTexture> texture = [device_ newTextureWithDescriptor:tdesc];
            if (!texture) {
                ION_LOG_ERROR("Metal: texture allocation failed");
                return 0;
            }
            if (pixels) {
                [texture replaceRegion:MTLRegionMake2D(0, 0, desc.width,
                                                       desc.height)
                           mipmapLevel:0
                             withBytes:pixels
                           bytesPerRow:desc.width * 4];
            }
            if (desc.generateMipmaps) {
                id<MTLCommandBuffer> blitBuffer = [commandQueue_ commandBuffer];
                id<MTLBlitCommandEncoder> blit =
                    [blitBuffer blitCommandEncoder];
                [blit generateMipmapsForTexture:texture];
                [blit endEncoding];
                [blitBuffer commit];
            }

            uint64_t id = nextId_++;
            textureData_[id] = {texture, desc};
            return id;
        }
    }

    void destroyTexture(uint64_t id) override {
        textureData_.erase(id);
    }

    uint64_t createCubemap(const TextureDesc& desc,
                           const void* const faces[6]) override {
        @autoreleasepool {
            MTLTextureDescriptor* tdesc = [MTLTextureDescriptor
                textureCubeDescriptorWithPixelFormat:metalPixelFormat(desc.format)
                                                size:desc.width
                                           mipmapped:desc.generateMipmaps];
            tdesc.usage = MTLTextureUsageShaderRead;
            if (desc.generateMipmaps) {
                tdesc.usage |= MTLTextureUsageShaderWrite;
            }
            id<MTLTexture> texture = [device_ newTextureWithDescriptor:tdesc];
            if (!texture) {
                ION_LOG_ERROR("Metal: cubemap allocation failed");
                return 0;
            }
            if (faces) {
                for (int i = 0; i < 6; ++i) {
                    if (!faces[i]) {
                        break;
                    }
                    [texture replaceRegion:MTLRegionMake2D(0, 0, desc.width,
                                                           desc.height)
                               mipmapLevel:0
                                     slice:i
                                 withBytes:faces[i]
                               bytesPerRow:desc.width * 4
                             bytesPerImage:desc.width * desc.height * 4];
                }
            }
            if (desc.generateMipmaps) {
                id<MTLCommandBuffer> blitBuffer = [commandQueue_ commandBuffer];
                id<MTLBlitCommandEncoder> blit =
                    [blitBuffer blitCommandEncoder];
                [blit generateMipmapsForTexture:texture];
                [blit endEncoding];
                [blitBuffer commit];
            }

            uint64_t id = nextId_++;
            textureData_[id] = {texture, desc};
            return id;
        }
    }

    RenderTargetCreateInfo createRenderTarget(
        const RenderTargetDesc& desc) override {
        @autoreleasepool {
            RenderTargetCreateInfo info;
            if (desc.width == 0 || desc.height == 0) {
                return info;
            }
            if (desc.format != TextureFormat::Depth) {
                MTLPixelFormat colorFormat =
                    desc.format == TextureFormat::RGBA16F
                        ? MTLPixelFormatRGBA16Float
                        : MTLPixelFormatRGBA8Unorm;
                MTLTextureDescriptor* cdesc = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:colorFormat
                                                width:desc.width
                                               height:desc.height
                                              mipmapped:NO];
                cdesc.usage = MTLTextureUsageRenderTarget |
                              MTLTextureUsageShaderRead;
                cdesc.storageMode = MTLStorageModePrivate;
                id<MTLTexture> color = [device_ newTextureWithDescriptor:cdesc];
                if (!color) {
                    ION_LOG_ERROR("Metal: render target color allocation failed");
                    return info;
                }
                uint64_t colorId = nextId_++;
                textureData_[colorId] = {color,
                                         {desc.width, desc.height, desc.format,
                                          true, false}};
                info.colorTextureId = colorId;
            }
            if (desc.withDepth) {
                MTLTextureDescriptor* ddesc = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:
                        MTLPixelFormatDepth32Float
                                                width:desc.width
                                               height:desc.height
                                              mipmapped:NO];
                ddesc.usage = MTLTextureUsageRenderTarget |
                              MTLTextureUsageShaderRead;
                ddesc.storageMode = MTLStorageModePrivate;
                id<MTLTexture> depth = [device_ newTextureWithDescriptor:ddesc];
                if (!depth) {
                    ION_LOG_ERROR("Metal: render target depth allocation failed");
                    return info;
                }
                uint64_t depthId = nextId_++;
                textureData_[depthId] = {depth,
                                         {desc.width, desc.height,
                                          TextureFormat::Depth, false, false}};
                info.depthTextureId = depthId;
            }

            RenderTargetData rt;
            rt.desc = desc;
            if (info.colorTextureId) {
                rt.color = textureData_[info.colorTextureId].texture;
            }
            if (info.depthTextureId) {
                rt.depth = textureData_[info.depthTextureId].texture;
            }
            uint64_t targetId = nextId_++;
            renderTargets_[targetId] = rt;
            info.targetId = targetId;
            return info;
        }
    }

    void destroyRenderTarget(uint64_t id) override {
        auto it = renderTargets_.find(id);
        if (it == renderTargets_.end()) {
            return;
        }
        for (auto texIt = textureData_.begin(); texIt != textureData_.end();) {
            if (texIt->second.texture == it->second.color ||
                texIt->second.texture == it->second.depth) {
                texIt = textureData_.erase(texIt);
            } else {
                ++texIt;
            }
        }
        renderTargets_.erase(it);
    }

    uint64_t createVertexBuffer(uint32_t sizeBytes,
                                const void* data) override {
        @autoreleasepool {
            id<MTLBuffer> buffer =
                [device_ newBufferWithBytes:data
                                     length:sizeBytes
                                    options:MTLResourceStorageModeShared];
            if (!buffer) {
                ION_LOG_ERROR("Metal: vertex buffer allocation failed");
                return 0;
            }
            uint64_t id = nextId_++;
            vertexBuffers_.emplace(id, buffer);
            return id;
        }
    }

    void destroyVertexBuffer(uint64_t id) override {
        vertexBuffers_.erase(id);
    }

    void updateVertexBuffer(uint64_t id, uint32_t offsetBytes,
                            uint32_t sizeBytes, const void* data) override {
        @autoreleasepool {
            auto it = vertexBuffers_.find(id);
            if (it != vertexBuffers_.end()) {
                memcpy((uint8_t*)it->second.contents + offsetBytes, data,
                       sizeBytes);
            }
        }
    }

    uint64_t createIndexBuffer(uint32_t count, bool is16Bit,
                               const void* data) override {
        @autoreleasepool {
            NSUInteger elementSize = is16Bit ? 2 : 4;
            id<MTLBuffer> buffer =
                [device_ newBufferWithBytes:data
                                     length:count * elementSize
                                    options:MTLResourceStorageModeShared];
            if (!buffer) {
                ION_LOG_ERROR("Metal: index buffer allocation failed");
                return 0;
            }
            uint64_t id = nextId_++;
            indexBufferData_[id] = {buffer, count, is16Bit};
            return id;
        }
    }

    void destroyIndexBuffer(uint64_t id) override {
        indexBufferData_.erase(id);
    }

    void updateIndexBuffer(uint64_t id, uint32_t offsetBytes,
                           uint32_t sizeBytes, const void* data) override {
        @autoreleasepool {
            auto it = indexBufferData_.find(id);
            if (it != indexBufferData_.end()) {
                memcpy((uint8_t*)it->second.buffer.contents + offsetBytes,
                       data, sizeBytes);
            }
        }
    }

private:
    struct TextureData {
        id<MTLTexture> texture = nil;
        TextureDesc desc;
    };

    struct IndexData {
        id<MTLBuffer> buffer = nil;
        uint32_t count = 0;
        bool is16Bit = true;
    };

    struct RenderTargetData {
        id<MTLTexture> color = nil;
        id<MTLTexture> depth = nil;
        RenderTargetDesc desc;
    };

    void reflectUniforms_(id<MTLFunction> function, ShaderData& data) {
        @autoreleasepool {
            MTLAutoreleasedArgument reflectedArg = nil;
            id<MTLArgumentEncoder> encoder =
                [function newArgumentEncoderWithBufferIndex:0
                                                 reflection:&reflectedArg];
            if (!encoder || !reflectedArg) {
                return;
            }
            MTLStructType* structType = reflectedArg.bufferStructType;
            if (!structType) {
                return;
            }
            uint32_t maxEnd = 0;
            for (MTLStructMember* member in structType.members) {
                uint32_t size = uniformSizeForDataType(member.dataType);
                if (size == 0) {
                    continue;
                }
                if (member.dataType == MTLDataTypeArray) {
                    MTLArrayType *arrayType = [member arrayType];
                    if (arrayType.arrayLength > 1) {
                        size *= (uint32_t)arrayType.arrayLength;
                    }
                }

                UniformSlot slot;
                slot.offset = (uint32_t)member.offset;
                slot.size = size;
                data.uniforms[member.name.UTF8String] = slot;
                maxEnd = std::max(maxEnd, slot.offset + slot.size);
            }
            data.uniformSize =
                std::max((uint32_t)encoder.encodedLength, maxEnd);
            if (data.uniformSize > 0) {
                ION_LOG_INFO("Metal: shader uniform block is %u bytes (%zu "
                             "uniforms)",
                             data.uniformSize, data.uniforms.size());
            }
        }
    }

    id<MTLRenderPipelineState> buildPipeline_(id<MTLFunction> vertexFn,
                                              id<MTLFunction> fragmentFn,
                                              MTLVertexDescriptor* vd,
                                              MTLPixelFormat colorFormat,
                                              bool blendingEnabled,
                                              NSUInteger sampleCount,
                                              NSError** error) {
        @autoreleasepool {
            MTLRenderPipelineDescriptor* pd =
                [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = vertexFn;
            pd.fragmentFunction = fragmentFn;
            pd.vertexDescriptor = vd;
            pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            pd.colorAttachments[0].pixelFormat = colorFormat;
            if (blendingEnabled && colorFormat != MTLPixelFormatInvalid) {
                pd.colorAttachments[0].blendingEnabled = YES;
                pd.colorAttachments[0].sourceRGBBlendFactor =
                    MTLBlendFactorSourceAlpha;
                pd.colorAttachments[0].destinationRGBBlendFactor =
                    MTLBlendFactorOneMinusSourceAlpha;
                pd.colorAttachments[0].sourceAlphaBlendFactor =
                    MTLBlendFactorSourceAlpha;
                pd.colorAttachments[0].destinationAlphaBlendFactor =
                    MTLBlendFactorOneMinusSourceAlpha;
            }
            if (sampleCount > 1) {
                pd.sampleCount = sampleCount;
            }
            return [device_ newRenderPipelineStateWithDescriptor:pd
                                                           error:error];
        }
    }

    id<MTLSamplerState> createSampler_(bool linear) {
        @autoreleasepool {
            MTLSamplerDescriptor* desc = [MTLSamplerDescriptor new];
            desc.magFilter = linear ? MTLSamplerMinMagFilterLinear
                                    : MTLSamplerMinMagFilterNearest;
            desc.minFilter = linear ? MTLSamplerMinMagFilterLinear
                                    : MTLSamplerMinMagFilterNearest;
            desc.mipFilter = MTLSamplerMipFilterLinear;
            desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
            desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
            return [device_ newSamplerStateWithDescriptor:desc];
        }
    }

    void executeCommand_(id<MTLRenderCommandEncoder> encoder,
                         const RenderCommand& command) {
        switch (command.type) {
        case RenderCommandType::Clear:
            break;
        case RenderCommandType::UseShader: {
            auto it = shaders_.find(command.shaderId);
            if (it == shaders_.end()) {
                break;
            }
            activeShaderId_ = command.shaderId;
            auto pipelineIt =
                it->second.pipelines.find((int)currentColorFormat_);
            if (pipelineIt != it->second.pipelines.end() &&
                pipelineIt->second != nil) {
                [encoder setRenderPipelineState:pipelineIt->second];
            } else {
                activeShaderId_ = 0;
                break;
            }
            if (uniformBlob_.size() < it->second.uniformSize) {
                uniformBlob_.assign(it->second.uniformSize, 0);
            }
            std::fill(uniformBlob_.begin(), uniformBlob_.end(), 0);
            break;
        }
        case RenderCommandType::SetTexture: {
            auto it = textureData_.find(command.textureId);
            if (it == textureData_.end()) {
                break;
            }
            [encoder setFragmentTexture:it->second.texture
                                atIndex:command.textureSlot];
            id<MTLSamplerState> sampler =
                it->second.desc.filterLinear ? samplerLinear_ : samplerNearest_;
            [encoder setFragmentSamplerState:sampler
                                     atIndex:command.textureSlot];
            break;
        }
        case RenderCommandType::SetRenderTarget:
            // Handled at pass boundaries in endFrame.
            break;
        case RenderCommandType::SetDepthWrite:
            [encoder setDepthStencilState:command.count ? depthState_
                                                       : depthStateNoWrite_];
            break;
        case RenderCommandType::BindVertexBuffer: {
            auto it = vertexBuffers_.find(command.vertexBufferId);
            if (it == vertexBuffers_.end()) {
                break;
            }
            [encoder setVertexBuffer:it->second offset:0 atIndex:30];
            break;
        }
        case RenderCommandType::BindIndexBuffer: {
            if (indexBufferData_.count(command.indexBufferId) > 0) {
                indexBufferId_ = command.indexBufferId;
            }
            break;
        }
        case RenderCommandType::SetUniformFloat:
        case RenderCommandType::SetUniformVec2:
        case RenderCommandType::SetUniformVec3:
        case RenderCommandType::SetUniformVec4:
        case RenderCommandType::SetUniformMat4: {
            if (uniformBlob_.empty()) {
                break;
            }
            auto shader = shaders_.find(activeShaderId_);
            if (shader == shaders_.end()) {
                break;
            }
            auto it = shader->second.uniforms.find(command.uniformName);
            if (it == shader->second.uniforms.end()) {
                break;
            }
            const UniformSlot& slot = it->second;
            if (slot.offset + slot.size > uniformBlob_.size()) {
                break;
            }
            uint32_t copyBytes = std::min(slot.size, command.uniformBytes);
            memcpy(uniformBlob_.data() + slot.offset, command.uniformData,
                   copyBytes);
            break;
        }
        case RenderCommandType::SetUniformVec4Array: {
            if (uniformBlob_.empty()) {
                break;
            }
            auto shader = shaders_.find(activeShaderId_);
            if (shader == shaders_.end()) {
                break;
            }
            auto it = shader->second.uniforms.find(command.uniformName);
            if (it == shader->second.uniforms.end()) {
                break;
            }
            const UniformSlot& slot = it->second;
            if (slot.offset + slot.size > uniformBlob_.size()) {
                break;
            }
            uint32_t copyBytes =
                std::min(slot.size, command.uniformCount * 16u);
            memcpy(uniformBlob_.data() + slot.offset,
                   command.uniformArrayData, copyBytes);
            break;
        }
        case RenderCommandType::Draw: {
            if (!uniformBlob_.empty()) {
                [encoder setVertexBytes:uniformBlob_.data()
                                 length:uniformBlob_.size()
                                atIndex:0];
                [encoder setFragmentBytes:uniformBlob_.data()
                                   length:uniformBlob_.size()
                                  atIndex:0];
            }
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:command.count];
            break;
        }
        case RenderCommandType::DrawIndexed: {
            if (!uniformBlob_.empty()) {
                [encoder setVertexBytes:uniformBlob_.data()
                                 length:uniformBlob_.size()
                                atIndex:0];
                [encoder setFragmentBytes:uniformBlob_.data()
                                   length:uniformBlob_.size()
                                  atIndex:0];
            }
            auto indexIt = indexBufferData_.find(indexBufferId_);
            if (indexIt == indexBufferData_.end()) {
                break;
            }
            const IndexData& indexBuffer = indexIt->second;
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:command.count
                                 indexType:indexBuffer.is16Bit
                                               ? MTLIndexTypeUInt16
                                               : MTLIndexTypeUInt32
                               indexBuffer:indexBuffer.buffer
                         indexBufferOffset:command.startIndex *
                                           (indexBuffer.is16Bit ? 2 : 4)];
            break;
        }
        }
    }

    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> commandQueue_ = nil;
    NSView* view_ = nil;
    CAMetalLayer* layer_ = nil;
    id<CAMetalDrawable> drawable_ = nil;
    id<MTLTexture> msaaTexture_ = nil;
    uint64_t msaaSizeKey_ = 0;
    id<MTLTexture> depthTexture_ = nil;
    uint64_t depthSizeKey_ = 0;
    id<MTLDepthStencilState> depthState_ = nil;
    id<MTLDepthStencilState> depthStateNoWrite_ = nil;
    id<MTLSamplerState> samplerLinear_ = nil;
    id<MTLSamplerState> samplerNearest_ = nil;
    RendererConfig config_;
    Color currentClear_ = Color::black();
    TargetColorFormat currentColorFormat_ = TargetColorFormat::Swapchain;
    uint64_t nextId_ = 1;

    std::unordered_map<uint64_t, ShaderData> shaders_;
    std::unordered_map<uint64_t, TextureData> textureData_;
    std::unordered_map<uint64_t, RenderTargetData> renderTargets_;
    std::unordered_map<uint64_t, id<MTLBuffer>> vertexBuffers_;
    std::unordered_map<uint64_t, IndexData> indexBufferData_;

    const ShaderData* activeShader_ = nullptr;
    uint64_t activeShaderId_ = 0;
    uint64_t indexBufferId_ = 0;
    std::vector<uint8_t> uniformBlob_;
    std::vector<RenderCommand> pendingCommands_;
    bool initialized_ = false;
};

} // namespace ion

namespace ion {
ion::RenderBackend* createMetalBackend() {
    return new ion::MetalBackend();
}
} // namespace ion
