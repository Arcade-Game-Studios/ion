#include <ion/math/Vector2.hpp>
#include <ion/render/Camera2D.hpp>
#include <ion/render/ParticleSystem.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/SpriteAnimation.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/SpriteRegion.hpp>
#include <ion/render/TextureAtlas.hpp>
#include <ion/render/Tilemap.hpp>

#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            failures++;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
    do {                                                                             \
        float _a = (a);                                                              \
        float _b = (b);                                                              \
        if (_a < _b - (eps) || _a > _b + (eps)) {                                    \
            std::printf("FAIL %s:%d: %s = %f, expected %f (+/-%f)\n",                \
                        __FILE__, __LINE__, #a, _a, _b, (float)(eps));               \
            failures++;                                                              \
        }                                                                            \
    } while (0)

static void testCamera2D() {
    ion::Camera2D camera;
    camera.setViewport(800, 600);
    camera.setPosition(ion::Vector2(100.0f, -50.0f));
    camera.setZoom(2.0f);

    // Screen center maps to the camera position.
    ion::Vector2 world = camera.screenToWorld(400, 300);
    CHECK_NEAR(world.x, 100.0f, 1e-4f);
    CHECK_NEAR(world.y, -50.0f, 1e-4f);

    // World center maps to the screen center.
    ion::Vector2 screen = camera.worldToScreen(ion::Vector2(100.0f, -50.0f));
    CHECK_NEAR(screen.x, 400.0f, 1e-4f);
    CHECK_NEAR(screen.y, 300.0f, 1e-4f);

    // Round trips are inverses of each other.
    ion::Vector2 p(240.0f, -320.0f);
    ion::Vector2 back = camera.screenToWorld(camera.worldToScreen(p).x,
                                             camera.worldToScreen(p).y);
    CHECK_NEAR(back.x, p.x, 1e-3f);
    CHECK_NEAR(back.y, p.y, 1e-3f);

    ion::Vector2 s(123.0f, 456.0f);
    ion::Vector2 sb = camera.worldToScreen(camera.screenToWorld(s.x, s.y));
    CHECK_NEAR(sb.x, s.x, 1e-3f);
    CHECK_NEAR(sb.y, s.y, 1e-3f);

    // Zoom 2 means 2 world units span 1 screen pixel.
    ion::Vector2 step = camera.worldToScreen(ion::Vector2(102.0f, -50.0f));
    CHECK_NEAR(step.x - 400.0f, 1.0f, 1e-4f);

    // +y world is up on screen.
    ion::Vector2 up = camera.worldToScreen(ion::Vector2(100.0f, -48.0f));
    CHECK_NEAR(up.y, 299.0f, 1e-4f);

    // Rotation keeps the round trip intact.
    camera.setRotation(0.7f);
    ion::Vector2 rw = camera.screenToWorld(camera.worldToScreen(p).x,
                                           camera.worldToScreen(p).y);
    CHECK_NEAR(rw.x, p.x, 1e-3f);
    CHECK_NEAR(rw.y, p.y, 1e-3f);

    // Zoom and viewport are clamped.
    camera.setZoom(0.0f);
    CHECK(camera.zoom() > 0.0f);
    camera.setViewport(0, 0);
    CHECK(camera.viewportWidth() >= 1u);
    CHECK(camera.viewportHeight() >= 1u);
}

static void testSpriteRegion() {
    ion::Texture texture;
    texture.id = 7;
    texture.desc.width = 100;
    texture.desc.height = 50;

    ion::SpriteRegion region;
    region.texture = texture;
    region.x = 20;
    region.y = 10;
    region.width = 10;
    region.height = 5;
    region.computeUV();
    CHECK_NEAR(region.u0, 0.2f, 1e-6f);
    CHECK_NEAR(region.v0, 0.2f, 1e-6f);
    CHECK_NEAR(region.u1, 0.3f, 1e-6f);
    CHECK_NEAR(region.v1, 0.3f, 1e-6f);
    CHECK(region.isValid());

    ion::SpriteRegion full = ion::SpriteRegion::full(texture);
    CHECK_NEAR(full.u0, 0.0f, 1e-6f);
    CHECK_NEAR(full.v0, 0.0f, 1e-6f);
    CHECK_NEAR(full.u1, 1.0f, 1e-6f);
    CHECK_NEAR(full.v1, 1.0f, 1e-6f);
}

static void testTextureAtlas() {
    ion::Texture texture;
    texture.id = 9;
    texture.desc.width = 128;
    texture.desc.height = 128;

    ion::TextureAtlas atlas;
    atlas.setTexture(texture);
    CHECK(atlas.texture().id == 9);
    CHECK(atlas.regionCount() == 0);

    CHECK(atlas.addRegion("hero_idle", 0, 0, 32, 32));
    CHECK(!atlas.addRegion("hero_idle", 32, 0, 32, 32)); // duplicate
    CHECK(atlas.addRegion("hero_jump", 64, 0, 32, 32));
    CHECK(atlas.regionCount() == 2);
    CHECK(atlas.find("hero_idle") != nullptr);
    CHECK(atlas.find("hero_jump") != nullptr);
    CHECK(atlas.find("missing") == nullptr);
    CHECK(atlas.find("hero_idle")->width == 32);

    // Whole-texture grid.
    CHECK(atlas.addGrid("tile", 4, 4));
    CHECK(atlas.regionCount() == 2 + 16);
    const ion::SpriteRegion* t15 = atlas.find("tile_15");
    CHECK(t15 != nullptr);
    CHECK_NEAR(t15->u0, 96.0f / 128.0f, 1e-6f);
    CHECK_NEAR(t15->v0, 96.0f / 128.0f, 1e-6f);

    // Explicit cell size grid (padding support).
    CHECK(atlas.addGrid("pad", 2, 2, 24, 24));
    CHECK(atlas.regionCount() == 2 + 16 + 4);
    const ion::SpriteRegion* pad1 = atlas.find("pad_1");
    CHECK(pad1 != nullptr);
    CHECK(pad1->width == 24);
    CHECK_NEAR(pad1->u0, 24.0f / 128.0f, 1e-6f);

    CHECK(atlas.names().size() == atlas.regionCount());
}

static void testSpriteAnimation() {
    ion::Texture texture;
    texture.id = 11;
    texture.desc.width = 128;
    texture.desc.height = 32;

    ion::SpriteRegion frame0;
    frame0.texture = texture;
    frame0.width = 32;
    frame0.height = 32;
    ion::SpriteRegion frame1 = frame0;
    ion::SpriteRegion frame2 = frame0;

    ion::SpriteAnimation anim;
    anim.setFrames({frame0, frame1, frame2}, 0.5f);
    CHECK(anim.frameCount() == 3);
    CHECK_NEAR(anim.duration(), 1.5f, 1e-5f);
    CHECK(anim.frameIndex() == 0);
    CHECK(!anim.isPlaying());
    CHECK(!anim.isFinished());

    anim.play();
    CHECK(anim.isPlaying());
    anim.update(0.25f);
    CHECK(anim.frameIndex() == 0);
    anim.update(0.25f); // 0.5s elapsed -> frame 1
    CHECK(anim.frameIndex() == 1);
    anim.update(1.0f); // 1.5s elapsed -> wraps to frame 0 (looping)
    CHECK(anim.frameIndex() == 0);
    CHECK(anim.isPlaying());

    // Non-looping finishes on the last frame.
    anim.setFrames({frame0, frame1, frame2}, 0.5f);
    anim.play(false);
    anim.update(0.6f);
    CHECK(anim.frameIndex() == 1);
    anim.update(1.0f);
    CHECK(anim.frameIndex() == 2);
    CHECK(!anim.isPlaying());
    CHECK(anim.isFinished());

    // Pause/resume.
    anim.setFrames({frame0, frame1, frame2}, 0.5f);
    anim.play();
    anim.update(0.5f);
    anim.pause();
    anim.update(1.0f);
    CHECK(anim.frameIndex() == 1);
    anim.resume();
    anim.update(0.5f);
    CHECK(anim.frameIndex() == 2);

    // Stop resets to the first frame.
    anim.stop();
    CHECK(!anim.isPlaying());
    CHECK(anim.frameIndex() == 0);
}

static void testTilemap() {
    ion::Texture tileset;
    tileset.id = 13;
    tileset.desc.width = 64;
    tileset.desc.height = 32;

    ion::TilemapDesc desc;
    desc.width = 10;
    desc.height = 8;
    desc.tileSize = 16;
    desc.tileset = tileset;
    desc.tilesAcross = 2;

    ion::Tilemap map;
    CHECK(!map.create(ion::TilemapDesc{})); // invalid desc rejected
    CHECK(map.create(desc));
    CHECK(map.width() == 10);
    CHECK(map.height() == 8);
    CHECK(map.tileSize() == 16);
    CHECK(map.tile(3, 2) == -1);
    CHECK(map.empty(3, 2));

    map.setTile(3, 2, 0);
    map.setTile(4, 2, 1);
    CHECK(map.tile(3, 2) == 0);
    CHECK(map.tile(4, 2) == 1);
    CHECK(!map.empty(3, 2));
    CHECK(map.empty(0, 0));

    map.setTile(10, 2, 5); // out of bounds, ignored
    map.setTile(3, 8, 5);
    CHECK(map.tile(10, 2) == -1);
    CHECK(map.tile(3, 8) == -1);

    map.setTile(3, 2, -1); // clearing
    CHECK(map.empty(3, 2));
}

static void testParticleEmitter() {
    ion::ParticleEmitterConfig config;
    config.capacity = 4;
    config.rate = 0.0f; // burst only
    config.lifetime = 1.0f;

    ion::ParticleEmitter emitter;
    emitter.setConfig(config);
    CHECK(emitter.aliveCount() == 0);
    CHECK(!emitter.isActive());

    emitter.burst(4);
    CHECK(emitter.aliveCount() == 4);

    // All particles die after their lifetime.
    emitter.update(1.5f);
    CHECK(emitter.aliveCount() == 0);

    // Capacity is never exceeded.
    emitter.burst(10);
    CHECK(emitter.aliveCount() <= 4);

    // Continuous emission.
    config.capacity = 8;
    config.rate = 10.0f;
    config.lifetime = 5.0f;
    emitter.setConfig(config);
    emitter.start();
    CHECK(emitter.isActive());
    emitter.update(0.3f); // ~3 particles
    CHECK(emitter.aliveCount() > 0);
    emitter.stop();
    CHECK(!emitter.isActive());
    emitter.clear();
    CHECK(emitter.aliveCount() == 0);
}

static void testSpriteBatchNull() {
    ion::Renderer renderer;
    ion::RendererConfig rendererConfig;
    rendererConfig.backend = ion::RendererBackend::Null;
    CHECK(renderer.initialize(nullptr, rendererConfig));

    ion::SpriteBatch batch;
    CHECK(batch.initialize(&renderer, 16));
    CHECK(batch.isInitialized());
    CHECK(batch.quadCount() == 0);
    CHECK(batch.drawCallCount() == 0);

    const uint8_t pixels[16] = {255, 255, 255, 255};
    ion::TextureDesc texDesc;
    texDesc.width = 2;
    texDesc.height = 2;
    ion::Texture texA = renderer.createTexture(texDesc, pixels);
    ion::Texture texB = renderer.createTexture(texDesc, pixels);
    CHECK(texA.isValid());
    CHECK(texB.isValid());

    ion::SpriteRegion regA = ion::SpriteRegion::full(texA);
    ion::SpriteRegion regB = ion::SpriteRegion::full(texB);

    ion::Camera2D camera;
    camera.setViewport(640, 480);
    camera.setZoom(1.0f);

    renderer.beginFrame();
    batch.begin(camera);
    CHECK(batch.quadCount() == 0);

    batch.drawSprite(regA, ion::Vector2(0.0f, 0.0f), 32.0f);
    CHECK(batch.quadCount() == 1);
    batch.drawSprite(regA, ion::Vector2(40.0f, 0.0f), 32.0f);
    CHECK(batch.quadCount() == 2);

    // Note: the Null backend assigns the same id (1) to every texture, so a
    // texture switch cannot be observed via draw calls here. Just make sure
    // switching textures (and drawing with a different region) stays valid.
    batch.drawSprite(regB, ion::Vector2(80.0f, 0.0f), 32.0f);
    CHECK(batch.quadCount() == 3);

    batch.drawRect(ion::Vector2(0.0f, -10.0f), ion::Vector2(50.0f, 20.0f),
                   ion::Color::red());
    batch.drawRectOutline(ion::Vector2(0.0f, -40.0f), ion::Vector2(50.0f, 20.0f),
                          2.0f, ion::Color::green());
    batch.drawLine(ion::Vector2(0.0f, 0.0f), ion::Vector2(100.0f, 100.0f),
                   3.0f, ion::Color::blue());
    batch.drawCircle(ion::Vector2(200.0f, 200.0f), 25.0f, ion::Color::yellow());
    batch.drawCircleOutline(ion::Vector2(300.0f, 200.0f), 25.0f, 2.0f,
                            ion::Color::cyan());

    CHECK(batch.quadCount() > 0);
    batch.end();
    CHECK(batch.quadCount() == 0);
    CHECK(batch.drawCallCount() >= 1);
    renderer.endFrame();

    renderer.destroyTexture(texA);
    renderer.destroyTexture(texB);
    batch.shutdown();
    CHECK(!batch.isInitialized());
    renderer.shutdown();
}

int main() {
    testCamera2D();
    testSpriteRegion();
    testTextureAtlas();
    testSpriteAnimation();
    testTilemap();
    testParticleEmitter();
    testSpriteBatchNull();

    if (failures == 0) {
        std::printf("2d_test: all tests passed\n");
        return 0;
    }
    std::printf("2d_test: %d failure(s)\n", failures);
    return 1;
}
