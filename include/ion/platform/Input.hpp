#pragma once

#include <ion/math/Vector2.hpp>

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
// Inverse of keyName (case-insensitive); Key::Unknown if unrecognised.
// Useful for loading key bindings from a Config file.
Key keyFromName(const std::string& name);
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
// True if any key went down this frame.
bool isAnyKeyPressed();

// Text typed this frame as UTF-8 (respects keyboard layout and shift).
// Use for text fields; use isKeyPressed for game controls.
const std::string& textInput();

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
Vector2 mousePosition();
Vector2 mouseDelta();

// --- Gamepad ------------------------------------------------------------
bool isGamepadConnected(int index);
bool isGamepadDown(int index, GamepadButton button);
bool isGamepadPressed(int index, GamepadButton button);
bool isGamepadReleased(int index, GamepadButton button);
float gamepadAxis(int index, GamepadAxis axis); // sticks -1..1 (+y up), triggers 0..1
// Stick as a vector with a radial dead zone: magnitudes below deadZone read
// as zero and the rest is rescaled to 0..1, so small drift is ignored
// without a jump at the dead-zone edge.
Vector2 gamepadStick(int index, bool left, float deadZone = 0.15f);
// Index of the first connected gamepad, or -1.
int firstConnectedGamepad();
void gamepadVibrate(int index, float leftMotor, float rightMotor); // 0..1

// --- Platform hooks (called by platform backends) -----------------------
void platformKeyDown(Key key);
void platformKeyUp(Key key);
void platformMouseDown(MouseButton button);
void platformMouseUp(MouseButton button);
void platformMouseMove(float x, float y);
void platformMouseScroll(float x, float y);
// Appends a typed Unicode code point to this frame's textInput().
void platformTextInput(uint32_t codepoint);
// Releases every held key and mouse button (e.g. on focus loss), producing
// released() edges, so nothing stays stuck down.
void platformReleaseAll();
void platformGamepadConnected(int index, bool connected);
void platformGamepadButton(int index, GamepadButton button, bool down);
void platformGamepadAxis(int index, GamepadAxis axis, float value);

// Platform backends implement this; the window event pump calls it every frame.
void pollGamepads();

// --- Input mapping system ------------------------------------------------
// Maps abstract action names to keys, mouse buttons and gamepad inputs.
//
//   actions.bind("jump", Key::Space);
//   actions.bind("jump", GamepadButton::A);          // any connected pad
//   actions.bindAxis("move_x", GamepadAxis::LeftX);
//   actions.bindKeys("move_x", Key::A, Key::D);      // -1 / +1
//   float dx = actions.value("move_x");
class ActionMap {
public:
    void bind(const std::string& action, Key key);
    void bind(const std::string& action, MouseButton button);
    // gamepad -1 means any connected gamepad.
    void bind(const std::string& action, GamepadButton button, int gamepad = -1);
    // Analog binding contributing axis * scale to value(). isDown() treats
    // |axis value| above the press threshold as held.
    void bindAxis(const std::string& action, GamepadAxis axis, float scale = 1.0f,
                  int gamepad = -1);
    // Digital axis: negative key contributes -1, positive key +1.
    void bindKeys(const std::string& action, Key negative, Key positive);
    void unbind(const std::string& action);
    void clear();

    bool hasAction(const std::string& action) const;
    bool isDown(const std::string& action) const;
    bool isPressed(const std::string& action) const;
    bool isReleased(const std::string& action) const;

    // Analog value in -1..1: the largest-magnitude contribution among the
    // axis and key-pair bindings, or 1 when a digital binding is held.
    float value(const std::string& action) const;

    // Threshold above which an analog binding counts as "down" (default 0.5).
    void setPressThreshold(float threshold) { threshold_ = threshold; }

private:
    struct PadButton {
        GamepadButton button;
        int gamepad;
    };
    struct PadAxis {
        GamepadAxis axis;
        float scale;
        int gamepad;
    };
    struct KeyPair {
        Key negative;
        Key positive;
    };
    struct Binding {
        std::vector<Key> keys;
        std::vector<MouseButton> buttons;
        std::vector<PadButton> padButtons;
        std::vector<PadAxis> padAxes;
        std::vector<KeyPair> keyPairs;
    };
    float axisValue_(const PadAxis& axis) const;
    std::unordered_map<std::string, Binding> bindings_;
    float threshold_ = 0.5f;
};

} // namespace input

} // namespace ion
