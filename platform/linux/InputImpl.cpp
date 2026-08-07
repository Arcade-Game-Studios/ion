#include <ion/platform/Input.hpp>
#include <ion/core/Log.hpp>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>

namespace ion {
namespace input {

namespace {
constexpr int MAX_GAMEPADS = 4;
constexpr const char* kJoystickDevices[] = {
    "/dev/input/js0", "/dev/input/js1", "/dev/input/js2", "/dev/input/js3",
};

int g_fds[MAX_GAMEPADS] = {-1, -1, -1, -1};
bool g_warnedUnsupported = false;

GamepadButton mapButton(int number) {
    switch (number) {
    case 0: return GamepadButton::A;
    case 1: return GamepadButton::B;
    case 2: return GamepadButton::X;
    case 3: return GamepadButton::Y;
    case 4: return GamepadButton::LeftBumper;
    case 5: return GamepadButton::RightBumper;
    case 6: return GamepadButton::Back;
    case 7: return GamepadButton::Start;
    case 8: return GamepadButton::Guide;
    case 9: return GamepadButton::LeftStick;
    case 10: return GamepadButton::RightStick;
    case 11: return GamepadButton::DPadUp;
    case 12: return GamepadButton::DPadDown;
    case 13: return GamepadButton::DPadLeft;
    case 14: return GamepadButton::DPadRight;
    default: return GamepadButton::A;
    }
}

GamepadAxis mapAxis(int number) {
    switch (number) {
    case 0: return GamepadAxis::LeftX;
    case 1: return GamepadAxis::LeftY;
    case 2: return GamepadAxis::RightX;
    case 3: return GamepadAxis::RightY;
    case 4: return GamepadAxis::TriggerLeft;
    case 5: return GamepadAxis::TriggerRight;
    default: return GamepadAxis::LeftX;
    }
}

} // namespace

void pollGamepads() {
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (g_fds[i] < 0) {
            g_fds[i] = ::open(kJoystickDevices[i], O_RDONLY | O_NONBLOCK);
        }

        int fd = g_fds[i];
        bool connected = fd >= 0;
        platformGamepadConnected(i, connected);
        if (!connected) {
            continue;
        }

        struct js_event event;
        while (::read(fd, &event, sizeof(event)) == (ssize_t)sizeof(event)) {
            switch (event.type & ~JS_EVENT_INIT) {
            case JS_EVENT_BUTTON:
                platformGamepadButton(i, mapButton(event.number), event.value != 0);
                break;
            case JS_EVENT_AXIS: {
                GamepadAxis axis = mapAxis(event.number);
                float value = event.value / 32767.0f;
                if (axis == GamepadAxis::TriggerLeft || axis == GamepadAxis::TriggerRight) {
                    value = value < 0.0f ? 0.0f : value;
                }
                platformGamepadAxis(i, axis, value);
                break;
            }
            default:
                break;
            }
        }
    }
}

void gamepadVibrate(int index, float leftMotor, float rightMotor) {
    (void)index;
    (void)leftMotor;
    (void)rightMotor;
    if (!g_warnedUnsupported) {
        g_warnedUnsupported = true;
        ION_LOG_WARN("%s", "Gamepad vibration is not supported on the Linux backend");
    }
}

} // namespace input
} // namespace ion
