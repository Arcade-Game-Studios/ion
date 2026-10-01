#include "Game.hpp"
#include "Sfx.hpp"

#include <ion/audio/Audio.hpp>
#include <ion/core/Timer.hpp>
#include <ion/platform/Input.hpp>
#include <ion/platform/Window.hpp>
#include <ion/render/Camera2D.hpp>
#include <ion/render/ParticleSystem.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/Text.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace asteroids;

namespace {

ion::Vector2 rotated(ion::Vector2 v, float a) {
    float c = std::cos(a), s = std::sin(a);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

ion::ParticleEmitterConfig sparkConfig(float speed, float lifetime, ion::Color color) {
    ion::ParticleEmitterConfig c;
    c.capacity = 512;
    c.lifetime = lifetime;
    c.speed = speed;
    c.speedSpread = speed * 0.5f;
    c.angle = 0.0f;
    c.angleSpread = PI;
    c.startSize = 4.0f;
    c.endSize = 1.0f;
    c.startColor = color;
    c.endColor = ion::Color(color.r, color.g, color.b, 0.0f);
    return c;
}

// Draws a copy at each wrapped position so objects crossing a border appear
// on both sides.
template <typename Fn>
void drawWrapped(ion::Vector2 pos, float extent, Fn&& draw) {
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            ion::Vector2 p(pos.x + dx * WORLD_W, pos.y + dy * WORLD_H);
            if (p.x > -extent && p.x < WORLD_W + extent &&
                p.y > -extent && p.y < WORLD_H + extent) {
                draw(p);
            }
        }
    }
}

class App {
public:
    int run(const char* backendName) {
        ion::WindowConfig config;
        config.title = "Ion Asteroids";
        config.appName = "IonAsteroids";
        config.width = (uint32_t)WORLD_W;
        config.height = (uint32_t)WORLD_H;
        window_ = ion::Window(config);
        if (!window_.create()) {
            std::printf("failed to create window\n");
            return 1;
        }

        ion::RendererConfig rc;
        if (std::strcmp(backendName, "metal") == 0) rc.backend = ion::RendererBackend::Metal;
        else if (std::strcmp(backendName, "gl") == 0) rc.backend = ion::RendererBackend::OpenGL;
        else if (std::strcmp(backendName, "null") == 0) rc.backend = ion::RendererBackend::Null;
        if (!renderer_.initialize(&window_, rc) ||
            !batch_.initialize(&renderer_, &window_) || !font_.initialize(&renderer_)) {
            std::printf("failed to initialize renderer\n");
            window_.destroy();
            return 1;
        }

        audio_.initialize(); // silent fallback if no device
        sfx_ = makeSfx();

        sparks_.setConfig(sparkConfig(160.0f, 0.7f, ion::Color(1.0f, 0.8f, 0.3f)));
        debris_.setConfig(sparkConfig(110.0f, 1.1f, ion::Color(0.8f, 0.85f, 1.0f)));
        flameConfig_ = sparkConfig(70.0f, 0.3f, ion::Color(1.0f, 0.5f, 0.2f));
        flame_.setConfig(flameConfig_);

        camera_.setViewport(window_.width(), window_.height());
        camera_.setPosition(ion::Vector2(WORLD_W * 0.5f, WORLD_H * 0.5f));

        timer_.reset();
        while (window_.isOpen()) {
            window_.pollEvents();
            float dt = std::min(timer_.tick(), 1.0f / 20.0f);
            if (ion::input::isKeyPressed(ion::Key::Escape)) {
                break;
            }
            frame_(dt);
            ion::input::update();
        }

        audio_.shutdown();
        font_.shutdown();
        renderer_.shutdown();
        window_.destroy();
        return 0;
    }

private:
    static bool down(ion::Key a, ion::Key b) {
        return ion::input::isKeyDown(a) || ion::input::isKeyDown(b);
    }

    void frame_(float dt) {
        namespace in = ion::input;
        Input input;
        input.turn = (down(ion::Key::A, ion::Key::ArrowLeft) ? 1.0f : 0.0f) -
                     (down(ion::Key::D, ion::Key::ArrowRight) ? 1.0f : 0.0f);
        input.thrust = down(ion::Key::W, ion::Key::ArrowUp);
        input.fire = in::isKeyDown(ion::Key::Space);
        input.restart = in::isKeyPressed(ion::Key::Enter);
        if (in::isKeyPressed(ion::Key::P)) {
            paused_ = !paused_;
            if (paused_ && thrustVoice_ != ion::INVALID_VOICE) {
                audio_.stop(thrustVoice_);
                thrustVoice_ = ion::INVALID_VOICE;
            }
        }

        if (!paused_) {
            game_.update(dt, input);
            playSounds_(input);
            for (const Explosion& e : game_.takeExplosions()) {
                playExplosion_(e);
                auto& emitter = e.ship ? debris_ : sparks_;
                emitter.setPosition(e.pos);
                emitter.burst(e.ship ? 60 : (uint32_t)(e.radius * 0.8f));
            }
            sparks_.update(dt);
            debris_.update(dt);

            if (auto* p = game_.registry().tryGet<Player>(game_.player())) {
                const auto& t = game_.registry().get<Transform>(game_.player());
                if (p->thrusting) {
                    flameConfig_.angle = t.angle + PI;
                    flameConfig_.angleSpread = 0.3f;
                    flame_.setConfig(flameConfig_);
                    flame_.setPosition(t.pos - Game::forward(t.angle) * 12.0f);
                    flame_.burst(2);
                }
            }
            flame_.update(dt);
        }
        render_();
    }

    static float panFor_(ion::Vector2 pos) {
        return std::clamp((pos.x / WORLD_W) * 2.0f - 1.0f, -1.0f, 1.0f) * 0.7f;
    }

    // Translates this frame's game events into sounds (explosions are handled
    // alongside their particles in frame_).
    void playSounds_(const Input& input) {
        if (game_.shotsFired() != lastShots_) {
            lastShots_ = game_.shotsFired();
            audio_.play(sfx_.shoot, {.volume = 0.6f, .pitch = 0.95f + 0.1f * (lastShots_ % 3)});
        }
        if (game_.wave() != lastWave_) {
            if (game_.wave() > 1) {
                audio_.play(sfx_.wave, {.volume = 0.8f});
            }
            lastWave_ = game_.wave();
        }

        // Engine rumble follows the thrust key while the ship is alive.
        bool thrusting = input.thrust && game_.registry().valid(game_.player());
        if (thrusting && !audio_.mixer().isPlaying(thrustVoice_)) {
            thrustVoice_ = audio_.play(sfx_.thrust, {.volume = 0.5f, .loop = true});
        } else if (!thrusting && thrustVoice_ != ion::INVALID_VOICE) {
            audio_.stop(thrustVoice_);
            thrustVoice_ = ion::INVALID_VOICE;
        }
    }

    void playExplosion_(const Explosion& e) {
        const ion::Sound& snd = e.ship ? sfx_.shipDown
                              : e.radius > 40.0f ? sfx_.boomLarge
                              : e.radius > 20.0f ? sfx_.boomMedium
                                                 : sfx_.boomSmall;
        audio_.play(snd, {.volume = e.ship ? 1.0f : 0.8f, .pan = panFor_(e.pos),
                          .pitch = 0.9f + 0.2f * (float)(explosionCount_++ % 5) / 4.0f});
    }

    void drawShip_(const Transform& t, const Player& p) {
        // Blink while invulnerable.
        if (p.invulnerable > 0.0f && std::fmod(p.invulnerable, 0.25f) > 0.125f) {
            return;
        }
        drawWrapped(t.pos, 30.0f, [&](ion::Vector2 c) {
            ion::Vector2 nose = c + rotated({18, 0}, t.angle);
            ion::Vector2 left = c + rotated({-12, 11}, t.angle);
            ion::Vector2 right = c + rotated({-12, -11}, t.angle);
            ion::Vector2 notch = c + rotated({-6, 0}, t.angle);
            ion::Color col = ion::Color::white();
            batch_.drawLine(nose, left, 2.0f, col);
            batch_.drawLine(left, notch, 2.0f, col);
            batch_.drawLine(notch, right, 2.0f, col);
            batch_.drawLine(right, nose, 2.0f, col);
        });
    }

    void drawAsteroid_(const Transform& t, const Asteroid& a) {
        float r = radiusForSize(a.size);
        drawWrapped(t.pos, r * 1.3f, [&](ion::Vector2 c) {
            ion::Vector2 prev;
            for (int i = 0; i <= SHAPE_POINTS; ++i) {
                int k = i % SHAPE_POINTS;
                float ang = t.angle + 2.0f * PI * k / SHAPE_POINTS;
                ion::Vector2 pt = c + ion::Vector2(std::cos(ang), std::sin(ang)) * (r * a.shape[k]);
                if (i > 0) {
                    batch_.drawLine(prev, pt, 2.0f, ion::Color(0.75f, 0.8f, 0.9f));
                }
                prev = pt;
            }
        });
    }

    void centered_(const std::string& text, float y, float size, ion::Color color) {
        ion::Vector2 m = font_.measure(text, size);
        font_.draw(batch_, text, ion::Vector2((WORLD_W - m.x) * 0.5f, y), size, color);
    }

    void render_() {
        auto& reg = game_.registry();
        renderer_.beginFrame();
        renderer_.clear(ion::Color(0.02f, 0.02f, 0.06f));
        batch_.begin(camera_);

        reg.view<Asteroid, Transform>().each(
            [&](ion::EntityId, Asteroid& a, Transform& t) { drawAsteroid_(t, a); });
        reg.view<Bullet, Transform>().each([&](ion::EntityId, Bullet&, Transform& t) {
            batch_.drawCircle(t.pos, 3.0f, ion::Color(1.0f, 0.95f, 0.5f), 8);
        });
        if (auto* p = reg.tryGet<Player>(game_.player())) {
            drawShip_(reg.get<Transform>(game_.player()), *p);
        }
        flame_.draw(batch_);
        sparks_.draw(batch_);
        debris_.draw(batch_);

        // HUD. Text is positioned by its bottom-left corner, y is up.
        const float top = WORLD_H - 36.0f;
        font_.draw(batch_, "SCORE " + std::to_string(game_.score()),
                   ion::Vector2(20.0f, top), 20.0f);
        font_.draw(batch_, "WAVE " + std::to_string(game_.wave()),
                   ion::Vector2(WORLD_W * 0.5f - 40.0f, top), 20.0f, ion::Color::cyan());
        font_.draw(batch_, "BEST " + std::to_string(game_.highScore()),
                   ion::Vector2(WORLD_W - 190.0f, top), 20.0f, ion::Color::yellow());
        for (int i = 0; i < game_.lives(); ++i) {
            ion::Vector2 c(30.0f + i * 28.0f, top - 28.0f);
            ion::Color col = ion::Color(0.6f, 0.9f, 1.0f);
            batch_.drawLine(c + ion::Vector2(0, 12), c + ion::Vector2(-8, -8), 2.0f, col);
            batch_.drawLine(c + ion::Vector2(0, 12), c + ion::Vector2(8, -8), 2.0f, col);
            batch_.drawLine(c + ion::Vector2(-8, -8), c + ion::Vector2(8, -8), 2.0f, col);
        }

        if (game_.state() == State::GameOver) {
            centered_("GAME OVER", WORLD_H * 0.5f + 40.0f, 48.0f, ion::Color::red());
            centered_("Press Enter to play again", WORLD_H * 0.5f - 20.0f, 20.0f,
                      ion::Color::white());
        } else if (paused_) {
            centered_("PAUSED", WORLD_H * 0.5f + 20.0f, 40.0f, ion::Color::yellow());
        } else if (game_.wave() == 1 && game_.score() == 0) {
            centered_("A/D or arrows: turn   W: thrust   Space: fire   P: pause",
                      60.0f, 16.0f, ion::Color(0.6f, 0.6f, 0.7f));
        }

        batch_.end();
        renderer_.endFrame();
    }

    ion::Window window_;
    ion::Renderer renderer_;
    ion::SpriteBatch batch_;
    ion::Camera2D camera_;
    ion::Font font_;
    ion::Timer timer_;
    ion::ParticleEmitter sparks_, debris_, flame_;
    ion::ParticleEmitterConfig flameConfig_;
    Game game_;
    ion::Audio audio_;
    Sfx sfx_;
    ion::VoiceId thrustVoice_ = ion::INVALID_VOICE;
    int lastShots_ = 0;
    int lastWave_ = 1;
    int explosionCount_ = 0;
    bool paused_ = false;
};

} // namespace

int main(int argc, char** argv) {
    App app;
    return app.run(argc > 1 ? argv[1] : "auto");
}
