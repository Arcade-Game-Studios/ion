# Platform

Platform types live in `ion::` and are declared under `include/ion/platform/`.

## Window

`ion::Window` creates and manages a native OS window.

```cpp
ion::WindowConfig config;
config.title = "My Game";
config.appName = "MyGame";
config.iconPath = ion::getAssetPath("assets/icon.png");
config.width = 1280;
config.height = 720;
config.resizable = true;
config.fullscreen = false;
config.onResize = [](uint32_t w, uint32_t h) {
    // Update viewports when the window is resized.
};

ion::Window window(config);
if (!window.create()) {
    return 1;
}

// Main loop
while (window.isOpen()) {
    window.pollEvents();
    // update(deltaTime);
    // render();
}
window.close();
window.destroy();
```

Methods:

- `create()` / `destroy()` — open and close the native window
- `isOpen()` / `close()` — query and request closure
- `pollEvents()` — pump OS events (call once per frame)
- `width()` / `height()` — current size in pixels
- `setFullscreen(bool)` / `isFullscreen()` — fullscreen toggle
- `nativeHandle()` — raw OS window handle for platform integration

## Input

Input state is polled once per frame through `ion::input`. The engine and
`Application` run loops call `ion::input::update()` automatically; if you drive
a `Window` manually, call it after `pollEvents()`.

### Keyboard

```cpp
using namespace ion;
if (input::isKeyDown(Key::W))       { /* held */ }
if (input::isKeyPressed(Key::Space)) { /* just pressed this frame */ }
if (input::isKeyReleased(Key::Escape)) { /* just released */ }
```

Keys are `ion::Key` (letters, digits, F-keys, arrows, modifiers, keypad, etc.).
`ion::keyName(Key)` returns a human-readable name.

### Mouse

```cpp
if (input::isMousePressed(MouseButton::Left)) { }
float x = input::mouseX();
float y = input::mouseY();
float dx = input::mouseDeltaX();
float dy = input::mouseDeltaY();
float scroll = input::mouseScrollY();
```

Buttons: `Left`, `Right`, `Middle`, `Button4`, `Button5`.

### Gamepad

```cpp
if (input::isGamepadConnected(0)) {
    float x = input::gamepadAxis(0, GamepadAxis::LeftX);
    if (input::isGamepadPressed(0, GamepadButton::A)) { }
    input::gamepadVibrate(0, 0.5f, 0.5f);
}
```

Up to `MAX_GAMEPADS` (4) controllers. Axis values are `-1..1` for sticks and
`0..1` for triggers. Buttons: `A/B/X/Y`, bumpers, back/start/guide, sticks,
DPad.

### Action maps

Bind abstract action names to physical inputs:

```cpp
ion::input::ActionMap actions;
actions.bind("jump", ion::Key::Space);
actions.bind("jump", ion::GamepadButton::A);
actions.bind("fire", ion::MouseButton::Left);

if (actions.isPressed("jump")) { /* fire jump */ }
if (actions.isDown("fire"))    { /* keep firing */ }
```

API: `bind`, `unbind`, `clear`, `hasAction`, `isDown`, `isPressed`, `isReleased`.

## Paths

```cpp
std::string dir = ion::executableDirectory();
std::string path = ion::getAssetPath("assets/icon.png");
```
