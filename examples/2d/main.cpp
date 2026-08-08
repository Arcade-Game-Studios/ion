#include <ion/core/Timer.hpp>
#include <ion/core/Version.hpp>
#include <ion/platform/Input.hpp>
#include <ion/platform/Window.hpp>
#include <ion/render/Camera2D.hpp>
#include <ion/render/ParticleSystem.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/SpriteAnimation.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/Text.hpp>
#include <ion/render/TextureAtlas.hpp>
#include <ion/render/Tilemap.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

class TwoDApp {
public:
    int run(const char* backendName) {
        ion::WindowConfig config;
        config.title = "Ion 0.2.0 Two Dimensional";
        config.appName = "Ion2D";
        config.width = 1280;
        config.height = 720;

        window_ = ion::Window(config);
        if (!window_.create()) {
            std::printf("failed to create window\n");
            return 1;
        }

        ion::RendererConfig rendererConfig;
        if (std::strcmp(backendName, "metal") == 0) {
            rendererConfig.backend = ion::RendererBackend::Metal;
        } else if (std::strcmp(backendName, "gl") == 0) {
            rendererConfig.backend = ion::RendererBackend::OpenGL;
        } else if (std::strcmp(backendName, "null") == 0) {
            rendererConfig.backend = ion::RendererBackend::Null;
        }

        if (!renderer_.initialize(&window_, rendererConfig)) {
            std::printf("failed to initialize renderer\n");
            window_.destroy();
            return 1;
        }

        const ion::GPUInfo& gpu = renderer_.gpuInfo();
        std::printf("GPU: %s (%s)\n", gpu.name.c_str(), gpu.vendor.c_str());
        if (gpu.backend == ion::RendererBackend::Null) {
            std::printf("Running with the Null backend: no draw calls executed\n");
        }

        if (!createAssets_()) {
            std::printf("failed to create 2D assets\n");
            renderer_.shutdown();
            window_.destroy();
            return 1;
        }

        if (!font_.initialize(&renderer_)) {
            std::printf("failed to initialize font\n");
            renderer_.shutdown();
            window_.destroy();
            return 1;
        }

        // World camera: 3x zoom, follows the player.
        worldCamera_.setViewport(window_.width(), window_.height());
        worldCamera_.setZoom(3.0f);
        worldCamera_.setPosition(player_);

        // UI camera: 1 world unit == 1 pixel, centered on the window.
        uiCamera_.setViewport(window_.width(), window_.height());
        uiCamera_.setZoom(1.0f);
        uiCamera_.setPosition(ion::Vector2((float)window_.width() * 0.5f,
                                           (float)window_.height() * 0.5f));

        heroAnim_.play(true);

        timer_.reset();
        while (window_.isOpen()) {
            window_.pollEvents();
            float deltaTime = timer_.tick();
            if (ion::input::isKeyPressed(ion::Key::Escape)) {
                break;
            }

            frames_++;
            elapsed_ += deltaTime;
            if (frames_ % 60 == 0) {
                std::printf("avg %.2f ms/frame, window %ux%u\n",
                            elapsed_ / (float)frames_ * 1000.0f,
                            window_.width(), window_.height());
            }

            update_(deltaTime);
            render_();
        }

        font_.shutdown();
        renderer_.destroyTexture(spriteSheet_);
        renderer_.shutdown();
        window_.destroy();
        std::printf("2D example exited.\n");
        return 0;
    }

private:
    bool createAssets_() {
        constexpr int size = 64;
        uint32_t pixels[size * size] = {0};
        auto setPx = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
            if (x < 0 || x >= size || y < 0 || y >= size) {
                return;
            }
            pixels[y * size + x] =
                0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) |
                (uint32_t)r;
        };
        auto fillTile = [&](int col, int row, uint8_t r, uint8_t g, uint8_t b) {
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 16; x++) {
                    setPx(col * 16 + x, row * 16 + y, r, g, b);
                }
            }
        };
        auto drawHero = [&](int col, int variant) {
            const int ox = col * 16;
            const int oy = 48;
            auto px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
                setPx(ox + x, oy + y, r, g, b);
            };
            const uint8_t skinR = 255, skinG = 216, skinB = 178;
            const uint8_t bodyR = 214, bodyG = 64, bodyB = 64;
            const uint8_t legR = 52, legG = 96, legB = 180;
            // Head.
            for (int y = 1; y <= 4; y++) {
                for (int x = 5; x <= 10; x++) {
                    px(x, y, skinR, skinG, skinB);
                }
            }
            px(6, 2, 24, 24, 24);
            px(9, 2, 24, 24, 24);
            // Body.
            for (int y = 5; y <= 9; y++) {
                for (int x = 4; x <= 11; x++) {
                    px(x, y, bodyR, bodyG, bodyB);
                }
            }
            if (variant == 2) { // arms raised
                for (int y = 3; y <= 5; y++) {
                    px(1, y, bodyR, bodyG, bodyB);
                    px(14, y, bodyR, bodyG, bodyB);
                }
                for (int y = 3; y <= 5; y++) {
                    px(2, y, skinR, skinG, skinB);
                    px(13, y, skinR, skinG, skinB);
                }
            } else {
                for (int y = 5; y <= 8; y++) {
                    px(2, y, bodyR, bodyG, bodyB);
                    px(13, y, bodyR, bodyG, bodyB);
                }
                px(2, 9, skinR, skinG, skinB);
                px(13, 9, skinR, skinG, skinB);
            }
            // Legs.
            int legLeft = (variant == 1) ? 3 : 5;
            int legRight = (variant == 1) ? 10 : 9;
            for (int y = 10; y <= 13; y++) {
                for (int x = legLeft; x <= legLeft + 2; x++) {
                    px(x, y, legR, legG, legB);
                }
                for (int x = legRight; x <= legRight + 2; x++) {
                    px(x, y, legR, legG, legB);
                }
            }
        };
        auto drawStar = [&]() {
            const int ox = 48;
            const int oy = 48;
            auto px = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
                setPx(ox + x, oy + y, r, g, b);
            };
            const uint8_t yR = 255, yG = 216, yB = 64;
            px(7, 2, yR, yG, yB);
            px(6, 3, yR, yG, yB);
            px(7, 3, yR, yG, yB);
            px(8, 3, yR, yG, yB);
            for (int y = 4; y <= 10; y++) {
                for (int x = 5; x <= 10; x++) {
                    px(x, y, yR, yG, yB);
                }
            }
            px(4, 5, yR, yG, yB);
            px(4, 6, yR, yG, yB);
            px(11, 5, yR, yG, yB);
            px(11, 6, yR, yG, yB);
            px(3, 7, yR, yG, yB);
            px(12, 7, yR, yG, yB);
            px(4, 8, yR, yG, yB);
            px(11, 8, yR, yG, yB);
            px(5, 11, yR, yG, yB);
            px(6, 11, yR, yG, yB);
            px(9, 11, yR, yG, yB);
            px(10, 11, yR, yG, yB);
            px(7, 12, yR, yG, yB);
            px(8, 12, yR, yG, yB);
            px(7, 13, yR, yG, yB);
            px(8, 13, yR, yG, yB);
        };

        for (int i = 0; i < 4; i++) {
            fillTile(i, 0, 0x4e, 0x9e, 0x3a); // grass
        }
        fillTile(0, 1, 0x9c, 0x6b, 0x3d); // dirt
        fillTile(1, 1, 0x8c, 0x5f, 0x37);
        fillTile(2, 1, 0xa8, 0x76, 0x43);
        fillTile(3, 1, 0x2e, 0x5f, 0xa8); // water
        fillTile(0, 2, 0x8a, 0x8a, 0x8a); // wall
        fillTile(1, 2, 0x7a, 0x7a, 0x7a);
        fillTile(2, 2, 0xd9, 0xc8, 0x93); // sand
        fillTile(3, 2, 0x2e, 0x5f, 0xa8); // water
        drawHero(0, 0);
        drawHero(1, 1);
        drawHero(2, 2);
        drawStar();

        ion::TextureDesc desc;
        desc.width = size;
        desc.height = size;
        desc.filterLinear = false;
        spriteSheet_ = renderer_.createTexture(desc, pixels);
        if (!spriteSheet_.isValid()) {
            return false;
        }

        atlas_.setTexture(spriteSheet_);
        if (!atlas_.addGrid("tile", 4, 4)) {
            return false;
        }
        if (!atlas_.addRegion("hero_0", 0, 48, 16, 16) ||
            !atlas_.addRegion("hero_1", 16, 48, 16, 16) ||
            !atlas_.addRegion("hero_2", 32, 48, 16, 16) ||
            !atlas_.addRegion("star", 48, 48, 16, 16)) {
            return false;
        }
        heroFrames_ = {*atlas_.find("hero_0"), *atlas_.find("hero_1"),
                       *atlas_.find("hero_2")};

        if (!batch_.initialize(&renderer_, 8192)) {
            return false;
        }

        if (!createMap_()) {
            return false;
        }

        heroAnim_.setFrames(heroFrames_, 0.18f);

        ion::ParticleEmitterConfig pconfig;
        pconfig.rate = 40.0f;
        pconfig.lifetime = 0.6f;
        pconfig.lifetimeSpread = 0.2f;
        pconfig.speed = 36.0f;
        pconfig.speedSpread = 14.0f;
        pconfig.angle = -1.5707963f;
        pconfig.angleSpread = 0.9f;
        pconfig.gravity = ion::Vector2(0.0f, -50.0f);
        pconfig.startSize = 7.0f;
        pconfig.endSize = 2.0f;
        pconfig.startColor = ion::Color(1.0f, 0.85f, 0.3f);
        pconfig.endColor = ion::Color::transparent();
        emitter_.setConfig(pconfig);
        emitter_.start();
        return true;
    }

    bool createMap_() {
        ion::TilemapDesc desc;
        desc.width = 48;
        desc.height = 48;
        desc.tileSize = 16;
        desc.tileset = spriteSheet_;
        desc.tilesAcross = 4;
        if (!tilemap_.create(desc)) {
            return false;
        }

        auto tile = [&](uint32_t x, uint32_t y, int32_t id) {
            tilemap_.setTile(x, y, id);
        };
        for (uint32_t y = 0; y < desc.height; y++) {
            for (uint32_t x = 0; x < desc.width; x++) {
                tile(x, y, 0); // grass floor
            }
        }
        for (uint32_t x = 0; x < desc.width; x++) {
            tile(x, 0, 8);
            tile(x, desc.height - 1, 8);
        }
        for (uint32_t y = 0; y < desc.height; y++) {
            tile(0, y, 8);
            tile(desc.width - 1, y, 8);
        }
        // Lake in the north-west corner.
        for (uint32_t y = 4; y < 12; y++) {
            for (uint32_t x = 3; x < 14; x++) {
                tile(x, y, 7);
            }
        }
        // Sand patch in the south-east.
        for (uint32_t y = 34; y < 40; y++) {
            for (uint32_t x = 34; x < 42; x++) {
                tile(x, y, 10);
            }
        }
        return true;
    }

    void update_(float deltaTime) {
        float speed = 140.0f;
        ion::Vector2 move;
        if (ion::input::isKeyDown(ion::Key::W) ||
            ion::input::isKeyDown(ion::Key::ArrowUp)) {
            move.y += 1.0f;
        }
        if (ion::input::isKeyDown(ion::Key::S) ||
            ion::input::isKeyDown(ion::Key::ArrowDown)) {
            move.y -= 1.0f;
        }
        if (ion::input::isKeyDown(ion::Key::A) ||
            ion::input::isKeyDown(ion::Key::ArrowLeft)) {
            move.x -= 1.0f;
        }
        if (ion::input::isKeyDown(ion::Key::D) ||
            ion::input::isKeyDown(ion::Key::ArrowRight)) {
            move.x += 1.0f;
        }
        if (move.x != 0.0f || move.y != 0.0f) {
            float length = std::sqrt(move.x * move.x + move.y * move.y);
            move.x /= length;
            move.y /= length;
            player_ += move * speed * deltaTime;
        }

        // Clamp the player inside the map.
        float half = 768.0f - 16.0f; // 48 tiles * 16px
        player_.x = std::max(16.0f, std::min(half, player_.x));
        player_.y = std::max(16.0f, std::min(half, player_.y));

        heroAnim_.update(deltaTime);
        emitter_.setPosition(player_);
        emitter_.update(deltaTime);
        if (ion::input::isKeyDown(ion::Key::Space)) {
            emitter_.burst(24);
        }

        sway_ += deltaTime;
        worldCamera_.setViewport(window_.width(), window_.height());
        worldCamera_.setPosition(player_);
    }

    void drawHero_(ion::SpriteBatch& batch) {
        batch.drawSprite(heroAnim_.currentFrame(), player_, ion::Vector2(64, 64),
                         std::sin(sway_ * 3.0f) * 0.08f, ion::Vector2(32, 32),
                         ion::Color::white());
    }

    void render_() {
        renderer_.beginFrame();
        renderer_.clear(ion::Color(0.12f, 0.14f, 0.20f));

        // World pass.
        batch_.begin(worldCamera_);
        batch_.drawRect(ion::Vector2(100.0f, 100.0f), ion::Vector2(100.0f, 100.0f),
                        ion::Color::red());
        batch_.drawSprite(heroFrames_[0], ion::Vector2(300.0f, 300.0f),
                          ion::Vector2(64, 64), 0.0f, ion::Vector2(32, 32),
                          ion::Color::white());
        tilemap_.draw(batch_, worldCamera_);
        batch_.end();

        // UI pass (screen-space text).
        batch_.begin(uiCamera_);
        std::string hud = "Ion " + std::string(ion::VERSION_STRING) + " 2D";
        font_.draw(batch_, hud, ion::Vector2(12.0f, -12.0f), 16.0f,
                   ion::Color::white());
        font_.draw(batch_, "WASD: move  |  Space: burst",
                   ion::Vector2(12.0f, -32.0f), 16.0f,
                   ion::Color::cyan());
        batch_.end();

        renderer_.endFrame();
    }

    ion::Window window_;
    ion::Renderer renderer_;
    ion::SpriteBatch batch_;
    ion::Camera2D worldCamera_;
    ion::Camera2D uiCamera_;
    ion::Texture spriteSheet_;
    ion::TextureAtlas atlas_;
    ion::Tilemap tilemap_;
    ion::SpriteAnimation heroAnim_;
    std::vector<ion::SpriteRegion> heroFrames_;
    ion::ParticleEmitter emitter_;
    ion::Font font_;
    ion::Timer timer_;
    ion::Vector2 player_ = ion::Vector2(384.0f, 384.0f);
    float sway_ = 0.0f;
    int frames_ = 0;
    float elapsed_ = 0.0f;
};

} // namespace

int main(int argc, char** argv) {
    const char* backend = argc > 1 ? argv[1] : "auto";
    std::printf("Ion Engine v%s initializing\n", ion::VERSION_STRING);
    TwoDApp app;
    return app.run(backend);
}
