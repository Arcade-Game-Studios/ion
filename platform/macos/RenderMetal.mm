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

struct ShaderData {
    id<MTLRenderPipelineState> pipeline = nil;
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
        vertexBuffers_.clear();
        indexBufferData_.clear();
        commandQueue_ = nil;
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
            drawable_ = [layer_ nextDrawable];
        }
    }

    void endFrame() override {
        @autoreleasepool {
            if (!drawable_) {
                return;
            }
            for (const RenderCommand& command : pendingCommands_) {
                if (command.type == RenderCommandType::Clear) {
                    currentClear_ = command.clearColor;
                }
            }
            id<MTLCommandBuffer> commandBuffer = [commandQueue_ commandBuffer];
            commandBuffer.label = @"Ion Render Frame";

            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor
                renderPassDescriptor];
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].clearColor =
                MTLClearColorMake(currentClear_.r, currentClear_.g,
                                  currentClear_.b, currentClear_.a);
            if (msaaTexture_) {
                pass.colorAttachments[0].texture = msaaTexture_;
                pass.colorAttachments[0].resolveTexture = drawable_.texture;
                pass.colorAttachments[0].storeAction =
                    MTLStoreActionStoreAndMultisampleResolve;
            } else {
                pass.colorAttachments[0].texture = drawable_.texture;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            }

            id<MTLRenderCommandEncoder> encoder =
                [commandBuffer renderCommandEncoderWithDescriptor:pass];
            for (const RenderCommand& command : pendingCommands_) {
                executeCommand_(encoder, command);
            }
            [encoder endEncoding];
            [commandBuffer presentDrawable:drawable_];
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
            vd.layouts[30].stride = 36;
            vd.layouts[30].stepFunction = MTLVertexStepFunctionPerVertex;

            MTLRenderPipelineDescriptor* pd =
                [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = vertexFn;
            pd.fragmentFunction = fragmentFn;
            pd.vertexDescriptor = vd;
            pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            if (config_.antialiasSamples > 1) {
                pd.sampleCount = config_.antialiasSamples;
            }

            id<MTLRenderPipelineState> pipeline =
                [device_ newRenderPipelineStateWithDescriptor:pd
                                                        error:&error];
            if (!pipeline) {
                ION_LOG_ERROR("Metal: pipeline state creation failed: %s",
                              error.localizedDescription.UTF8String);
                return 0;
            }

            ShaderData data;
            data.pipeline = pipeline;
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
            [encoder setRenderPipelineState:it->second.pipeline];
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
    id<MTLSamplerState> samplerLinear_ = nil;
    id<MTLSamplerState> samplerNearest_ = nil;
    RendererConfig config_;
    Color currentClear_ = Color::black();
    uint64_t nextId_ = 1;

    std::unordered_map<uint64_t, ShaderData> shaders_;
    std::unordered_map<uint64_t, TextureData> textureData_;
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
