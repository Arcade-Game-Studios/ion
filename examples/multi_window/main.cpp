#include <ion/platform/Window.hpp>

#include <cstdio>

namespace {

// Creates two windows and pumps events for both in a single loop.
void onResize(const char* name, uint32_t width, uint32_t height) {
    std::printf("[%s] resized to %ux%u\n", name, width, height);
    std::fflush(stdout);
}

} // namespace

int main() {
    ion::WindowConfig primaryConfig;
    primaryConfig.title = "Ion Primary";
    primaryConfig.appName = "Ion";
    primaryConfig.width = 960;
    primaryConfig.height = 540;
    primaryConfig.onResize = [](uint32_t w, uint32_t h) {
        onResize("primary", w, h);
    };

    ion::WindowConfig secondaryConfig;
    secondaryConfig.title = "Ion Secondary";
    secondaryConfig.width = 640;
    secondaryConfig.height = 480;
    secondaryConfig.onResize = [](uint32_t w, uint32_t h) {
        onResize("secondary", w, h);
    };

    ion::Window primary(primaryConfig);
    ion::Window secondary(secondaryConfig);

    if (!primary.create() || !secondary.create()) {
        std::printf("failed to create window(s)\n");
        return 1;
    }

    std::printf("two windows open: primary %ux%u, secondary %ux%u\n",
                primary.width(), primary.height(),
                secondary.width(), secondary.height());

    int toggleTimer = 0;
    while (primary.isOpen() || secondary.isOpen()) {
        primary.pollEvents();
        secondary.pollEvents();

        if (toggleTimer == 180) {
            std::printf("toggling primary fullscreen to %s\n",
                        primary.isFullscreen() ? "false" : "true");
            primary.setFullscreen(!primary.isFullscreen());
        }
        toggleTimer++;

        if (!primary.isOpen()) {
            secondary.close();
        }
    }

    primary.destroy();
    secondary.destroy();
    std::printf("all windows closed\n");
    return 0;
}
