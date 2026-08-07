#include <ion/core/Application.hpp>
#include <ion/platform/Input.hpp>
#include <ion/platform/Window.hpp>

#include <cstdio>

namespace {

// Demonstrates keyboard, mouse, gamepad, and the ActionMap system.
class InputDemo : public ion::Application {
public:
    InputDemo() {
        windowConfig_.title = "Ion Input Demo";
        windowConfig_.appName = "Ion";
        windowConfig_.width = 960;
        windowConfig_.height = 540;
    }

    void initialize() override {
        std::printf("Input demo: press keys, click, move the mouse, connect a gamepad.\n");
        std::printf("Press ESC to quit.\n");
        actions_.bind("quit", ion::Key::Escape);
    }

    void update(float deltaTime) override {
        (void)deltaTime;
        frame_++;

        if (actions_.isPressed("quit")) {
            quit();
            return;
        }

        for (int i = 1; i < static_cast<int>(ion::Key::Count); ++i) {
            ion::Key key = static_cast<ion::Key>(i);
            if (ion::input::isKeyPressed(key)) {
                std::printf("key pressed: %s\n", ion::keyName(key));
            }
        }
        for (int i = 1; i < static_cast<int>(ion::MouseButton::Count); ++i) {
            ion::MouseButton button = static_cast<ion::MouseButton>(i);
            if (ion::input::isMousePressed(button)) {
                std::printf("mouse pressed: %s at (%.0f, %.0f)\n",
                            ion::mouseButtonName(button),
                            ion::input::mouseX(), ion::input::mouseY());
            }
        }

        if (frame_ % 60 == 0) {
            std::printf("mouse (%.0f, %.0f) delta (%.1f, %.1f) scroll (%.1f, %.1f)\n",
                        ion::input::mouseX(), ion::input::mouseY(),
                        ion::input::mouseDeltaX(), ion::input::mouseDeltaY(),
                        ion::input::mouseScrollX(), ion::input::mouseScrollY());
        }

        for (int i = 0; i < ion::MAX_GAMEPADS; ++i) {
            if (!ion::input::isGamepadConnected(i)) {
                continue;
            }
            for (int b = 1; b < static_cast<int>(ion::GamepadButton::Count); ++b) {
                ion::GamepadButton button = static_cast<ion::GamepadButton>(b);
                if (ion::input::isGamepadPressed(i, button)) {
                    std::printf("gamepad %d pressed: %s\n", i,
                                ion::gamepadButtonName(button));
                }
            }
            std::printf("gamepad %d: LX %.2f LY %.2f RX %.2f RY %.2f LT %.2f RT %.2f\n",
                        i,
                        ion::input::gamepadAxis(i, ion::GamepadAxis::LeftX),
                        ion::input::gamepadAxis(i, ion::GamepadAxis::LeftY),
                        ion::input::gamepadAxis(i, ion::GamepadAxis::RightX),
                        ion::input::gamepadAxis(i, ion::GamepadAxis::RightY),
                        ion::input::gamepadAxis(i, ion::GamepadAxis::TriggerLeft),
                        ion::input::gamepadAxis(i, ion::GamepadAxis::TriggerRight));
            if (ion::input::isGamepadPressed(i, ion::GamepadButton::A)) {
                ion::input::gamepadVibrate(i, 1.0f, 1.0f);
                std::printf("gamepad %d: vibrate on\n", i);
            } else if (ion::input::isGamepadReleased(i, ion::GamepadButton::A)) {
                ion::input::gamepadVibrate(i, 0.0f, 0.0f);
                std::printf("gamepad %d: vibrate off\n", i);
            }
        }
    }

    void render() override {
    }

private:
    ion::input::ActionMap actions_;
    int frame_ = 0;
};

} // namespace

int main() {
    InputDemo app;
    app.run();
    std::printf("Input demo exited.\n");
    return 0;
}
