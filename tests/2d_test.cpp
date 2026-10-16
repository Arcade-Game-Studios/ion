#include <ion/math/Vector2.hpp>
#include <ion/render/Camera2D.hpp>
#include <ion/render/ParticleSystem.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/SpriteAnimation.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/SpriteRegion.hpp>
#include <ion/render/TextureAtlas.hpp>
#include <ion/render/Mesh.hpp>
#include <ion/render/Model.hpp>
#include <ion/render/Text.hpp>
#include <ion/render/Tilemap.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
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

    // Zoom 2 magnifies: 1 world unit spans 2 screen pixels (matching what
    // viewProjection() renders).
    ion::Vector2 step = camera.worldToScreen(ion::Vector2(102.0f, -50.0f));
    CHECK_NEAR(step.x - 400.0f, 4.0f, 1e-4f);

    // +y world is up on screen.
    ion::Vector2 up = camera.worldToScreen(ion::Vector2(100.0f, -48.0f));
    CHECK_NEAR(up.y, 296.0f, 1e-4f);

    // Rotation keeps the round trip intact.
    camera.setRotation(0.7f);
    ion::Vector2 rw = camera.screenToWorld(camera.worldToScreen(p).x,
                                           camera.worldToScreen(p).y);
    CHECK_NEAR(rw.x, p.x, 1e-3f);
    CHECK_NEAR(rw.y, p.y, 1e-3f);

    // worldToScreen agrees with the matrix the renderer actually uses,
    // including zoom and rotation about the camera position.
    {
        ion::Vector3 ndc = camera.viewProjection().transformPoint(ion::Vector3(p.x, p.y, 0.0f));
        ion::Vector2 projected((ndc.x * 0.5f + 0.5f) * 800.0f, (0.5f - ndc.y * 0.5f) * 600.0f);
        ion::Vector2 screenP = camera.worldToScreen(p);
        CHECK_NEAR(projected.x, screenP.x, 1e-2f);
        CHECK_NEAR(projected.y, screenP.y, 1e-2f);
    }

    // Visible bounds: at zoom 2 an 800x600 viewport shows 400x300 units.
    camera.setRotation(0.0f);
    ion::Rect bounds = camera.visibleBounds();
    CHECK_NEAR(bounds.width, 400.0f, 1e-3f);
    CHECK_NEAR(bounds.height, 300.0f, 1e-3f);
    CHECK_NEAR(bounds.center().x, 100.0f, 1e-3f);
    camera.setRotation(1.5707963f);
    CHECK_NEAR(camera.visibleBounds().width, 300.0f, 1e-2f);
    camera.setRotation(0.0f);

    // clampTo keeps the view inside the world, follow converges.
    camera.setPosition(ion::Vector2(0.0f, 0.0f));
    camera.clampTo(ion::Rect(0.0f, 0.0f, 1000.0f, 1000.0f));
    CHECK_NEAR(camera.position().x, 200.0f, 1e-3f);
    CHECK_NEAR(camera.position().y, 150.0f, 1e-3f);
    camera.clampTo(ion::Rect(0.0f, 0.0f, 100.0f, 1000.0f)); // narrower than view
    CHECK_NEAR(camera.position().x, 50.0f, 1e-3f);
    for (int i = 0; i < 200; i++) {
        camera.follow(ion::Vector2(500.0f, 500.0f), 10.0f, 1.0f / 60.0f);
    }
    CHECK_NEAR(camera.position().x, 500.0f, 1e-2f);
    camera.setRotation(0.7f);

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
    CHECK(batch.initialize(&renderer, nullptr, 16));
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

static ion::Texture fakeTexture(uint32_t w, uint32_t h) {
    ion::Texture t;
    t.id = 42;
    t.desc.width = w;
    t.desc.height = h;
    return t;
}

static void testTextureAtlasBoundsAndSequence() {
    ion::TextureAtlas atlas;
    atlas.setTexture(fakeTexture(64, 32));
    CHECK(atlas.addRegion("ok", 32, 0, 32, 32));
    CHECK(!atlas.addRegion("too_wide", 40, 0, 32, 32));
    CHECK(!atlas.addRegion("too_tall", 0, 16, 8, 32));
    CHECK(!atlas.addRegion("overflow", 0xFFFFFFF0u, 0, 32, 1));
    CHECK(atlas.contains("ok") && !atlas.contains("too_wide"));
    CHECK(atlas.addGrid("walk", 4, 2));
    std::vector<ion::SpriteRegion> frames = atlas.sequence("walk", 2, 4);
    CHECK(frames.size() == 4);
    CHECK(frames.size() == 4 && frames[0].x == 32 && frames[2].y == 16);
    CHECK(atlas.sequence("walk", 6, 10).size() == 2); // stops at the end
    CHECK(atlas.sequence("nope", 0, 3).empty());
}

static void testAnimationModes() {
    std::vector<ion::SpriteRegion> frames(4);
    for (int i = 0; i < 4; i++) {
        frames[i].x = (uint32_t)i;
    }
    ion::SpriteAnimation anim;
    anim.setFrames(frames, 0.1f);
    std::vector<int> seen;
    anim.setFrameCallback([&](int f) { seen.push_back(f); });
    anim.play(ion::AnimationMode::PingPong);
    std::vector<int> order;
    for (int i = 0; i < 8; i++) {
        order.push_back(anim.frameIndex());
        anim.update(0.1001f);
    }
    // 0 1 2 3 2 1 0 1 ...
    CHECK((order == std::vector<int>{0, 1, 2, 3, 2, 1, 0, 1}));
    CHECK(seen.size() == 8);
    CHECK(anim.isPlaying());

    anim.play(ion::AnimationMode::Once);
    anim.setSpeed(2.0f);
    anim.update(0.1f); // 0.2s of animation
    CHECK(anim.frameIndex() == 2);
    anim.update(1.0f);
    CHECK(anim.isFinished());
    CHECK(anim.frameIndex() == 3);
    CHECK(anim.progress() == 1.0f);
    anim.setSpeed(-5.0f);
    CHECK(anim.speed() == 0.0f);

    anim.play(ion::AnimationMode::Loop);
    anim.setFrameIndex(10);
    CHECK(anim.frameIndex() == 3);
    anim.setFrameIndex(1);
    CHECK(anim.frameIndex() == 1);
    CHECK(!anim.isFinished());
}

static void testTilemapCollision() {
    ion::Tilemap map;
    ion::TilemapDesc desc;
    desc.width = 10;
    desc.height = 10;
    desc.tileSize = 16;
    desc.tileset = fakeTexture(64, 64);
    desc.tilesAcross = 4;
    CHECK(map.create(desc));
    map.fillRect(0, 0, 10, 1, 1); // floor (row 0)
    map.fillRect(5, 1, 1, 3, 2);  // wall at column 5, rows 1..3
    std::vector<int32_t> all = map.tiles();
    CHECK(all.size() == 100);
    CHECK(!map.setTiles(std::vector<int32_t>(3, 0)));

    int32_t tx = -1, ty = -1;
    CHECK(map.worldToTile(ion::Vector2(33.0f, 17.0f), tx, ty) && tx == 2 && ty == 1);
    CHECK(!map.worldToTile(ion::Vector2(-1.0f, 5.0f), tx, ty));
    CHECK(map.tileToWorld(3, 2) == ion::Vector2(48.0f, 32.0f));
    CHECK(map.tileBounds(1, 1) == ion::Rect(16, 16, 16, 16));
    CHECK(map.worldBounds() == ion::Rect(0, 0, 160, 160));
    CHECK(map.isSolidAt(ion::Vector2(5.0f, 5.0f)));
    CHECK(!map.isSolidAt(ion::Vector2(5.0f, 20.0f)));
    CHECK(!map.isSolidTile(-1, 0));

    // Falling box lands exactly on the floor.
    ion::Rect box(20.0f, 30.0f, 10.0f, 10.0f);
    ion::TileCollision hit = map.moveAndSlide(box, ion::Vector2(0.0f, -20.0f));
    CHECK(hit.hitY && hit.onGround && !hit.hitX);
    CHECK(box.y == 16.0f);
    CHECK(hit.moved.y == -14.0f);
    // Resting on the floor does not count as overlapping it.
    CHECK(!map.overlapsSolid(box));

    // Walking right stops flush against the wall at x = 80.
    hit = map.moveAndSlide(box, ion::Vector2(60.0f, 0.0f));
    CHECK(hit.hitX);
    CHECK(box.right() == 80.0f);
    // Free movement is unaffected.
    hit = map.moveAndSlide(box, ion::Vector2(-5.0f, 3.0f));
    CHECK(!hit.hitX && !hit.hitY);
    CHECK(box.x == 65.0f);

    // Tile 2 made passable: the wall no longer blocks.
    map.setSolid(2, false);
    hit = map.moveAndSlide(box, ion::Vector2(10.0f, 0.0f));
    CHECK(!hit.hitX);
    CHECK(!map.isSolidTile(5, 2));
    map.setSolid(2, true);
    CHECK(map.isSolidTile(5, 2));
}

static void testParticleExtras() {
    ion::ParticleEmitterConfig config;
    config.capacity = 4096;
    config.lifetime = 0.1f;
    config.lifetimeSpread = 0.5f; // could go negative without the floor
    config.spawnRadius = 10.0f;
    ion::ParticleEmitter emitter;
    emitter.setConfig(config);
    emitter.setSeed(7);
    emitter.burstAt(ion::Vector2(100.0f, 100.0f), 4096);
    CHECK(emitter.aliveCount() == 4096);
    CHECK(emitter.position() == ion::Vector2());
    CHECK(!emitter.isFinished());
    emitter.update(1.0f);
    CHECK(emitter.aliveCount() == 0);
    CHECK(emitter.isFinished());

    // Same seed, same simulation.
    ion::ParticleEmitter a, b;
    config.capacity = 64;
    config.lifetimeSpread = 0.0f;
    config.lifetime = 5.0f;
    config.rate = 30.0f;
    a.setConfig(config);
    b.setConfig(config);
    a.setSeed(123);
    b.setSeed(123);
    a.start();
    b.start();
    a.prewarm(1.0f);
    b.prewarm(1.0f);
    CHECK(a.aliveCount() == b.aliveCount());
    CHECK(a.aliveCount() >= 28 && a.aliveCount() <= 31);
}

static void testSpriteBatchExtras() {
    ion::SpriteBatch uninitialized;
    ion::Camera2D camera;
    uninitialized.begin(camera); // must not crash
    uninitialized.end();

    ion::Renderer renderer;
    ion::RendererConfig config;
    config.backend = ion::RendererBackend::Null;
    CHECK(renderer.initialize(nullptr, config));
    ion::SpriteBatch batch;
    CHECK(batch.initialize(&renderer, nullptr, 64));
    renderer.beginFrame();
    batch.begin(camera);
    ion::SpriteRegion region = ion::SpriteRegion::full(fakeTexture(8, 8));
    batch.drawSprite(region, ion::Vector2(), ion::Vector2(8, 8), 0.0f, ion::Vector2(),
                     ion::Color::white(), ion::SpriteFlip::Horizontal | ion::SpriteFlip::Vertical);
    batch.drawTriangle(ion::Vector2(0, 0), ion::Vector2(1, 0), ion::Vector2(0, 1),
                       ion::Color::red());
    ion::Vector2 hexagon[6];
    for (int i = 0; i < 6; i++) {
        hexagon[i] = ion::Vector2::fromAngle((float)i * 1.0471976f, 10.0f);
    }
    batch.drawPolygon(hexagon, 6, ion::Color::green());
    batch.drawPolygon(hexagon, 2, ion::Color::green()); // ignored
    batch.drawPolyline(hexagon, 6, 1.0f, ion::Color::blue(), true);
    batch.drawRect(ion::Rect(0, 0, 4, 4), ion::Color::white());
    batch.end();
    CHECK(batch.drawCallCount() == 2); // sprite texture, then the white texture
    CHECK(renderer.stats().triangles == 2 + 2 + 6 + 6 * 2 + 2);
    renderer.endFrame();

    // Moving keeps the batch usable.
    ion::SpriteBatch moved(std::move(batch));
    CHECK(moved.isInitialized());
    CHECK(!batch.isInitialized());
    renderer.beginFrame();
    moved.begin(camera);
    moved.drawRect(ion::Vector2(), ion::Vector2(1, 1), ion::Color::white());
    moved.end();
    CHECK(moved.drawCallCount() == 1);
    renderer.endFrame();
    moved.shutdown();
    renderer.shutdown();
}

static void testFontLayout() {
    ion::Renderer renderer;
    ion::RendererConfig config;
    config.backend = ion::RendererBackend::Null;
    CHECK(renderer.initialize(nullptr, config));
    ion::Font font;
    CHECK(font.initialize(&renderer));
    // Bitmap font at 7px: 6px advance per glyph, 8px lines.
    CHECK(font.measure("abc", 7.0f).x == 18.0f);
    CHECK(font.measure("\t", 7.0f).x == 24.0f);
    CHECK(font.measure("a\nbb", 7.0f) == ion::Vector2(12.0f, 16.0f));
    CHECK(font.lineHeight(14.0f) == 16.0f);

    std::string wrapped = font.wrap("the quick brown fox", 60.0f, 7.0f); // 10 glyphs/line
    CHECK(wrapped == "the quick\nbrown fox");
    CHECK(font.wrap("supercalifragilistic word", 30.0f, 7.0f) ==
          "supercalifragilistic\nword");
    CHECK(font.wrap("keep\n\nbreaks", 100.0f, 7.0f) == "keep\n\nbreaks");

    ion::Font moved(std::move(font));
    CHECK(moved.isInitialized());
    CHECK(!font.isInitialized());
    CHECK(moved.measure("ab", 7.0f).x == 12.0f);

    ion::SpriteBatch batch;
    CHECK(batch.initialize(&renderer, nullptr, 64));
    ion::Camera2D camera;
    renderer.beginFrame();
    batch.begin(camera);
    moved.drawAligned(batch, "ab\ncd", ion::Vector2(50, 50), 7.0f, ion::TextAlign::Center);
    batch.end();
    CHECK(renderer.stats().triangles == 8);
    renderer.endFrame();
    batch.shutdown();
    moved.shutdown();
    renderer.shutdown();
}

static void testMeshes() {
    ion::Renderer renderer;
    ion::RendererConfig config;
    config.backend = ion::RendererBackend::Null;
    CHECK(renderer.initialize(nullptr, config));

    ion::Mesh cube = ion::createCubeMesh(renderer, 2.0f);
    CHECK(cube.vertexCount == 24 && cube.indexCount == 36);
    CHECK(cube.bounds.min == ion::Vector3(-1, -1, -1));
    CHECK(cube.bounds.max == ion::Vector3(1, 1, 1));
    CHECK(cube.indexBuffer.is16Bit);

    // Cube faces wind counter-clockwise when viewed from outside.
    std::vector<ion::Vertex> v;
    std::vector<uint32_t> idx;
    ion::buildCube(1.0f, v, idx);
    bool outward = true;
    for (size_t i = 0; i < idx.size(); i += 3) {
        ion::Vector3 n = ion::Vector3::cross(v[idx[i + 1]].position - v[idx[i]].position,
                                             v[idx[i + 2]].position - v[idx[i]].position);
        outward = outward && ion::Vector3::dot(n, v[idx[i]].normal) > 0.0f;
    }
    CHECK(outward);

    ion::buildSphere(1.0f, 12, 8, v, idx);
    bool unit = true;
    for (const ion::Vertex& vertex : v) {
        unit = unit && std::fabs(vertex.position.length() - 1.0f) < 1e-4f;
    }
    CHECK(unit);
    std::vector<ion::Vertex> recomputed = v;
    ion::computeNormals(recomputed, idx);
    float worstDot = 1.0f;
    for (size_t i = 0; i < v.size(); i++) {
        if (std::fabs(v[i].position.y) < 0.99f) { // poles are degenerate
            worstDot = std::min(worstDot, ion::Vector3::dot(recomputed[i].normal, v[i].normal));
        }
    }
    CHECK(worstDot > 0.95f);

    // More than 65536 vertices switches to 32-bit indices instead of
    // dropping them.
    std::vector<ion::Vertex> big(70000);
    std::vector<uint32_t> bigIdx = {0, 1, 69999};
    ion::Mesh large = ion::createMesh(renderer, big.data(), (uint32_t)big.size(),
                                      bigIdx.data(), 3);
    CHECK(large.indexBuffer.isValid());
    CHECK(!large.indexBuffer.is16Bit);
    CHECK(large.indexCount == 3);

    ion::Model model;
    ion::ModelPart part;
    part.mesh = cube;
    part.transform = ion::Matrix4::translation(ion::Vector3(10, 0, 0));
    part.material.texture = fakeTexture(1, 1);
    model.parts.push_back(part);
    part.mesh = ion::createPlaneMesh(renderer, 4.0f, 4.0f); // shares the texture
    part.transform = ion::Matrix4::identity();
    model.parts.push_back(part);
    ion::AABB bounds = model.bounds();
    CHECK(bounds.min.x == -2.0f && bounds.max.x == 11.0f);
    CHECK(model.triangleCount() == 12 + 2);
    model.destroy(renderer);
    CHECK(model.parts.empty());

    ion::destroyMesh(renderer, large);
    CHECK(!large.isValid());

    // Moved-from renderers ignore calls instead of crashing.
    ion::Renderer other(std::move(renderer));
    renderer.setUniform("x", 1.0f);
    renderer.draw(3);
    other.beginFrame();
    other.draw(0); // empty draws are not recorded
    other.drawIndexed(0);
    CHECK(other.recordedCommandCount() == 0);
    other.endFrame();
    other.shutdown();
}

int main() {
    testCamera2D();
    testSpriteRegion();
    testTextureAtlas();
    testSpriteAnimation();
    testTilemap();
    testParticleEmitter();
    testSpriteBatchNull();
    testTextureAtlasBoundsAndSequence();
    testAnimationModes();
    testTilemapCollision();
    testParticleExtras();
    testSpriteBatchExtras();
    testFontLayout();
    testMeshes();

    if (failures == 0) {
        std::printf("2d_test: all tests passed\n");
        return 0;
    }
    std::printf("2d_test: %d failure(s)\n", failures);
    return 1;
}
