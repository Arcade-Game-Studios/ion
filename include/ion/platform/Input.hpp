#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ion {

inline constexpr int MAX_GAMEPADS = 4;

enum class Key {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
    Escape, Tab, CapsLock, Space, Enter, Backspace, Delete,
    Insert, Home, End, PageUp, PageDown,
    ArrowLeft, ArrowRight, ArrowUp, ArrowDown,
    LeftShift, RightShift, LeftControl, RightControl,
    LeftAlt, RightAlt, LeftSuper, RightSuper, Menu,
    Semicolon, Apostrophe, Comma, Period, Slash, Backslash,
    Minus, Equal, LeftBracket, RightBracket, Backtick,
    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4,
    Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadDecimal, KeypadDivide, KeypadMultiply,
    KeypadSubtract, KeypadAdd, KeypadEnter, KeypadEqual,
    Count,
};

enum class MouseButton {
    None = 0,
    Left, Right, Middle, Button4, Button5,
    Count,
};

enum class GamepadButton {
    Unknown = 0,
    A, B, X, Y,
    LeftBumper, RightBumper,
    Back, Start, Guide,
    LeftStick, RightStick,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    Count,
};

enum class GamepadAxis {
    Unknown = 0,
    LeftX, LeftY, RightX, RightY,
    TriggerLeft, TriggerRight,
    Count,
};

// Returns a human-readable name for debug/UI purposes.
const char* keyName(Key key);
const char* mouseButtonName(MouseButton button);
const char* gamepadButtonName(GamepadButton button);
const char* gamepadAxisName(GamepadAxis axis);

namespace input {

// Advances per-frame input state. Call once at the end of each frame.
// The engine and Application run loops call this automatically.
void update();

// --- Keyboard -----------------------------------------------------------
bool isKeyDown(Key key);
bool isKeyPressed(Key key);
bool isKeyReleased(Key key);

// --- Mouse --------------------------------------------------------------
bool isMouseDown(MouseButton button);
bool isMousePressed(MouseButton button);
bool isMouseReleased(MouseButton button);

float mouseX();
float mouseY();
float mouseDeltaX();
float mouseDeltaY();
float mouseScrollX();
float mouseScrollY();

// --- Gamepad ------------------------------------------------------------
bool isGamepadConnected(int index);
bool isGamepadDown(int index, GamepadButton button);
bool isGamepadPressed(int index, GamepadButton button);
bool isGamepadReleased(int index, GamepadButton button);
float gamepadAxis(int index, GamepadAxis axis); // sticks -1..1, triggers 0..1
void gamepadVibrate(int index, float leftMotor, float rightMotor); // 0..1

// --- Platform hooks (called by platform backends) -----------------------
void platformKeyDown(Key key);
void platformKeyUp(Key key);
void platformMouseDown(MouseButton button);
void platformMouseUp(MouseButton button);
void platformMouseMove(float x, float y);
void platformMouseScroll(float x, float y);
void platformGamepadConnected(int index, bool connected);
void platformGamepadButton(int index, GamepadButton button, bool down);
void platformGamepadAxis(int index, GamepadAxis axis, float value);

// Platform backends implement this; the window event pump calls it every frame.
void pollGamepads();

// --- Input mapping system ------------------------------------------------
// Maps abstract action names to keys and mouse buttons.
class ActionMap {
public:
    void bind(const std::string& action, Key key);
    void bind(const std::string& action, MouseButton button);
    void unbind(const std::string& action);
    void clear();

    bool hasAction(const std::string& action) const;
    bool isDown(const std::string& action) const;
    bool isPressed(const std::string& action) const;
    bool isReleased(const std::string& action) const;

private:
    struct Binding {
        std::vector<Key> keys;
        std::vector<MouseButton> buttons;
    };
    std::unordered_map<std::string, Binding> bindings_;
};

} // namespace input

} // namespace ion
