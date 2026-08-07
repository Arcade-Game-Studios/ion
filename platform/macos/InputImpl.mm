//
// Ion Engine
// macOS Input Implementation (Gamepad via GameController, haptics via CoreHaptics)
//
#include <ion/platform/Input.hpp>

#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>

#include <cmath>

namespace ion {
namespace input {

namespace {

struct MacPadState {
    CHHapticEngine* hapticEngine = nil;
    id<CHHapticAdvancedPatternPlayer> hapticPlayer = nil;
    float lastIntensity = -1.0f;
};

MacPadState g_state[MAX_GAMEPADS];

void stopVibration(MacPadState& state) {
    if (state.hapticPlayer) {
        [state.hapticPlayer stopAtTime:0 error:nil];
        state.hapticPlayer = nil;
    }
    state.lastIntensity = -1.0f;
}

GCExtendedGamepad* extendedPadForIndex(int index) {
    if (@available(macOS 10.15, *)) {
        NSArray<GCController*>* controllers = [GCController controllers];
        if (index >= 0 && index < (int)controllers.count) {
            return controllers[index].extendedGamepad;
        }
    }
    return nil;
}

} // namespace

void pollGamepads() {
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        GCExtendedGamepad* pad = extendedPadForIndex(i);
        bool connected = pad != nil;
        platformGamepadConnected(i, connected);
        if (!connected) {
            continue;
        }

        platformGamepadButton(i, GamepadButton::A, pad.buttonA.isPressed);
        platformGamepadButton(i, GamepadButton::B, pad.buttonB.isPressed);
        platformGamepadButton(i, GamepadButton::X, pad.buttonX.isPressed);
        platformGamepadButton(i, GamepadButton::Y, pad.buttonY.isPressed);
        platformGamepadButton(i, GamepadButton::LeftBumper, pad.leftShoulder.isPressed);
        platformGamepadButton(i, GamepadButton::RightBumper, pad.rightShoulder.isPressed);
        platformGamepadButton(i, GamepadButton::LeftStick, pad.leftThumbstickButton.isPressed);
        platformGamepadButton(i, GamepadButton::RightStick, pad.rightThumbstickButton.isPressed);
        platformGamepadButton(i, GamepadButton::DPadUp, pad.dpad.up.isPressed);
        platformGamepadButton(i, GamepadButton::DPadDown, pad.dpad.down.isPressed);
        platformGamepadButton(i, GamepadButton::DPadLeft, pad.dpad.left.isPressed);
        platformGamepadButton(i, GamepadButton::DPadRight, pad.dpad.right.isPressed);

        platformGamepadAxis(i, GamepadAxis::LeftX, pad.leftThumbstick.xAxis.value);
        platformGamepadAxis(i, GamepadAxis::LeftY, pad.leftThumbstick.yAxis.value);
        platformGamepadAxis(i, GamepadAxis::RightX, pad.rightThumbstick.xAxis.value);
        platformGamepadAxis(i, GamepadAxis::RightY, pad.rightThumbstick.yAxis.value);
        platformGamepadAxis(i, GamepadAxis::TriggerLeft, pad.leftTrigger.value);
        platformGamepadAxis(i, GamepadAxis::TriggerRight, pad.rightTrigger.value);
    }
}

void gamepadVibrate(int index, float leftMotor, float rightMotor) {
    if (@available(macOS 11.0, *)) {
        if (index < 0 || index >= MAX_GAMEPADS) {
            return;
        }
        float intensity = std::max(0.0f, std::min(1.0f, std::max(leftMotor, rightMotor)));
        MacPadState& state = g_state[index];

        if (intensity <= 0.001f) {
            stopVibration(state);
            return;
        }

        GCController* controller = nil;
        NSArray<GCController*>* controllers = [GCController controllers];
        if (index < (int)controllers.count) {
            controller = controllers[index];
        }
        if (!controller) {
            stopVibration(state);
            return;
        }

        if (std::fabs(intensity - state.lastIntensity) > 0.01f || !state.hapticPlayer) {
            stopVibration(state);

            if (!state.hapticEngine) {
                state.hapticEngine =
                    [controller.haptics createEngineWithLocality:GCHapticsLocalityDefault];
            }
            if (!state.hapticEngine) {
                return;
            }

            NSError* error = nil;
            if (![state.hapticEngine startAndReturnError:&error]) {
                return;
            }

            CHHapticEventParameter* intensityParam = [[CHHapticEventParameter alloc]
                initWithParameterID:CHHapticEventParameterIDHapticIntensity
                              value:intensity];
            CHHapticEvent* event = [[CHHapticEvent alloc]
                initWithEventType:CHHapticEventTypeHapticContinuous
                       parameters:@[ intensityParam ]
                     relativeTime:0
                         duration:GCHapticDurationInfinite];
            CHHapticPattern* pattern = [[CHHapticPattern alloc] initWithEvents:@[ event ]
                                                                    parameters:@[]
                                                                         error:&error];
            if (!pattern) {
                return;
            }
            state.hapticPlayer =
                [state.hapticEngine createAdvancedPlayerWithPattern:pattern error:&error];
            if (!state.hapticPlayer) {
                return;
            }
            if (![state.hapticPlayer startAtTime:0 error:&error]) {
                stopVibration(state);
                return;
            }
            state.lastIntensity = intensity;
        }
    }
}

} // namespace input
} // namespace ion
