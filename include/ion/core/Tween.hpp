#pragma once

#include <ion/math/Quaternion.hpp>
#include <ion/math/Vector2.hpp>
#include <ion/math/Vector3.hpp>
#include <ion/math/Vector4.hpp>
#include <ion/render/Color.hpp>

#include <cstdint>
#include <functional>

namespace ion {

//
// Easing curves (Robert Penner's set). ease(curve, t) maps t in [0, 1] to
// eased progress; ease(c, 0) == 0 and ease(c, 1) == 1 for every curve.
// Back and Elastic overshoot outside [0, 1] in between, by design.
//
enum class Ease : uint8_t {
    Linear,
    QuadIn, QuadOut, QuadInOut,
    CubicIn, CubicOut, CubicInOut,
    QuartIn, QuartOut, QuartInOut,
    QuintIn, QuintOut, QuintInOut,
    SineIn, SineOut, SineInOut,
    ExpoIn, ExpoOut, ExpoInOut,
    CircIn, CircOut, CircInOut,
    BackIn, BackOut, BackInOut,
    ElasticIn, ElasticOut, ElasticInOut,
    BounceIn, BounceOut, BounceInOut,
    Count,
};

float ease(Ease curve, float t);
const char* easeName(Ease curve);

// Interpolation used by Tween<T>; overload for your own types.
inline float tweenLerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vector2 tweenLerp(const Vector2& a, const Vector2& b, float t) { return Vector2::lerp(a, b, t); }
inline Vector3 tweenLerp(const Vector3& a, const Vector3& b, float t) { return Vector3::lerp(a, b, t); }
inline Vector4 tweenLerp(const Vector4& a, const Vector4& b, float t) { return Vector4::lerp(a, b, t); }
inline Color tweenLerp(const Color& a, const Color& b, float t) { return Color::lerp(a, b, t); }
inline Quaternion tweenLerp(const Quaternion& a, const Quaternion& b, float t) {
    return Quaternion::slerp(a, b, t);
}

//
// Animates a value from `from` to `to` over `duration` seconds:
//
//   Tween<Vector2> slide(start, end, 0.4f, Ease::BackOut);
//   slide.setRepeat(-1).setYoyo(true);      // ping-pong forever
//   ...
//   sprite.position = slide.update(dt);
//
template <typename T>
class Tween {
public:
    Tween() = default;
    Tween(T from, T to, float duration, Ease curve = Ease::Linear)
        : from_(from), to_(to), duration_(duration > 0.0f ? duration : 0.0f),
          curve_(curve), value_(from) {}

    // Seconds to wait before starting.
    Tween& setDelay(float seconds) {
        delay_ = seconds > 0.0f ? seconds : 0.0f;
        return *this;
    }
    // Extra plays after the first; -1 repeats forever.
    Tween& setRepeat(int count) {
        repeat_ = count;
        return *this;
    }
    // Alternate direction on each repeat.
    Tween& setYoyo(bool yoyo) {
        yoyo_ = yoyo;
        return *this;
    }
    Tween& onComplete(std::function<void()> callback) {
        onComplete_ = std::move(callback);
        return *this;
    }

    // Advances and returns the current value.
    const T& update(float deltaTime) {
        if (finished_ || paused_) {
            return value_;
        }
        if (delay_ > 0.0f) {
            delay_ -= deltaTime;
            if (delay_ > 0.0f) {
                return value_;
            }
            deltaTime = -delay_; // carry the remainder into the tween
            delay_ = 0.0f;
        }
        elapsed_ += deltaTime;
        if (duration_ <= 0.0f) {
            elapsed_ = 0.0f;
            value_ = reversed_ ? from_ : to_;
            finish_();
            return value_;
        }
        while (elapsed_ >= duration_) {
            if (repeat_ == 0) {
                elapsed_ = duration_;
                break;
            }
            elapsed_ -= duration_;
            if (repeat_ > 0) {
                repeat_--;
            }
            if (yoyo_) {
                reversed_ = !reversed_;
            }
        }
        float t = elapsed_ / duration_;
        float eased = ease(curve_, reversed_ ? 1.0f - t : t);
        value_ = tweenLerp(from_, to_, eased);
        if (repeat_ == 0 && elapsed_ >= duration_) {
            finish_();
        }
        return value_;
    }

    const T& value() const { return value_; }
    bool isFinished() const { return finished_; }
    // Progress through the current play, 0..1.
    float progress() const { return duration_ > 0.0f ? elapsed_ / duration_ : 1.0f; }

    void pause() { paused_ = true; }
    void resume() { paused_ = false; }
    bool isPaused() const { return paused_; }

    // Jumps to the end (firing onComplete once).
    void complete() {
        if (!finished_) {
            value_ = reversed_ ? from_ : to_;
            elapsed_ = duration_;
            finish_();
        }
    }

private:
    void finish_() {
        if (!finished_) {
            finished_ = true;
            if (onComplete_) {
                onComplete_();
            }
        }
    }

    T from_{};
    T to_{};
    float duration_ = 0.0f;
    Ease curve_ = Ease::Linear;
    T value_{};
    float elapsed_ = 0.0f;
    float delay_ = 0.0f;
    int repeat_ = 0;
    bool yoyo_ = false;
    bool reversed_ = false;
    bool finished_ = false;
    bool paused_ = false;
    std::function<void()> onComplete_;
};

} // namespace ion
