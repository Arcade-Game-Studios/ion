#pragma once

#include <ion/math/Vector2.hpp>
#include <ion/render/Camera2D.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/SpriteRegion.hpp>
#include <ion/render/Vertex.hpp>

#include <cstdint>
#include <vector>

namespace ion {

//
// SpriteBatch renders 2D quads efficiently. Draw calls are accumulated into a
// single dynamic vertex/index buffer and flushed to the GPU in batches,
// grouped by texture.
//
// Usage, inside a renderer frame:
//   renderer.beginFrame();
//   renderer.clear(...);
//   batch.begin(camera);
//   batch.drawSprite(...);
//   ...
//   batch.end();        // flushes any remaining quads
//   renderer.endFrame();
//
// All drawn geometry uses the batch's own shader (position/color/uv with an
// orthographic uMVP and a single texture sampled at slot 0). Untextured
// primitives (rects, lines, circles) use the batch's built-in 1x1 white
// texture and are tinted by the vertex color.
//
class SpriteBatch {
public:
    SpriteBatch() = default;
    ~SpriteBatch();
    SpriteBatch(SpriteBatch&& other) noexcept;
    SpriteBatch& operator=(SpriteBatch&& other) noexcept;
    SpriteBatch(const SpriteBatch&) = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    bool initialize(Renderer* renderer, uint32_t maxQuads = 8192);
    void shutdown();
    bool isInitialized() const;

    // Begins a batched draw pass for the given camera. Must be called after
    // renderer.beginFrame() and before renderer.endFrame().
    void begin(const Camera2D& camera);
    // Flushes any remaining quads.
    void end();

    // Draws a region scaled to size at position. origin is the point within
    // the sprite placed at position (defaults to the center). rotation is in
    // radians around origin.
    void drawSprite(const SpriteRegion& region, const Vector2& position,
                    const Vector2& size, float rotation = 0.0f,
                    const Vector2& origin = {},
                    const Color& color = Color::white());

    // Draws a region at its native pixel size scaled by scale.
    void drawSprite(const SpriteRegion& region, const Vector2& position,
                    float scale = 1.0f, float rotation = 0.0f,
                    const Color& color = Color::white());

    void drawRect(const Vector2& position, const Vector2& size,
                  const Color& color);
    void drawRectOutline(const Vector2& position, const Vector2& size,
                         float thickness, const Color& color);
    void drawLine(const Vector2& from, const Vector2& to, float thickness,
                  const Color& color);
    void drawCircle(const Vector2& center, float radius, const Color& color,
                    uint32_t segments = 24);
    void drawCircleOutline(const Vector2& center, float radius,
                           float thickness, const Color& color,
                           uint32_t segments = 24);

    // Number of GPU draw calls issued so far in the current frame.
    size_t drawCallCount() const;
    // Number of quads pending in the current batch (not yet flushed).
    size_t quadCount() const;

private:
    void flush_();
    void pushQuad_(const SpriteRegion& region, const Vector2& p0,
                   const Vector2& p1, const Vector2& p2, const Vector2& p3,
                   const Vector2& uv0, const Vector2& uv1, const Vector2& uv2,
                   const Vector2& uv3, const Color& color);
    void pushTriangleFan_(const SpriteRegion& region, const Vector2& center,
                          const std::vector<Vector2>& ring,
                          const Color& color);

    Renderer* renderer_ = nullptr;
    Shader shader_;
    Texture whiteTexture_;
    SpriteRegion whiteRegion_;
    Camera2D camera_;
    uint64_t activeTexture_ = 0;

    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    // Renderer draws are deferred to endFrame(), so buffers must not be
    // rewritten between an update and its draw. Each flush_() therefore
    // uploads its own immutable vertex/index buffer pair and keeps them
    // alive until two frames later (see begin()).
    std::vector<std::pair<VertexBuffer, IndexBuffer>> thisFrameBuffers_;
    std::vector<std::pair<VertexBuffer, IndexBuffer>> prevFrameBuffers_;
    size_t vertexCapacity_ = 0;
    size_t indexCapacity_ = 0;
    size_t drawCalls_ = 0;
    uint32_t maxQuads_ = 8192;
    bool begun_ = false;
    bool initialized_ = false;
};

} // namespace ion
