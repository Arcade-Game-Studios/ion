#include <ion/platform/Input.hpp>

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

} // namespace

int main() {
    testKeyboard();
    testMouse();
    testGamepad();
    testNames();
    testActionMap();

    if (failures == 0) {
        std::printf("input_test: all checks passed\n");
        return 0;
    }
    std::printf("input_test: %d checks failed\n", failures);
    return 1;
}
