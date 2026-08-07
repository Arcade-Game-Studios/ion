#include <ion/core/Config.hpp>
#include <ion/core/Error.hpp>
#include <ion/core/Log.hpp>
#include <ion/core/Memory.hpp>
#include <ion/core/Timer.hpp>
#include <ion/math/Matrix4.hpp>
#include <ion/math/Vector3.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static int failures = 0;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            failures++;                                                              \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
    do {                                                                             \
        float _a = (a);                                                              \
        float _b = (b);                                                              \
        if (_a < _b - (eps) || _a > _b + (eps)) {                                    \
            std::printf("FAIL %s:%d: %s = %f, expected %f (+/-%f)\n",                \
                        __FILE__, __LINE__, #a, _a, _b, (float)(eps));               \
            failures++;                                                              \
        }                                                                            \
    } while (0)

static void testTimer() {
    ion::Timer timer;
    timer.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    float delta = timer.tick();
    CHECK(delta >= 0.015f);
    CHECK(delta < 1.0f);

    double elapsed = timer.elapsedSeconds();
    CHECK(elapsed >= 0.0);
    CHECK(elapsed < 10.0);
}

static void testConfig() {
    ion::Config config;
    config.set("width", 1280);
    config.set("height", 720);
    config.set("fullscreen", false);
    config.set("vsync", true);
    config.set("title", "Ion Test");
    config.set("sensitivity", 0.5f);

    CHECK(config.getInt("width") == 1280);
    CHECK(config.getInt("height") == 720);
    CHECK(config.getBool("fullscreen") == false);
    CHECK(config.getBool("vsync") == true);
    CHECK(config.getString("title") == "Ion Test");
    CHECK(config.getFloat("sensitivity") == 0.5f);
    CHECK(config.getInt("missing", 42) == 42);
    CHECK(config.has("width"));
    CHECK(!config.has("missing"));

    const char* path = "/tmp/ion_config_test.cfg";
    CHECK(config.save(path));
    ion::Config loaded;
    CHECK(loaded.load(path));
    CHECK(loaded.getInt("width") == 1280);
    CHECK(loaded.getString("title") == "Ion Test");
    CHECK(loaded.getBool("vsync") == true);
    std::remove(path);
}

static void testError() {
    ion::Result<int> ok(7);
    CHECK(ok.ok());
    CHECK(ok.value() == 7);

    ion::Result<int> err(ion::makeError(ion::ErrorCode::NotFound, "missing thing"));
    CHECK(!err.ok());
    CHECK(err.error().code == ion::ErrorCode::NotFound);
    CHECK(err.error().message == "missing thing");
}

static void testMemory() {
    CHECK(ion::isPowerOfTwo(1));
    CHECK(ion::isPowerOfTwo(1024));
    CHECK(!ion::isPowerOfTwo(1000));
    CHECK(ion::alignUp(13, 16) == 16);
    CHECK(ion::alignUp(16, 16) == 16);
    CHECK(ion::alignDown(17, 16) == 16);

    size_t startAlloc = ion::Memory::allocationCount();
    size_t startBytes = ion::Memory::allocatedBytes();

    void* ptr = ion::Memory::allocate(64);
    CHECK(ptr != nullptr);
    CHECK(ion::Memory::allocationCount() == startAlloc + 1);
    CHECK(ion::Memory::allocatedBytes() == startBytes + 64);

    void* grown = ion::Memory::reallocate(ptr, 128);
    CHECK(grown != nullptr);
    CHECK(ion::Memory::allocatedBytes() == startBytes + 128);

    ion::Memory::free(grown);
    CHECK(ion::Memory::allocationCount() == startAlloc);
    CHECK(ion::Memory::allocatedBytes() == startBytes);
}

static void testMath() {
    ion::Vector3 a(1.0f, 2.0f, 3.0f);
    ion::Vector3 b(4.0f, 5.0f, 6.0f);
    CHECK(a + b == ion::Vector3(5.0f, 7.0f, 9.0f));
    CHECK_NEAR(a.length(), 3.741657f, 0.0001f);

    ion::Vector3 cross = ion::Vector3::cross(a, b);
    CHECK(cross == ion::Vector3(-3.0f, 6.0f, -3.0f));

    ion::Matrix4 id = ion::Matrix4::identity();
    ion::Vector3 v = id * a;
    CHECK(v == a);

    ion::Matrix4 m = ion::Matrix4::translation(ion::Vector3(1.0f, 2.0f, 3.0f));
    ion::Vector3 translated = m * a;
    CHECK(translated == ion::Vector3(2.0f, 4.0f, 6.0f));
}

static void testLog() {
    ion::LogLevel old = ion::logLevel();
    ion::setLogLevel(ion::LogLevel::Trace);
    ion::log(ion::LogLevel::Info, "log test %d %s", 42, "ok");
    ion::log(ion::LogLevel::Warn, "warning test");
    ION_LOG_INFO("macro info %d", 1);
    ion::setLogLevel(old);
}

int main() {
    testTimer();
    testConfig();
    testError();
    testMemory();
    testMath();
    testLog();

    if (failures == 0) {
        std::printf("All core tests passed\n");
        return 0;
    }
    std::printf("%d core test(s) FAILED\n", failures);
    return 1;
}
