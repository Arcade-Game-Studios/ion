#pragma once

#include <ion/math/MathUtils.hpp>
#include <ion/math/Vector2.hpp>
#include <ion/math/Vector3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ion {

//
// Small, fast, seedable PRNG (PCG32, O'Neill 2014). Deterministic across
// platforms and compilers for a given seed, unlike std::uniform_*
// distributions, so it is suitable for replays and procedural generation.
//
class Random {
public:
    explicit Random(uint64_t seed = 0x853c49e6748fea9bull, uint64_t stream = 0xda3e39cb94b95bdbull) {
        setSeed(seed, stream);
    }

    void setSeed(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbull) {
        state_ = 0;
        inc_ = (stream << 1u) | 1u;
        nextU32();
        state_ += seed;
        nextU32();
    }

    uint32_t nextU32() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

    // Uniform in [0, bound) without modulo bias. Returns 0 for bound 0.
    uint32_t nextBelow(uint32_t bound) {
        if (bound == 0) {
            return 0;
        }
        uint32_t threshold = (0u - bound) % bound;
        for (;;) {
            uint32_t r = nextU32();
            if (r >= threshold) {
                return r % bound;
            }
        }
    }

    // Uniform integer in [lo, hi] (inclusive). Arguments may be in any order.
    int range(int lo, int hi) {
        if (lo > hi) {
            int t = lo;
            lo = hi;
            hi = t;
        }
        uint32_t span = (uint32_t)((int64_t)hi - (int64_t)lo + 1);
        if (span == 0) { // full 32-bit range
            return (int)nextU32();
        }
        return (int)((int64_t)lo + (int64_t)nextBelow(span));
    }

    // Uniform float in [0, 1).
    float nextFloat() {
        return (float)(nextU32() >> 8) * (1.0f / 16777216.0f);
    }

    // Uniform float in [lo, hi).
    float range(float lo, float hi) {
        return lo + (hi - lo) * nextFloat();
    }

    // True with the given probability (0..1).
    bool chance(float probability) {
        return nextFloat() < probability;
    }

    // -1 or +1.
    float sign() {
        return (nextU32() & 1u) ? 1.0f : -1.0f;
    }

    // Uniform angle in [0, TAU).
    float angle() {
        return nextFloat() * TAU;
    }

    // Normal distribution (Box-Muller).
    float gaussian(float mean = 0.0f, float stddev = 1.0f) {
        float u1 = nextFloat();
        float u2 = nextFloat();
        if (u1 < 1e-7f) {
            u1 = 1e-7f;
        }
        float mag = std::sqrt(-2.0f * std::log(u1));
        return mean + stddev * mag * std::cos(TAU * u2);
    }

    Vector2 unitVector2() {
        return Vector2::fromAngle(angle());
    }

    // Uniform point inside the unit disc.
    Vector2 insideUnitCircle() {
        float r = std::sqrt(nextFloat());
        return Vector2::fromAngle(angle(), r);
    }

    // Uniform direction on the unit sphere.
    Vector3 unitVector3() {
        float z = range(-1.0f, 1.0f);
        float a = angle();
        float r = std::sqrt(1.0f - z * z);
        return Vector3(r * std::cos(a), r * std::sin(a), z);
    }

    // Uniform point inside the unit ball.
    Vector3 insideUnitSphere() {
        return unitVector3() * std::cbrt(nextFloat());
    }

    // Uniformly random element; `items` must not be empty.
    template <typename T>
    const T& pick(const std::vector<T>& items) {
        return items[nextBelow((uint32_t)items.size())];
    }

    // Index chosen with probability proportional to weights[i]. Returns
    // count when all weights are zero or count is zero.
    size_t weightedIndex(const float* weights, size_t count) {
        float total = 0.0f;
        for (size_t i = 0; i < count; i++) {
            total += weights[i] > 0.0f ? weights[i] : 0.0f;
        }
        if (total <= 0.0f) {
            return count;
        }
        float r = nextFloat() * total;
        for (size_t i = 0; i < count; i++) {
            float w = weights[i] > 0.0f ? weights[i] : 0.0f;
            if (r < w) {
                return i;
            }
            r -= w;
        }
        return count - 1;
    }

    // Fisher-Yates shuffle.
    template <typename T>
    void shuffle(std::vector<T>& items) {
        for (size_t i = items.size(); i > 1; i--) {
            size_t j = nextBelow((uint32_t)i);
            T tmp = std::move(items[i - 1]);
            items[i - 1] = std::move(items[j]);
            items[j] = std::move(tmp);
        }
    }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

} // namespace ion
