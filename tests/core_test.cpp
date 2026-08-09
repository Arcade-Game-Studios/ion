#include <ion/core/Config.hpp>
#include <ion/core/Error.hpp>
#include <ion/core/Log.hpp>
#include <ion/core/Memory.hpp>
#include <ion/core/Timer.hpp>
#include <ion/math/Matrix4.hpp>
#include <ion/math/Vector3.hpp>
#include <ion/render/Svg.hpp>

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

static uint8_t pixelR(const std::vector<uint8_t>& p, uint32_t w, uint32_t x,
                      uint32_t y) {
    return p[((size_t)y * w + x) * 4 + 0];
}

static uint8_t pixelA(const std::vector<uint8_t>& p, uint32_t w, uint32_t x,
                      uint32_t y) {
    return p[((size_t)y * w + x) * 4 + 3];
}

static void testSvgFillRect() {
    ion::SvgImage image;
    CHECK(image.parse(
        "<svg width=\"32\" height=\"32\">"
        "<rect x=\"8\" y=\"8\" width=\"16\" height=\"16\" fill=\"#ff0000\"/>"
        "</svg>"));
    CHECK(image.isValid());
    CHECK(image.width() == 32);
    CHECK(image.height() == 32);

    std::vector<uint8_t> px = image.rasterize();
    CHECK(px.size() == (size_t)32 * 32 * 4);
    // Inside the rect: opaque red.
    CHECK(pixelR(px, 32, 16, 16) == 255);
    CHECK(pixelA(px, 32, 16, 16) == 255);
    CHECK(px[((size_t)16 * 32 + 16) * 4 + 1] == 0);
    CHECK(px[((size_t)16 * 32 + 16) * 4 + 2] == 0);
    // Outside the rect: transparent.
    CHECK(pixelA(px, 32, 2, 2) == 0);
    CHECK(pixelA(px, 32, 30, 30) == 0);
}

static void testSvgCircleStroke() {
    ion::SvgImage image;
    CHECK(image.parse(
        "<svg width=\"40\" height=\"40\">"
        "<circle cx=\"20\" cy=\"20\" r=\"10\" fill=\"none\" "
        "stroke=\"#00ff00\" stroke-width=\"4\"/>"
        "</svg>"));
    std::vector<uint8_t> px = image.rasterize();
    // Center is hollow (no fill).
    CHECK(pixelA(px, 40, 20, 20) == 0);
    // Stroke band is present (circle r=10 centered at (20,20), width 4
    // spans radius 8..12, so (20,10) is inside the band).
    CHECK(pixelA(px, 40, 20, 10) == 255);
    CHECK(pixelR(px, 40, 20, 10) == 0);
    CHECK(pixelA(px, 40, 20, 3) == 0);
}

static void testSvgPathAndDefaults() {
    // A triangle with no paint attributes defaults to black fill; the second
    // path is offset by a group transform.
    ion::SvgImage image;
    CHECK(image.parse(
        "<svg width=\"48\" height=\"24\">"
        "<path d=\"M4 20 L14 4 L24 20 Z\"/>"
        "<g transform=\"translate(24 0)\">"
        "<path d=\"M4 20 L14 4 L24 20 Z\" fill=\"blue\"/>"
        "</g>"
        "</svg>"));
    std::vector<uint8_t> px = image.rasterize();
    CHECK(pixelA(px, 48, 10, 14) > 200);
    CHECK(pixelR(px, 48, 10, 14) == 0); // default black fill
    CHECK(pixelA(px, 48, 34, 14) > 200);
    CHECK(px[((size_t)14 * 48 + 34) * 4 + 2] == 255); // blue
}

static void testSvgBezierAndArc() {
    ion::SvgImage image;
    CHECK(image.parse(
        "<svg width=\"64\" height=\"64\">"
        "<path d=\"M8 32 C 8 8 56 8 56 32\" "
        "fill=\"none\" stroke=\"magenta\" stroke-width=\"2\"/>"
        "<path d=\"M 4 58 A 24 24 0 0 1 52 58\" fill=\"none\" "
        "stroke=\"cyan\" stroke-width=\"2\"/>"
        "</svg>"));
    std::vector<uint8_t> px = image.rasterize();
    // Cubic midpoint (t=0.5) is at (32, 14).
    CHECK(pixelA(px, 64, 32, 14) > 150);
    // The arc from (4,58) to (52,58) bulges up through (28, 34): center is
    // (28,58) with radius 24.
    CHECK(pixelA(px, 64, 28, 34) > 150);
}

static void testSvgMalformed() {
    ion::SvgImage image;
    CHECK(!image.parse("<svg width=\"8\"><rect"));
    CHECK(!image.parse(""));
    CHECK(!image.parse("<g></g>"));
    ion::SvgImage empty;
    CHECK(!empty.isValid());
    CHECK(empty.rasterize().empty());
}

static void testSvgRasterizeHelper() {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px = ion::rasterizeSvg(
        "<svg width=\"16\" height=\"16\">"
        "<circle cx=\"8\" cy=\"8\" r=\"6\" fill=\"#336699\"/>"
        "</svg>",
        w, h);
    CHECK(w == 16);
    CHECK(h == 16);
    CHECK(px.size() == (size_t)16 * 16 * 4);
    CHECK(pixelA(px, 16, 8, 8) > 200);
    CHECK(px[((size_t)8 * 16 + 8) * 4 + 0] == 0x33);
    CHECK(px[((size_t)8 * 16 + 8) * 4 + 1] == 0x66);
    CHECK(px[((size_t)8 * 16 + 8) * 4 + 2] == 0x99);
}

static void testSvgFile() {
    const char* path = "/tmp/ion_svg_test.svg";
    FILE* f = std::fopen(path, "wb");
    CHECK(f != nullptr);
    if (f) {
        std::fputs("<svg width=\"8\" height=\"8\">"
                   "<rect width=\"8\" height=\"8\" fill=\"green\"/>"
                   "</svg>",
                   f);
        std::fclose(f);
    }
    ion::SvgImage image;
    CHECK(image.parseFile(path));
    CHECK(image.width() == 8);
    std::vector<uint8_t> px = image.rasterize();
    CHECK(pixelA(px, 8, 4, 4) == 255);
    std::remove(path);
    ion::SvgImage missing;
    CHECK(!missing.parseFile("/tmp/ion_svg_does_not_exist.svg"));
}

int main() {
    testTimer();
    testConfig();
    testError();
    testMemory();
    testMath();
    testLog();
    testSvgFillRect();
    testSvgCircleStroke();
    testSvgPathAndDefaults();
    testSvgBezierAndArc();
    testSvgMalformed();
    testSvgRasterizeHelper();
    testSvgFile();

    if (failures == 0) {
        std::printf("All core tests passed\n");
        return 0;
    }
    std::printf("%d core test(s) FAILED\n", failures);
    return 1;
}
