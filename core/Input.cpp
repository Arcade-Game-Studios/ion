#include <ion/platform/Input.hpp>

#include <cctype>
#include <cmath>
#include <cstring>

namespace ion {

namespace {

struct ButtonState {
    bool down = false;
    bool pressed = false;
    bool released = false;
};

constexpr int KEY_COUNT = static_cast<int>(Key::Count);
constexpr int MOUSE_COUNT = static_cast<int>(MouseButton::Count);
constexpr int GAMEPAD_BUTTON_COUNT = static_cast<int>(GamepadButton::Count);
constexpr int GAMEPAD_AXIS_COUNT = static_cast<int>(GamepadAxis::Count);

ButtonState g_keyStates[KEY_COUNT];
ButtonState g_mouseStates[MOUSE_COUNT];
ButtonState g_gamepadStates[MAX_GAMEPADS][GAMEPAD_BUTTON_COUNT];
bool g_gamepadConnected[MAX_GAMEPADS] = {};
float g_gamepadAxes[MAX_GAMEPADS][GAMEPAD_AXIS_COUNT] = {};

float g_mouseX = 0.0f;
float g_mouseY = 0.0f;
float g_prevMouseX = 0.0f;
float g_prevMouseY = 0.0f;
float g_scrollX = 0.0f;
float g_scrollY = 0.0f;
bool g_anyKeyPressed = false;
std::string g_textInput;

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x110000) {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

int keyIndex(Key key) {
    int index = static_cast<int>(key);
    return (index > 0 && index < KEY_COUNT) ? index : -1;
}

int mouseIndex(MouseButton button) {
    int index = static_cast<int>(button);
    return (index > 0 && index < MOUSE_COUNT) ? index : -1;
}

int gamepadButtonIndex(GamepadButton button) {
    int index = static_cast<int>(button);
    return (index > 0 && index < GAMEPAD_BUTTON_COUNT) ? index : -1;
}

void clearEdges(ButtonState* states, int count) {
    for (int i = 0; i < count; ++i) {
        states[i].pressed = false;
        states[i].released = false;
    }
}

} // namespace

const char* keyName(Key key) {
    static const char* names[] = {
        "Unknown", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K",
        "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y",
        "Z", "Num0", "Num1", "Num2", "Num3", "Num4", "Num5", "Num6", "Num7",
        "Num8", "Num9", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9",
        "F10", "F11", "F12", "F13", "F14", "F15", "F16", "F17", "F18", "F19",
        "F20", "F21", "F22", "F23", "F24", "Escape", "Tab", "CapsLock",
        "Space", "Enter", "Backspace", "Delete", "Insert", "Home", "End",
        "PageUp", "PageDown", "ArrowLeft", "ArrowRight", "ArrowUp",
        "ArrowDown", "LeftShift", "RightShift", "LeftControl", "RightControl",
        "LeftAlt", "RightAlt", "LeftSuper", "RightSuper", "Menu", "Semicolon",
        "Apostrophe", "Comma", "Period", "Slash", "Backslash", "Minus",
        "Equal", "LeftBracket", "RightBracket", "Backtick", "Keypad0",
        "Keypad1", "Keypad2", "Keypad3", "Keypad4", "Keypad5", "Keypad6",
        "Keypad7", "Keypad8", "Keypad9", "KeypadDecimal", "KeypadDivide",
        "KeypadMultiply", "KeypadSubtract", "KeypadAdd", "KeypadEnter",
        "KeypadEqual",
    };
    int index = static_cast<int>(key);
    if (index < 0 || index >= KEY_COUNT) {
        return "Unknown";
    }
    return names[index];
}

Key keyFromName(const std::string& name) {
    auto lower = [](std::string text) {
        for (char& c : text) {
            c = (char)std::tolower((unsigned char)c);
        }
        return text;
    };
    std::string wanted = lower(name);
    for (int i = 1; i < KEY_COUNT; ++i) {
        Key key = static_cast<Key>(i);
        if (lower(keyName(key)) == wanted) {
            return key;
        }
    }
    return Key::Unknown;
}

const char* mouseButtonName(MouseButton button) {
    static const char* names[] = {
        "None", "Left", "Right", "Middle", "Button4", "Button5",
    };
    int index = static_cast<int>(button);
    if (index < 0 || index >= MOUSE_COUNT) {
        return "None";
    }
    return names[index];
}

const char* gamepadButtonName(GamepadButton button) {
    static const char* names[] = {
        "Unknown", "A", "B", "X", "Y", "LeftBumper", "RightBumper", "Back",
        "Start", "Guide", "LeftStick", "RightStick", "DPadUp", "DPadDown",
        "DPadLeft", "DPadRight",
    };
    int index = static_cast<int>(button);
    if (index < 0 || index >= GAMEPAD_BUTTON_COUNT) {
        return "Unknown";
    }
    return names[index];
}

const char* gamepadAxisName(GamepadAxis axis) {
    static const char* names[] = {
        "Unknown", "LeftX", "LeftY", "RightX", "RightY", "TriggerLeft",
        "TriggerRight",
    };
    int index = static_cast<int>(axis);
    if (index < 0 || index >= GAMEPAD_AXIS_COUNT) {
        return "Unknown";
    }
    return names[index];
}

namespace input {

void update() {
    clearEdges(g_keyStates, KEY_COUNT);
    clearEdges(g_mouseStates, MOUSE_COUNT);
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        clearEdges(g_gamepadStates[i], GAMEPAD_BUTTON_COUNT);
    }
    g_prevMouseX = g_mouseX;
    g_prevMouseY = g_mouseY;
    g_scrollX = 0.0f;
    g_scrollY = 0.0f;
    g_anyKeyPressed = false;
    g_textInput.clear();
}

bool isAnyKeyPressed() {
    return g_anyKeyPressed;
}

const std::string& textInput() {
    return g_textInput;
}

Vector2 mousePosition() {
    return Vector2(g_mouseX, g_mouseY);
}

Vector2 mouseDelta() {
    return Vector2(g_mouseX - g_prevMouseX, g_mouseY - g_prevMouseY);
}

Vector2 gamepadStick(int index, bool left, float deadZone) {
    Vector2 v(gamepadAxis(index, left ? GamepadAxis::LeftX : GamepadAxis::RightX),
              gamepadAxis(index, left ? GamepadAxis::LeftY : GamepadAxis::RightY));
    float len = v.length();
    if (len <= deadZone || len <= 0.0f) {
        return Vector2();
    }
    float scaled = (len - deadZone) / (1.0f - deadZone);
    if (scaled > 1.0f) {
        scaled = 1.0f;
    }
    return v * (scaled / len);
}

int firstConnectedGamepad() {
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (g_gamepadConnected[i]) {
            return i;
        }
    }
    return -1;
}

bool isKeyDown(Key key) {
    int index = keyIndex(key);
    return index >= 0 && g_keyStates[index].down;
}

bool isKeyPressed(Key key) {
    int index = keyIndex(key);
    return index >= 0 && g_keyStates[index].pressed;
}

bool isKeyReleased(Key key) {
    int index = keyIndex(key);
    return index >= 0 && g_keyStates[index].released;
}

bool isMouseDown(MouseButton button) {
    int index = mouseIndex(button);
    return index >= 0 && g_mouseStates[index].down;
}

bool isMousePressed(MouseButton button) {
    int index = mouseIndex(button);
    return index >= 0 && g_mouseStates[index].pressed;
}

bool isMouseReleased(MouseButton button) {
    int index = mouseIndex(button);
    return index >= 0 && g_mouseStates[index].released;
}

float mouseX() {
    return g_mouseX;
}

float mouseY() {
    return g_mouseY;
}

float mouseDeltaX() {
    return g_mouseX - g_prevMouseX;
}

float mouseDeltaY() {
    return g_mouseY - g_prevMouseY;
}

float mouseScrollX() {
    return g_scrollX;
}

float mouseScrollY() {
    return g_scrollY;
}

bool isGamepadConnected(int index) {
    if (index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }
    return g_gamepadConnected[index];
}

bool isGamepadDown(int index, GamepadButton button) {
    if (index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }
    int buttonIndex = gamepadButtonIndex(button);
    return buttonIndex >= 0 && g_gamepadStates[index][buttonIndex].down;
}

bool isGamepadPressed(int index, GamepadButton button) {
    if (index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }
    int buttonIndex = gamepadButtonIndex(button);
    return buttonIndex >= 0 && g_gamepadStates[index][buttonIndex].pressed;
}

bool isGamepadReleased(int index, GamepadButton button) {
    if (index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }
    int buttonIndex = gamepadButtonIndex(button);
    return buttonIndex >= 0 && g_gamepadStates[index][buttonIndex].released;
}

float gamepadAxis(int index, GamepadAxis axis) {
    if (index < 0 || index >= MAX_GAMEPADS) {
        return 0.0f;
    }
    int axisIndex = static_cast<int>(axis);
    if (axisIndex <= 0 || axisIndex >= GAMEPAD_AXIS_COUNT) {
        return 0.0f;
    }
    return g_gamepadAxes[index][axisIndex];
}

void platformKeyDown(Key key) {
    int index = keyIndex(key);
    if (index < 0) {
        return;
    }
    ButtonState& state = g_keyStates[index];
    if (!state.down) {
        state.down = true;
        state.pressed = true;
        g_anyKeyPressed = true;
    }
}

void platformKeyUp(Key key) {
    int index = keyIndex(key);
    if (index < 0) {
        return;
    }
    ButtonState& state = g_keyStates[index];
    if (state.down) {
        state.down = false;
        state.released = true;
    }
}

void platformMouseDown(MouseButton button) {
    int index = mouseIndex(button);
    if (index < 0) {
        return;
    }
    ButtonState& state = g_mouseStates[index];
    if (!state.down) {
        state.down = true;
        state.pressed = true;
    }
}

void platformMouseUp(MouseButton button) {
    int index = mouseIndex(button);
    if (index < 0) {
        return;
    }
    ButtonState& state = g_mouseStates[index];
    if (state.down) {
        state.down = false;
        state.released = true;
    }
}

void platformMouseMove(float x, float y) {
    g_mouseX = x;
    g_mouseY = y;
}

void platformMouseScroll(float x, float y) {
    g_scrollX += x;
    g_scrollY += y;
}

void platformTextInput(uint32_t codepoint) {
    // Skip control characters (backspace, enter, escape...): those are
    // handled through key events.
    if (codepoint < 0x20 || codepoint == 0x7F) {
        return;
    }
    appendUtf8(g_textInput, codepoint);
}

void platformReleaseAll() {
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (g_keyStates[i].down) {
            g_keyStates[i].down = false;
            g_keyStates[i].released = true;
        }
    }
    for (int i = 0; i < MOUSE_COUNT; ++i) {
        if (g_mouseStates[i].down) {
            g_mouseStates[i].down = false;
            g_mouseStates[i].released = true;
        }
    }
}

void platformGamepadConnected(int index, bool connected) {
    if (index < 0 || index >= MAX_GAMEPADS) {
        return;
    }
    if (g_gamepadConnected[index] && !connected) {
        // Unplugged: release held buttons and center the axes so nothing
        // keeps firing from stale state.
        for (int b = 0; b < GAMEPAD_BUTTON_COUNT; ++b) {
            ButtonState& state = g_gamepadStates[index][b];
            if (state.down) {
                state.down = false;
                state.released = true;
            }
        }
        for (int a = 0; a < GAMEPAD_AXIS_COUNT; ++a) {
            g_gamepadAxes[index][a] = 0.0f;
        }
    }
    g_gamepadConnected[index] = connected;
}

void platformGamepadButton(int index, GamepadButton button, bool down) {
    if (index < 0 || index >= MAX_GAMEPADS) {
        return;
    }
    int buttonIndex = gamepadButtonIndex(button);
    if (buttonIndex < 0) {
        return;
    }
    ButtonState& state = g_gamepadStates[index][buttonIndex];
    if (down && !state.down) {
        state.down = true;
        state.pressed = true;
    } else if (!down && state.down) {
        state.down = false;
        state.released = true;
    }
}

void platformGamepadAxis(int index, GamepadAxis axis, float value) {
    if (index < 0 || index >= MAX_GAMEPADS) {
        return;
    }
    int axisIndex = static_cast<int>(axis);
    if (axisIndex <= 0 || axisIndex >= GAMEPAD_AXIS_COUNT) {
        return;
    }
    g_gamepadAxes[index][axisIndex] = value;
}

void ActionMap::bind(const std::string& action, Key key) {
    Binding& binding = bindings_[action];
    for (Key bound : binding.keys) {
        if (bound == key) {
            return;
        }
    }
    binding.keys.push_back(key);
}

void ActionMap::bind(const std::string& action, MouseButton button) {
    Binding& binding = bindings_[action];
    for (MouseButton bound : binding.buttons) {
        if (bound == button) {
            return;
        }
    }
    binding.buttons.push_back(button);
}

void ActionMap::bind(const std::string& action, GamepadButton button, int gamepad) {
    Binding& binding = bindings_[action];
    for (const PadButton& bound : binding.padButtons) {
        if (bound.button == button && bound.gamepad == gamepad) {
            return;
        }
    }
    binding.padButtons.push_back({button, gamepad});
}

void ActionMap::bindAxis(const std::string& action, GamepadAxis axis, float scale,
                         int gamepad) {
    bindings_[action].padAxes.push_back({axis, scale, gamepad});
}

void ActionMap::bindKeys(const std::string& action, Key negative, Key positive) {
    bindings_[action].keyPairs.push_back({negative, positive});
}

void ActionMap::unbind(const std::string& action) {
    bindings_.erase(action);
}

void ActionMap::clear() {
    bindings_.clear();
}

bool ActionMap::hasAction(const std::string& action) const {
    return bindings_.count(action) > 0;
}

namespace {

// Applies `test` to the bound pad, or to every pad when gamepad == -1.
template <typename Fn>
bool anyPad(int gamepad, Fn&& test) {
    if (gamepad >= 0) {
        return test(gamepad);
    }
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (g_gamepadConnected[i] && test(i)) {
            return true;
        }
    }
    return false;
}

} // namespace

float ActionMap::axisValue_(const PadAxis& axis) const {
    float best = 0.0f;
    auto consider = [&](int pad) {
        float v = gamepadAxis(pad, axis.axis) * axis.scale;
        if (std::fabs(v) > std::fabs(best)) {
            best = v;
        }
        return false;
    };
    anyPad(axis.gamepad, consider);
    return best;
}

bool ActionMap::isDown(const std::string& action) const {
    auto it = bindings_.find(action);
    if (it == bindings_.end()) {
        return false;
    }
    const Binding& b = it->second;
    for (Key key : b.keys) {
        if (isKeyDown(key)) {
            return true;
        }
    }
    for (MouseButton button : b.buttons) {
        if (isMouseDown(button)) {
            return true;
        }
    }
    for (const PadButton& pb : b.padButtons) {
        if (anyPad(pb.gamepad, [&](int pad) { return isGamepadDown(pad, pb.button); })) {
            return true;
        }
    }
    for (const PadAxis& axis : b.padAxes) {
        if (std::fabs(axisValue_(axis)) > threshold_) {
            return true;
        }
    }
    for (const KeyPair& pair : b.keyPairs) {
        if (isKeyDown(pair.negative) || isKeyDown(pair.positive)) {
            return true;
        }
    }
    return false;
}

bool ActionMap::isPressed(const std::string& action) const {
    auto it = bindings_.find(action);
    if (it == bindings_.end()) {
        return false;
    }
    const Binding& b = it->second;
    for (Key key : b.keys) {
        if (isKeyPressed(key)) {
            return true;
        }
    }
    for (MouseButton button : b.buttons) {
        if (isMousePressed(button)) {
            return true;
        }
    }
    for (const PadButton& pb : b.padButtons) {
        if (anyPad(pb.gamepad, [&](int pad) { return isGamepadPressed(pad, pb.button); })) {
            return true;
        }
    }
    for (const KeyPair& pair : b.keyPairs) {
        if (isKeyPressed(pair.negative) || isKeyPressed(pair.positive)) {
            return true;
        }
    }
    return false;
}

bool ActionMap::isReleased(const std::string& action) const {
    auto it = bindings_.find(action);
    if (it == bindings_.end()) {
        return false;
    }
    const Binding& b = it->second;
    for (Key key : b.keys) {
        if (isKeyReleased(key)) {
            return true;
        }
    }
    for (MouseButton button : b.buttons) {
        if (isMouseReleased(button)) {
            return true;
        }
    }
    for (const PadButton& pb : b.padButtons) {
        if (anyPad(pb.gamepad, [&](int pad) { return isGamepadReleased(pad, pb.button); })) {
            return true;
        }
    }
    for (const KeyPair& pair : b.keyPairs) {
        if (isKeyReleased(pair.negative) || isKeyReleased(pair.positive)) {
            return true;
        }
    }
    return false;
}

float ActionMap::value(const std::string& action) const {
    auto it = bindings_.find(action);
    if (it == bindings_.end()) {
        return 0.0f;
    }
    const Binding& b = it->second;
    float best = 0.0f;
    auto consider = [&](float v) {
        if (std::fabs(v) > std::fabs(best)) {
            best = v;
        }
    };
    for (const PadAxis& axis : b.padAxes) {
        consider(axisValue_(axis));
    }
    for (const KeyPair& pair : b.keyPairs) {
        consider((isKeyDown(pair.positive) ? 1.0f : 0.0f) -
                 (isKeyDown(pair.negative) ? 1.0f : 0.0f));
    }
    if (best == 0.0f) {
        bool digital = false;
        for (Key key : b.keys) {
            digital = digital || isKeyDown(key);
        }
        for (MouseButton button : b.buttons) {
            digital = digital || isMouseDown(button);
        }
        for (const PadButton& pb : b.padButtons) {
            digital = digital ||
                      anyPad(pb.gamepad, [&](int pad) { return isGamepadDown(pad, pb.button); });
        }
        if (digital) {
            best = 1.0f;
        }
    }
    if (best > 1.0f) {
        best = 1.0f;
    } else if (best < -1.0f) {
        best = -1.0f;
    }
    return best;
}

} // namespace input

} // namespace ion
