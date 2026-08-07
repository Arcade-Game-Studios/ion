#include <ion/platform/Input.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <Xinput.h>
#pragma comment(lib, "Xinput.lib")

#include <algorithm>

namespace ion {
namespace input {

namespace {
constexpr int MAX_GAMEPADS = 4;

void pushButton(int index, uint16_t buttons, uint16_t mask, GamepadButton button) {
    platformGamepadButton(index, button, (buttons & mask) != 0);
}

} // namespace

void pollGamepads() {
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        XINPUT_STATE state = {};
        DWORD result = XInputGetState((DWORD)i, &state);
        bool connected = (result == ERROR_SUCCESS);
        platformGamepadConnected(i, connected);
        if (!connected) {
            continue;
        }

        const XINPUT_GAMEPAD& gamepad = state.Gamepad;
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_A, GamepadButton::A);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_B, GamepadButton::B);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_X, GamepadButton::X);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_Y, GamepadButton::Y);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER,
                   GamepadButton::LeftBumper);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                   GamepadButton::RightBumper);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_BACK, GamepadButton::Back);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_START, GamepadButton::Start);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB,
                   GamepadButton::LeftStick);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB,
                   GamepadButton::RightStick);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_DPAD_UP, GamepadButton::DPadUp);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_DPAD_DOWN,
                   GamepadButton::DPadDown);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_DPAD_LEFT,
                   GamepadButton::DPadLeft);
        pushButton(i, gamepad.wButtons, XINPUT_GAMEPAD_DPAD_RIGHT,
                   GamepadButton::DPadRight);

        platformGamepadAxis(i, GamepadAxis::LeftX, gamepad.sThumbLX / 32767.0f);
        platformGamepadAxis(i, GamepadAxis::LeftY, gamepad.sThumbLY / 32767.0f);
        platformGamepadAxis(i, GamepadAxis::RightX, gamepad.sThumbRX / 32767.0f);
        platformGamepadAxis(i, GamepadAxis::RightY, gamepad.sThumbRY / 32767.0f);
        platformGamepadAxis(i, GamepadAxis::TriggerLeft, gamepad.bLeftTrigger / 255.0f);
        platformGamepadAxis(i, GamepadAxis::TriggerRight, gamepad.bRightTrigger / 255.0f);
    }
}

void gamepadVibrate(int index, float leftMotor, float rightMotor) {
    if (index < 0 || index >= MAX_GAMEPADS) {
        return;
    }
    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed =
        (WORD)(std::max(0.0f, std::min(1.0f, leftMotor)) * 65535.0f);
    vibration.wRightMotorSpeed =
        (WORD)(std::max(0.0f, std::min(1.0f, rightMotor)) * 65535.0f);
    XInputSetState((DWORD)index, &vibration);
}

} // namespace input
} // namespace ion
