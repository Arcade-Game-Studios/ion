#include <ion/platform/Input.hpp>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::printf("FAIL input_test.cpp:%d %s\n", line, expression);
        failures++;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void testKeyboard() {
    using namespace ion;
    input::update();

    CHECK(!input::isKeyDown(Key::Space));
    CHECK(!input::isKeyPressed(Key::Space));
    CHECK(!input::isKeyReleased(Key::Space));

    input::platformKeyDown(Key::Space);
    CHECK(input::isKeyDown(Key::Space));
    CHECK(input::isKeyPressed(Key::Space));
    CHECK(!input::isKeyReleased(Key::Space));

    input::update();
    CHECK(input::isKeyDown(Key::Space));
    CHECK(!input::isKeyPressed(Key::Space));

    input::platformKeyDown(Key::Space);
    CHECK(!input::isKeyPressed(Key::Space));

    input::platformKeyUp(Key::Space);
    CHECK(!input::isKeyDown(Key::Space));
    CHECK(!input::isKeyPressed(Key::Space));
    CHECK(input::isKeyReleased(Key::Space));

    input::update();
    CHECK(!input::isKeyReleased(Key::Space));

    input::platformKeyDown(Key::Unknown);
    input::platformKeyDown(static_cast<Key>(0));
    input::platformKeyDown(static_cast<Key>(static_cast<int>(Key::Count) + 5));
    input::update();
}

void testMouse() {
    using namespace ion;
    input::update();

    input::platformMouseMove(100.0f, 200.0f);
    CHECK(input::mouseX() == 100.0f);
    CHECK(input::mouseY() == 200.0f);

    input::update();
    CHECK(input::mouseDeltaX() == 0.0f);
    CHECK(input::mouseDeltaY() == 0.0f);

    input::platformMouseDown(MouseButton::Left);
    CHECK(input::isMouseDown(MouseButton::Left));
    CHECK(input::isMousePressed(MouseButton::Left));

    input::platformMouseMove(110.0f, 220.0f);
    CHECK(input::mouseDeltaX() == 10.0f);
    CHECK(input::mouseDeltaY() == 20.0f);
    input::update();
    CHECK(!input::isMousePressed(MouseButton::Left));
    CHECK(input::isMouseDown(MouseButton::Left));

    input::platformMouseUp(MouseButton::Left);
    CHECK(input::isMouseReleased(MouseButton::Left));
    input::update();
    CHECK(!input::isMouseReleased(MouseButton::Left));

    input::platformMouseScroll(2.0f, -3.0f);
    CHECK(input::mouseScrollX() == 2.0f);
    CHECK(input::mouseScrollY() == -3.0f);
    input::update();
    CHECK(input::mouseScrollX() == 0.0f);
    CHECK(input::mouseScrollY() == 0.0f);
}

void testGamepad() {
    using namespace ion;
    input::update();

    CHECK(!input::isGamepadConnected(0));
    CHECK(!input::isGamepadDown(0, GamepadButton::A));
    CHECK(input::gamepadAxis(0, GamepadAxis::LeftX) == 0.0f);

    input::platformGamepadConnected(0, true);
    CHECK(input::isGamepadConnected(0));

    input::platformGamepadButton(0, GamepadButton::A, true);
    CHECK(input::isGamepadDown(0, GamepadButton::A));
    CHECK(input::isGamepadPressed(0, GamepadButton::A));

    input::platformGamepadAxis(0, GamepadAxis::LeftX, 0.5f);
    input::platformGamepadAxis(0, GamepadAxis::TriggerRight, 0.75f);
    CHECK(input::gamepadAxis(0, GamepadAxis::LeftX) == 0.5f);
    CHECK(input::gamepadAxis(0, GamepadAxis::TriggerRight) == 0.75f);

    input::update();
    CHECK(!input::isGamepadPressed(0, GamepadButton::A));
    CHECK(input::isGamepadDown(0, GamepadButton::A));

    input::platformGamepadButton(0, GamepadButton::A, false);
    CHECK(!input::isGamepadDown(0, GamepadButton::A));
    CHECK(input::isGamepadReleased(0, GamepadButton::A));
    input::update();

    CHECK(!input::isGamepadConnected(5));
    CHECK(!input::isGamepadDown(5, GamepadButton::A));
    CHECK(input::gamepadAxis(5, GamepadAxis::LeftX) == 0.0f);
}

void testNames() {
    using namespace ion;
    CHECK(ion::keyName(Key::Space) == std::string("Space"));
    CHECK(ion::keyName(Key::ArrowLeft) == std::string("ArrowLeft"));
    CHECK(ion::keyName(Key::Unknown) == std::string("Unknown"));
    CHECK(ion::keyName(static_cast<Key>(9999)) == std::string("Unknown"));
    CHECK(ion::mouseButtonName(MouseButton::Right) == std::string("Right"));
    CHECK(ion::gamepadButtonName(GamepadButton::LeftBumper) == std::string("LeftBumper"));
    CHECK(ion::gamepadAxisName(GamepadAxis::TriggerLeft) == std::string("TriggerLeft"));
}

void testActionMap() {
    using namespace ion;
    input::update();

    input::ActionMap map;
    CHECK(!map.isDown("jump"));
    CHECK(!map.isPressed("jump"));
    CHECK(!map.hasAction("jump"));

    map.bind("jump", Key::Space);
    map.bind("jump", MouseButton::Middle);
    map.bind("fire", MouseButton::Left);
    CHECK(map.hasAction("jump"));
    CHECK(map.hasAction("fire"));

    input::platformKeyDown(Key::Space);
    CHECK(map.isDown("jump"));
    CHECK(map.isPressed("jump"));
    CHECK(!map.isDown("fire"));

    input::update();
    CHECK(map.isDown("jump"));
    CHECK(!map.isPressed("jump"));

    map.unbind("jump");
    CHECK(!map.hasAction("jump"));
    CHECK(!map.isDown("jump"));

    input::platformMouseDown(MouseButton::Left);
    CHECK(map.isDown("fire"));
    CHECK(map.isPressed("fire"));
    input::update();

    map.bind("fire", Key::Escape);
    input::platformKeyDown(Key::Escape);
    CHECK(map.isDown("fire"));
    input::update();

    map.clear();
    CHECK(!map.hasAction("fire"));
    input::update();
}

void testKeyNamesRoundTrip() {
    using namespace ion;
    for (int i = 1; i < static_cast<int>(Key::Count); ++i) {
        Key key = static_cast<Key>(i);
        CHECK(keyFromName(keyName(key)) == key);
    }
    CHECK(keyFromName("space") == Key::Space);
    CHECK(keyFromName("LEFTSHIFT") == Key::LeftShift);
    CHECK(keyFromName("not a key") == Key::Unknown);
    CHECK(keyFromName("") == Key::Unknown);
}

void testTextInputAndRelease() {
    using namespace ion;
    input::update();
    input::platformTextInput('h');
    input::platformTextInput('i');
    input::platformTextInput(0x08);    // backspace: control chars are dropped
    input::platformTextInput(0xE9);    // e-acute
    input::platformTextInput(0x1F600); // emoji (4-byte UTF-8)
    CHECK(input::textInput() == "hi\xC3\xA9\xF0\x9F\x98\x80");
    input::update();
    CHECK(input::textInput().empty());

    CHECK(!input::isAnyKeyPressed());
    input::platformKeyDown(Key::W);
    input::platformMouseDown(MouseButton::Left);
    CHECK(input::isAnyKeyPressed());
    input::update();
    CHECK(!input::isAnyKeyPressed()); // edge only
    input::platformReleaseAll();
    CHECK(!input::isKeyDown(Key::W));
    CHECK(input::isKeyReleased(Key::W));
    CHECK(!input::isMouseDown(MouseButton::Left));
    CHECK(input::isMouseReleased(MouseButton::Left));
    input::update();

    input::platformMouseMove(10.0f, 20.0f);
    input::update();
    input::platformMouseMove(13.0f, 16.0f);
    CHECK(input::mousePosition() == Vector2(13.0f, 16.0f));
    CHECK(input::mouseDelta() == Vector2(3.0f, -4.0f));
    input::update();
}

void testGamepadExtras() {
    using namespace ion;
    input::update();
    CHECK(input::firstConnectedGamepad() == -1 || input::isGamepadConnected(0));
    input::platformGamepadConnected(2, true);
    CHECK(input::firstConnectedGamepad() == 2 || input::firstConnectedGamepad() < 2);

    // Radial dead zone.
    input::platformGamepadAxis(2, GamepadAxis::LeftX, 0.1f);
    input::platformGamepadAxis(2, GamepadAxis::LeftY, 0.0f);
    CHECK(input::gamepadStick(2, true).isZero());
    input::platformGamepadAxis(2, GamepadAxis::LeftX, 1.0f);
    Vector2 full = input::gamepadStick(2, true);
    CHECK(std::fabs(full.x - 1.0f) < 1e-5f && full.y == 0.0f);
    input::platformGamepadAxis(2, GamepadAxis::LeftX, 0.575f); // halfway past 0.15
    CHECK(std::fabs(input::gamepadStick(2, true).x - 0.5f) < 1e-4f);
    input::platformGamepadAxis(2, GamepadAxis::LeftX, 0.0f);
    input::platformGamepadAxis(2, GamepadAxis::LeftY, -0.8f);
    CHECK(input::gamepadStick(2, true).y < 0.0f);

    // Unplugging releases buttons and centers axes.
    input::platformGamepadButton(2, GamepadButton::A, true);
    input::update();
    input::platformGamepadConnected(2, false);
    CHECK(!input::isGamepadDown(2, GamepadButton::A));
    CHECK(input::isGamepadReleased(2, GamepadButton::A));
    CHECK(input::gamepadAxis(2, GamepadAxis::LeftY) == 0.0f);
    input::update();
}

void testActionMapGamepadAndAxes() {
    using namespace ion;
    input::update();
    input::ActionMap map;
    map.bind("jump", GamepadButton::A);            // any pad
    map.bind("pause", GamepadButton::Start, 1);    // pad 1 only
    map.bindAxis("move_x", GamepadAxis::LeftX, 1.0f, 3); // earlier tests leave pad 0 deflected
    map.bindKeys("move_x", Key::A, Key::D);
    map.bindAxis("brake", GamepadAxis::TriggerLeft, 1.0f, 3);

    input::platformGamepadConnected(3, true);
    input::platformGamepadButton(3, GamepadButton::A, true);
    CHECK(map.isDown("jump"));
    CHECK(map.isPressed("jump"));
    CHECK(map.value("jump") == 1.0f);
    input::platformGamepadButton(3, GamepadButton::Start, true);
    CHECK(!map.isDown("pause")); // bound to pad 1, not 3

    CHECK(map.value("move_x") == 0.0f);
    input::platformKeyDown(Key::A);
    CHECK(map.value("move_x") == -1.0f);
    CHECK(map.isDown("move_x"));
    input::platformKeyDown(Key::D);
    CHECK(map.value("move_x") == 0.0f); // both held cancel out
    input::platformKeyUp(Key::A);
    input::platformKeyUp(Key::D);
    input::platformGamepadAxis(3, GamepadAxis::LeftX, -0.3f);
    CHECK(std::fabs(map.value("move_x") + 0.3f) < 1e-6f);
    CHECK(!map.isDown("move_x")); // below the 0.5 press threshold
    map.setPressThreshold(0.2f);
    CHECK(map.isDown("move_x"));

    input::platformGamepadAxis(3, GamepadAxis::TriggerLeft, 0.9f);
    CHECK(std::fabs(map.value("brake") - 0.9f) < 1e-6f);

    input::update();
    input::platformGamepadButton(3, GamepadButton::A, false);
    CHECK(map.isReleased("jump"));
    input::platformGamepadConnected(3, false);
    CHECK(map.value("move_x") == 0.0f);
    input::update();
}

} // namespace

int main() {
    testKeyboard();
    testMouse();
    testGamepad();
    testNames();
    testActionMap();
    testKeyNamesRoundTrip();
    testTextInputAndRelease();
    testGamepadExtras();
    testActionMapGamepadAndAxes();

    if (failures == 0) {
        std::printf("input_test: all checks passed\n");
        return 0;
    }
    std::printf("input_test: %d checks failed\n", failures);
    return 1;
}
