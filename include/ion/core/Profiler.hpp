#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace ion {

//
// Lightweight CPU profiler for named sections:
//
//   void Game::update(float dt) {
//       ION_PROFILE_SCOPE("update");
//       { ION_PROFILE_SCOPE("physics"); physics.step(dt); }
//   }
//   ...
//   Profiler::endFrame();                   // once per frame
//   std::string text = Profiler::report();  // e.g. into PerformanceOverlay
//
// Section names must be string literals (or otherwise outlive the
// profiler). Main-thread only. Compile with ION_DISABLE_PROFILER to make
// ION_PROFILE_SCOPE a no-op.
//
class Profiler {
public:
    struct Section {
        const char* name = nullptr;
        double lastFrameMs = 0.0; // total time in the last completed frame
        double averageMs = 0.0;   // exponential moving average per frame
        double maxMs = 0.0;       // worst frame since reset()
        uint32_t lastFrameCalls = 0;
        // Accumulated during the current frame.
        double currentMs = 0.0;
        uint32_t currentCalls = 0;
    };

    // Adds a timing sample to a section.
    static void record(const char* name, double milliseconds);

    // Closes the current frame: publishes per-frame totals and starts a new
    // frame.
    static void endFrame();

    static const std::vector<Section>& sections();
    static const Section* find(const char* name);
    static uint64_t frameCount();

    // Multi-line "name  avg  last  max  calls" table, slowest first.
    static std::string report();

    static void reset();

    static void setEnabled(bool enabled);
    static bool isEnabled();
};

// Times the enclosing scope.
class ProfileScope {
public:
    explicit ProfileScope(const char* name)
        : name_(name), start_(std::chrono::steady_clock::now()) {}
    ~ProfileScope() {
        auto end = std::chrono::steady_clock::now();
        Profiler::record(name_, std::chrono::duration<double, std::milli>(end - start_).count());
    }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    const char* name_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace ion

#define ION_PROFILE_CONCAT_INNER(a, b) a##b
#define ION_PROFILE_CONCAT(a, b) ION_PROFILE_CONCAT_INNER(a, b)
#if defined(ION_DISABLE_PROFILER)
#define ION_PROFILE_SCOPE(name) ((void)0)
#else
#define ION_PROFILE_SCOPE(name) \
    ::ion::ProfileScope ION_PROFILE_CONCAT(ionProfileScope_, __LINE__)(name)
#endif
