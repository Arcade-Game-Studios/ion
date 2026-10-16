#include <ion/platform/Input.hpp>
#include <ion/core/Log.hpp>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/joystick.h>

namespace ion {
namespace input {

namespace {

constexpr const char* kJoystickDevices[MAX_GAMEPADS] = {
    "/dev/input/js0", "/dev/input/js1", "/dev/input/js2", "/dev/input/js3",
};

// Probing absent devices costs a syscall each; only retry occasionally.
constexpr int kReopenIntervalFrames = 60;

struct Pad {
    int fd = -1;
    int reopenCountdown = 0;
    // Kernel axis/button codes (ABS_* / BTN_*) per joystick index.
    uint8_t axisMap[ABS_CNT] = {};
    uint16_t buttonMap[KEY_MAX - BTN_MISC + 1] = {};
    int axisCount = 0;
    int buttonCount = 0;
    float hatX = 0.0f;
    float hatY = 0.0f;
};

Pad g_pads[MAX_GAMEPADS];
bool g_warnedUnsupported = false;

GamepadButton buttonFromCode(uint16_t code) {
    switch (code) {
    case BTN_SOUTH: return GamepadButton::A;
    case BTN_EAST: return GamepadButton::B;
    case BTN_NORTH: return GamepadButton::X; // BTN_X
    case BTN_WEST: return GamepadButton::Y;  // BTN_Y
    case BTN_TL: return GamepadButton::LeftBumper;
    case BTN_TR: return GamepadButton::RightBumper;
    case BTN_SELECT: return GamepadButton::Back;
    case BTN_START: return GamepadButton::Start;
    case BTN_MODE: return GamepadButton::Guide;
    case BTN_THUMBL: return GamepadButton::LeftStick;
    case BTN_THUMBR: return GamepadButton::RightStick;
    case BTN_DPAD_UP: return GamepadButton::DPadUp;
    case BTN_DPAD_DOWN: return GamepadButton::DPadDown;
    case BTN_DPAD_LEFT: return GamepadButton::DPadLeft;
    case BTN_DPAD_RIGHT: return GamepadButton::DPadRight;
    default: return GamepadButton::Unknown;
    }
}

// Fallback layout (xpad order) when the mapping ioctls are unavailable.
uint16_t defaultButtonCode(int number) {
    static const uint16_t codes[] = {BTN_SOUTH, BTN_EAST,  BTN_NORTH, BTN_WEST,
                                     BTN_TL,    BTN_TR,    BTN_SELECT, BTN_START,
                                     BTN_MODE,  BTN_THUMBL, BTN_THUMBR};
    return number >= 0 && number < (int)(sizeof(codes) / sizeof(codes[0]))
               ? codes[number]
               : 0;
}

uint8_t defaultAxisCode(int number) {
    static const uint8_t codes[] = {ABS_X,  ABS_Y,  ABS_Z,     ABS_RX,
                                    ABS_RY, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};
    return number >= 0 && number < (int)sizeof(codes) ? codes[number] : ABS_MISC;
}

void closePad(int index) {
    Pad& pad = g_pads[index];
    if (pad.fd >= 0) {
        ::close(pad.fd);
        ION_LOG_INFO("Gamepad %d disconnected", index);
    }
    pad.fd = -1;
    pad.hatX = pad.hatY = 0.0f;
    pad.reopenCountdown = kReopenIntervalFrames;
    platformGamepadConnected(index, false);
}

bool openPad(int index) {
    Pad& pad = g_pads[index];
    int fd = ::open(kJoystickDevices[index], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    pad.fd = fd;

    uint8_t axes = 0;
    uint8_t buttons = 0;
    if (::ioctl(fd, JSIOCGAXES, &axes) < 0) {
        axes = 8;
    }
    if (::ioctl(fd, JSIOCGBUTTONS, &buttons) < 0) {
        buttons = 11;
    }
    pad.axisCount = axes;
    pad.buttonCount = buttons;
    if (::ioctl(fd, JSIOCGAXMAP, pad.axisMap) < 0) {
        for (int i = 0; i < axes && i < ABS_CNT; ++i) {
            pad.axisMap[i] = defaultAxisCode(i);
        }
    }
    if (::ioctl(fd, JSIOCGBTNMAP, pad.buttonMap) < 0) {
        for (int i = 0; i < buttons; ++i) {
            pad.buttonMap[i] = defaultButtonCode(i);
        }
    }

    char name[128] = "unknown";
    ::ioctl(fd, JSIOCGNAME(sizeof(name)), name);
    ION_LOG_INFO("Gamepad %d connected: %s (%d axes, %d buttons)", index, name,
                 (int)axes, (int)buttons);
    platformGamepadConnected(index, true);
    return true;
}

// Hat axes report -1/0/+1 and become D-pad buttons. Joystick Y is down-
// positive, so -1 on HAT0Y is "up".
void updateHat(int index, bool horizontal, float value) {
    Pad& pad = g_pads[index];
    float& stored = horizontal ? pad.hatX : pad.hatY;
    stored = value < -0.5f ? -1.0f : (value > 0.5f ? 1.0f : 0.0f);
    if (horizontal) {
        platformGamepadButton(index, GamepadButton::DPadLeft, stored < 0.0f);
        platformGamepadButton(index, GamepadButton::DPadRight, stored > 0.0f);
    } else {
        platformGamepadButton(index, GamepadButton::DPadUp, stored < 0.0f);
        platformGamepadButton(index, GamepadButton::DPadDown, stored > 0.0f);
    }
}

void handleAxis(int index, int number, int16_t raw) {
    Pad& pad = g_pads[index];
    if (number < 0 || number >= pad.axisCount || number >= ABS_CNT) {
        return;
    }
    float value = raw / 32767.0f;
    value = value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
    switch (pad.axisMap[number]) {
    case ABS_X: platformGamepadAxis(index, GamepadAxis::LeftX, value); break;
    // Ion reports +y up (like XInput and GameController); joystick is +y down.
    case ABS_Y: platformGamepadAxis(index, GamepadAxis::LeftY, -value); break;
    case ABS_RX: platformGamepadAxis(index, GamepadAxis::RightX, value); break;
    case ABS_RY: platformGamepadAxis(index, GamepadAxis::RightY, -value); break;
    // Triggers rest at -1 and are fully pressed at +1; Ion uses 0..1.
    case ABS_Z:
    case ABS_BRAKE:
        platformGamepadAxis(index, GamepadAxis::TriggerLeft, (value + 1.0f) * 0.5f);
        break;
    case ABS_RZ:
    case ABS_GAS:
        platformGamepadAxis(index, GamepadAxis::TriggerRight, (value + 1.0f) * 0.5f);
        break;
    case ABS_HAT0X: updateHat(index, true, value); break;
    case ABS_HAT0Y: updateHat(index, false, value); break;
    default: break;
    }
}

void handleButton(int index, int number, bool down) {
    Pad& pad = g_pads[index];
    if (number < 0 || number >= pad.buttonCount) {
        return;
    }
    GamepadButton button = buttonFromCode(pad.buttonMap[number]);
    if (button != GamepadButton::Unknown) {
        platformGamepadButton(index, button, down);
    }
}

} // namespace

void pollGamepads() {
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        Pad& pad = g_pads[i];
        if (pad.fd < 0) {
            if (pad.reopenCountdown > 0) {
                pad.reopenCountdown--;
                continue;
            }
            if (!openPad(i)) {
                pad.reopenCountdown = kReopenIntervalFrames;
                continue;
            }
        }

        struct js_event event;
        for (;;) {
            ssize_t n = ::read(pad.fd, &event, sizeof(event));
            if (n == (ssize_t)sizeof(event)) {
                switch (event.type & ~JS_EVENT_INIT) {
                case JS_EVENT_BUTTON: handleButton(i, event.number, event.value != 0); break;
                case JS_EVENT_AXIS: handleAxis(i, event.number, event.value); break;
                default: break;
                }
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                break; // drained
            }
            closePad(i); // ENODEV (unplugged) or a short read
            break;
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
