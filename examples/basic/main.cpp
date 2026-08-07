#include <ion/core/Application.hpp>
#include <ion/core/Version.hpp>
#include <ion/platform/Window.hpp>

#include <cstdio>

namespace {

// A basic app using the Ion Engine API.
class BasicApp : public ion::Application {
public:
    void initialize() override {
        std::printf("Ion Engine v%s initializing\n", ion::VERSION_STRING);
    }

    void update(float deltaTime) override {
        frames_++;
        elapsed_ += deltaTime;
        if (frames_ % 60 == 0) {
            std::printf("avg %.1f ms/frame, window %ux%u\n",
                        elapsed_ / (float)frames_ * 1000.0f,
                        window_.width(), window_.height());
        }
    }

    void render() override {
    }

    void shutdown() override {
        std::printf("Ion Engine shutting down\n");
    }

    void run() {
        ion::WindowConfig config;
        config.title = "Ion Example";
        config.appName = "Ion";
        config.iconPath = "assets/ion_default_window_icon.png";
        config.width = 960;
        config.height = 540;

        window_ = ion::Window(config);
        if (!window_.create()) {
            std::printf("failed to create window\n");
            return;
        }

        initialize();
        while (window_.isOpen()) {
            window_.pollEvents();
            update(1.0f / 60.0f);
            render();
        }
        shutdown();
        window_.destroy();
    }

private:
    ion::Window window_;
    int frames_ = 0;
    float elapsed_ = 0.0f;
};

} // namespace

int main() {
    BasicApp app;
    app.run();
    return 0;
}
