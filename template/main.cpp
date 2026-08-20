#include <ion/platform/Window.hpp>
#include <ion/platform/Input.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/Camera2D.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/Color.hpp>
#include <ion/core/Timer.hpp>
#include <ion/core/Version.hpp>

#include <cstdio>

int main() {
    ion::WindowConfig config;
    config.title = "My Game";
    config.appName = "MyGame";
    config.width = 1280;
    config.height = 720;

    ion::Window window(config);
    if (!window.create()) {
        std::printf("failed to create window\n");
        return 1;
    }

    ion::RendererConfig rendererConfig;
    rendererConfig.backend = ion::RendererBackend::Metal;

    ion::Renderer renderer;
    if (!renderer.initialize(&window, rendererConfig)) {
        std::printf("failed to initialize renderer\n");
        window.destroy();
        return 1;
    }

    ion::Camera2D camera;
    camera.setViewport(window.width(), window.height());
    camera.setZoom(1.0f);
    camera.setPosition(ion::Vector2(window.width() * 0.5f, window.height() * 0.5f));

    ion::SpriteBatch batch;
    if (!batch.initialize(&renderer, &window, 4096)) {
        std::printf("failed to initialize sprite batch\n");
        renderer.shutdown();
        window.destroy();
        return 1;
    }

    ion::Timer timer;
    timer.reset();

    std::printf("Ion Engine v%s - My Game started\n", ion::VERSION_STRING);

    while (window.isOpen()) {
        window.pollEvents();
        float dt = timer.tick();

        if (ion::input::isKeyPressed(ion::Key::Escape)) {
            break;
        }

        renderer.beginFrame();
        renderer.clear(ion::Color(0.15f, 0.15f, 0.2f));

        batch.begin(camera);
        batch.drawRect(ion::Vector2(640.0f, 360.0f), ion::Vector2(100.0f, 100.0f), ion::Color::red());
        batch.end();

        renderer.endFrame();
    }

    batch.shutdown();
    renderer.shutdown();
    window.destroy();

    std::printf("My Game exited.\n");
    return 0;
}
