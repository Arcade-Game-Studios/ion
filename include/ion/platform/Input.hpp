#pragma once

namespace ion {

enum class Key {
    Unknown,
    Space, Enter, Escape, Tab, Backspace, Delete,
    Left, Right, Up, Down,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Shift, Control, Alt,
};

enum class MouseButton {
    None,
    Left,
    Right,
    Middle,
};

class Input {
public:
    static bool isKeyDown(Key key);
    static bool isKeyPressed(Key key);
    static bool isKeyReleased(Key key);

    static bool isMouseButtonDown(MouseButton button);
    static bool isMouseButtonPressed(MouseButton button);
    static bool isMouseButtonReleased(MouseButton button);

    static float mouseX();
    static float mouseY();
};

} // namespace ion
