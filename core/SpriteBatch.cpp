#include <ion/render/SpriteBatch.hpp>

#include <ion/render/Vertex.hpp>

#include <cmath>
#include <cstring>
#include <vector>

namespace ion {

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

struct VSOut {
    float4 position [[position]];
    float4 color;
    float2 uv;
};

fragment float4 fragmentShader(VSOut in [[stage_in]],
                               texture2d<float> tex [[texture(0)]],
                               sampler samp [[sampler(0)]]) {
    return in.color * tex.sample(samp, in.uv);
}
)";

const char* kGLSLVertexShader = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;

uniform mat4 uMVP;

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

uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    fragColor = vColor * texture(uTexture, vUV);
}
)";

} // namespace

SpriteBatch::~SpriteBatch() {
    shutdown();
}

SpriteBatch::SpriteBatch(SpriteBatch&& other) noexcept
    : renderer_(other.renderer_), shader_(other.shader_),
      whiteTexture_(other.whiteTexture_), whiteRegion_(other.whiteRegion_),
      camera_(other.camera_), activeTexture_(other.activeTexture_),
      vertices_(std::move(other.vertices_)), indices_(std::move(other.indices_)),
      thisFrameBuffers_(std::move(other.thisFrameBuffers_)),
      prevFrameBuffers_(std::move(other.prevFrameBuffers_)),
      vertexCapacity_(other.vertexCapacity_), indexCapacity_(other.indexCapacity_),
      drawCalls_(other.drawCalls_), maxQuads_(other.maxQuads_),
      begun_(other.begun_), initialized_(other.initialized_) {
    other.renderer_ = nullptr;
    other.initialized_ = false;
    other.begun_ = false;
}

SpriteBatch& SpriteBatch::operator=(SpriteBatch&& other) noexcept {
    if (this != &other) {
        shutdown();
        renderer_ = other.renderer_;
        shader_ = other.shader_;
        whiteTexture_ = other.whiteTexture_;
        whiteRegion_ = other.whiteRegion_;
        camera_ = other.camera_;
        activeTexture_ = other.activeTexture_;
        vertices_ = std::move(other.vertices_);
        indices_ = std::move(other.indices_);
        thisFrameBuffers_ = std::move(other.thisFrameBuffers_);
        prevFrameBuffers_ = std::move(other.prevFrameBuffers_);
        vertexCapacity_ = other.vertexCapacity_;
        indexCapacity_ = other.indexCapacity_;
        drawCalls_ = other.drawCalls_;
        maxQuads_ = other.maxQuads_;
        begun_ = other.begun_;
        initialized_ = other.initialized_;
        other.renderer_ = nullptr;
        other.initialized_ = false;
        other.begun_ = false;
    }
    return *this;
}

bool SpriteBatch::initialize(Renderer* renderer, uint32_t maxQuads) {
    shutdown();
    if (!renderer || maxQuads == 0) {
        return false;
    }
    renderer_ = renderer;
    maxQuads_ = maxQuads;

    bool isMetal = renderer->gpuInfo().backend == RendererBackend::Metal;
    ShaderSource source;
    source.vertex = isMetal ? kMetalVertexShader : kGLSLVertexShader;
    source.fragment = isMetal ? kMetalFragmentShader : kGLSLFragmentShader;
    shader_ = renderer_->createShader(source);
    if (!shader_.isValid()) {
        shutdown();
        return false;
    }

    const uint32_t whitePixel = 0xFFFFFFFFu;
    TextureDesc whiteDesc;
    whiteDesc.width = 1;
    whiteDesc.height = 1;
    whiteDesc.filterLinear = false;
    whiteTexture_ = renderer_->createTexture(whiteDesc, &whitePixel);
    if (!whiteTexture_.isValid()) {
        shutdown();
        return false;
    }
    whiteRegion_ = SpriteRegion::full(whiteTexture_);

    vertexCapacity_ = (size_t)maxQuads_ * 4;
    indexCapacity_ = (size_t)maxQuads_ * 6;

    vertices_.reserve(vertexCapacity_);
    indices_.reserve(indexCapacity_);
    initialized_ = true;
    return true;
}

void SpriteBatch::shutdown() {
    if (renderer_) {
        if (shader_.isValid()) {
            renderer_->destroyShader(shader_);
        }
        if (whiteTexture_.isValid()) {
            renderer_->destroyTexture(whiteTexture_);
        }
        for (auto& pair : thisFrameBuffers_) {
            if (pair.first.isValid()) {
                renderer_->destroyVertexBuffer(pair.first);
            }
            if (pair.second.isValid()) {
                renderer_->destroyIndexBuffer(pair.second);
            }
        }
        for (auto& pair : prevFrameBuffers_) {
            if (pair.first.isValid()) {
                renderer_->destroyVertexBuffer(pair.first);
            }
            if (pair.second.isValid()) {
                renderer_->destroyIndexBuffer(pair.second);
            }
        }
    }
    thisFrameBuffers_.clear();
    prevFrameBuffers_.clear();
    renderer_ = nullptr;
    shader_ = Shader();
    whiteTexture_ = Texture();
    whiteRegion_ = SpriteRegion();
    vertices_.clear();
    indices_.clear();
    vertexCapacity_ = 0;
    indexCapacity_ = 0;
    begun_ = false;
    initialized_ = false;
}

bool SpriteBatch::isInitialized() const {
    return initialized_;
}

void SpriteBatch::begin(const Camera2D& camera) {
    for (auto& pair : prevFrameBuffers_) {
        if (pair.first.isValid()) {
            renderer_->destroyVertexBuffer(pair.first);
        }
        if (pair.second.isValid()) {
            renderer_->destroyIndexBuffer(pair.second);
        }
    }
    prevFrameBuffers_.clear();
    prevFrameBuffers_ = std::move(thisFrameBuffers_);
    thisFrameBuffers_.clear();

    camera_ = camera;
    activeTexture_ = 0;
    drawCalls_ = 0;
    begun_ = true;
    renderer_->useShader(shader_);
    renderer_->setUniform("uMVP", camera_.viewProjection());
}

void SpriteBatch::end() {
    if (!begun_) {
        return;
    }
    flush_();
    begun_ = false;
}

void SpriteBatch::drawSprite(const SpriteRegion& region, const Vector2& position,
                             const Vector2& size, float rotation,
                             const Vector2& origin, const Color& color) {
    if (!initialized_ || !begun_ || !region.isValid()) {
        return;
    }
    if (size.x == 0.0f || size.y == 0.0f) {
        return;
    }

    Vector2 local[4] = {
        {-origin.x, -origin.y},
        {size.x - origin.x, -origin.y},
        {size.x - origin.x, size.y - origin.y},
        {-origin.x, size.y - origin.y},
    };

    Vector2 world[4];
    if (rotation != 0.0f) {
        float c = std::cos(rotation);
        float s = std::sin(rotation);
        for (int i = 0; i < 4; i++) {
            world[i] = Vector2(local[i].x * c - local[i].y * s + position.x,
                               local[i].x * s + local[i].y * c + position.y);
        }
    } else {
        for (int i = 0; i < 4; i++) {
            world[i] = Vector2(local[i].x + position.x,
                               local[i].y + position.y);
        }
    }

    Vector2 uv[4] = {{region.u0, region.v1},
                     {region.u1, region.v1},
                     {region.u1, region.v0},
                     {region.u0, region.v0}};
    pushQuad_(region, world[0], world[1], world[2], world[3], uv[0], uv[1],
              uv[2], uv[3], color);
}

void SpriteBatch::drawSprite(const SpriteRegion& region, const Vector2& position,
                             float scale, float rotation, const Color& color) {
    if (!region.isValid()) {
        return;
    }
    Vector2 size((float)region.width * scale, (float)region.height * scale);
    drawSprite(region, position, size, rotation,
               Vector2(size.x * 0.5f, size.y * 0.5f), color);
}

void SpriteBatch::drawRect(const Vector2& position, const Vector2& size,
                           const Color& color) {
    drawSprite(whiteRegion_, position, size, 0.0f, {0.0f, 0.0f}, color);
}

void SpriteBatch::drawRectOutline(const Vector2& position, const Vector2& size,
                                  float thickness, const Color& color) {
    if (thickness <= 0.0f) {
        return;
    }
    drawRect(position, Vector2(size.x, thickness), color);
    drawRect(Vector2(position.x, position.y + size.y - thickness),
             Vector2(size.x, thickness), color);
    drawRect(position, Vector2(thickness, size.y), color);
    drawRect(Vector2(position.x + size.x - thickness, position.y),
             Vector2(thickness, size.y), color);
}

void SpriteBatch::drawLine(const Vector2& from, const Vector2& to,
                           float thickness, const Color& color) {
    if (!initialized_ || !begun_ || thickness <= 0.0f) {
        return;
    }
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-6f) {
        return;
    }
    float nx = dx / length;
    float ny = dy / length;
    float half = thickness * 0.5f;
    float px = -ny * half;
    float py = nx * half;

    Vector2 p0(from.x - px, from.y - py);
    Vector2 p1(to.x - px, to.y - py);
    Vector2 p2(to.x + px, to.y + py);
    Vector2 p3(from.x + px, from.y + py);
    pushQuad_(whiteRegion_, p0, p1, p2, p3, {0.0f, 0.0f}, {1.0f, 0.0f},
              {1.0f, 1.0f}, {0.0f, 1.0f}, color);
}

void SpriteBatch::drawCircle(const Vector2& center, float radius,
                             const Color& color, uint32_t segments) {
    if (!initialized_ || !begun_ || radius <= 0.0f) {
        return;
    }
    segments = segments < 3 ? 3 : segments;
    std::vector<Vector2> ring(segments);
    for (uint32_t i = 0; i < segments; i++) {
        float a = 2.0f * 3.14159265f * (float)i / (float)segments;
        ring[i] = Vector2(center.x + std::cos(a) * radius,
                          center.y + std::sin(a) * radius);
    }
    pushTriangleFan_(whiteRegion_, center, ring, color);
}

void SpriteBatch::drawCircleOutline(const Vector2& center, float radius,
                                    float thickness, const Color& color,
                                    uint32_t segments) {
    if (segments < 3) {
        segments = 3;
    }
    float prevX = center.x + radius;
    float prevY = center.y;
    for (uint32_t i = 1; i <= segments; i++) {
        float a = 2.0f * 3.14159265f * (float)i / (float)segments;
        float x = center.x + std::cos(a) * radius;
        float y = center.y + std::sin(a) * radius;
        drawLine(Vector2(prevX, prevY), Vector2(x, y), thickness, color);
        prevX = x;
        prevY = y;
    }
}

size_t SpriteBatch::drawCallCount() const {
    return drawCalls_;
}

size_t SpriteBatch::quadCount() const {
    return indices_.size() / 6;
}

void SpriteBatch::flush_() {
    if (!initialized_ || !begun_) {
        return;
    }
    if (vertices_.empty()) {
        indices_.clear();
        return;
    }
    VertexBuffer vb = renderer_->createVertexBuffer(
        (uint32_t)(vertices_.size() * sizeof(Vertex)), vertices_.data());
    IndexBuffer ib =
        renderer_->createIndexBuffer((uint32_t)indices_.size(), false,
                                     indices_.data());
    fprintf(stderr, "[DBG] flush_ verts=%zu idx=%zu vb=%llu ib=%llu\n",
            vertices_.size(), indices_.size(), (unsigned long long)vb.id,
            (unsigned long long)ib.id);
    thisFrameBuffers_.emplace_back(vb, ib);
    renderer_->setVertexBuffer(vb);
    renderer_->setIndexBuffer(ib);
    renderer_->drawIndexed((uint32_t)indices_.size(), 0);
    drawCalls_++;
    vertices_.clear();
    indices_.clear();
}

void SpriteBatch::pushQuad_(const SpriteRegion& region, const Vector2& p0,
                            const Vector2& p1, const Vector2& p2,
                            const Vector2& p3, const Vector2& uv0,
                            const Vector2& uv1, const Vector2& uv2,
                            const Vector2& uv3, const Color& color) {
    if (vertices_.size() + 4 > vertexCapacity_ ||
        indices_.size() + 6 > indexCapacity_) {
        flush_();
    }
    if (activeTexture_ != region.texture.id) {
        flush_();
        activeTexture_ = region.texture.id;
        renderer_->setTexture(0, region.texture);
    }

    uint32_t base = (uint32_t)vertices_.size();
    const Vector2 positions[4] = {p0, p1, p2, p3};
    const Vector2 uvs[4] = {uv0, uv1, uv2, uv3};
    for (int i = 0; i < 4; i++) {
        Vertex vertex;
        vertex.position = Vector3(positions[i].x, positions[i].y, 0.0f);
        vertex.color = Vector4(color.r, color.g, color.b, color.a);
        vertex.uv = uvs[i];
        vertices_.push_back(vertex);
    }
    uint32_t idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
    indices_.insert(indices_.end(), idx, idx + 6);
}

void SpriteBatch::pushTriangleFan_(const SpriteRegion& region,
                                   const Vector2& center,
                                   const std::vector<Vector2>& ring,
                                   const Color& color) {
    if (vertices_.size() + ring.size() + 1 > vertexCapacity_ ||
        indices_.size() + ring.size() * 3 > indexCapacity_) {
        flush_();
    }
    if (activeTexture_ != region.texture.id) {
        flush_();
        activeTexture_ = region.texture.id;
        renderer_->setTexture(0, region.texture);
    }

    uint32_t base = (uint32_t)vertices_.size();
    Vertex centerVertex;
    centerVertex.position = Vector3(center.x, center.y, 0.0f);
    centerVertex.color = Vector4(color.r, color.g, color.b, color.a);
    centerVertex.uv = {0.5f, 0.5f};
    vertices_.push_back(centerVertex);

    uint32_t n = (uint32_t)ring.size();
    for (uint32_t i = 0; i < n; i++) {
        Vertex vertex;
        vertex.position = Vector3(ring[i].x, ring[i].y, 0.0f);
        vertex.color = Vector4(color.r, color.g, color.b, color.a);
        vertex.uv = {0.5f, 0.5f};
        vertices_.push_back(vertex);
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx[3] = {base, base + 1 + i,
                           base + 1 + (i + 1) % n};
        indices_.insert(indices_.end(), idx, idx + 3);
    }
}

} // namespace ion
