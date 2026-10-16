// Image decoder/encoder tests. Fixtures come from
// scripts/gen_image_fixtures.py (tests run from the source directory).

#include <ion/render/Image.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            failures++;                                                              \
        }                                                                            \
    } while (0)

static const std::string kDir = "tests/data/images/";

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

static void testFixtures() {
    std::ifstream manifest(kDir + "MANIFEST");
    CHECK(manifest.good());
    uint32_t expectW = 0, expectH = 0;
    manifest >> expectW >> expectH;
    std::string name;
    int count = 0;
    while (manifest >> name) {
        count++;
        std::string stem = name.substr(0, name.find('.'));
        std::vector<uint8_t> expected = readFile(kDir + stem + ".rgba");
        uint32_t w = 0, h = 0;
        std::vector<uint8_t> pixels;
        bool ok = ion::loadImage(kDir + name, w, h, pixels);
        if (!ok || w != expectW || h != expectH || pixels != expected) {
            size_t firstDiff = 0;
            while (firstDiff < pixels.size() && firstDiff < expected.size() &&
                   pixels[firstDiff] == expected[firstDiff]) {
                firstDiff++;
            }
            std::printf("FAIL fixture %s (ok=%d %ux%u, first differing byte %zu)\n",
                        name.c_str(), ok, w, h, firstDiff);
            failures++;
        }
    }
    CHECK(count >= 40);
}

static void testEncodeRoundTrip() {
    const uint32_t w = 37, h = 19;
    std::vector<uint8_t> pixels(w * h * 4);
    for (size_t i = 0; i < pixels.size(); i++) {
        pixels[i] = (uint8_t)((i * 31) ^ (i >> 3));
    }
    std::vector<uint8_t> png = ion::encodePng(w, h, pixels.data());
    CHECK(!png.empty());
    uint32_t dw = 0, dh = 0;
    std::vector<uint8_t> decoded;
    CHECK(ion::loadImageFromMemory(png.data(), png.size(), dw, dh, decoded));
    CHECK(dw == w && dh == h);
    CHECK(decoded == pixels);

    const char* path = "/tmp/ion_image_test.png";
    CHECK(ion::savePng(path, w, h, pixels.data()));
    CHECK(ion::loadImage(path, dw, dh, decoded) && decoded == pixels);
    std::remove(path);

    CHECK(ion::encodePng(0, 4, pixels.data()).empty());
    CHECK(ion::encodePng(4, 4, nullptr).empty());
    CHECK(!ion::savePng("/nonexistent/dir/out.png", w, h, pixels.data()));
}

static void testHelpers() {
    uint8_t px[2 * 2 * 4] = {1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4};
    ion::flipImageVertically(2, 2, px);
    CHECK(px[0] == 3 && px[4] == 4 && px[8] == 1 && px[12] == 2);

    uint8_t color[8] = {255, 128, 0, 128, 200, 200, 200, 0};
    ion::premultiplyAlpha(2, 1, color);
    CHECK(color[0] == 128 && color[1] == 64 && color[2] == 0 && color[3] == 128);
    CHECK(color[4] == 0 && color[7] == 0);
}

// Every prefix and a sweep of single-byte corruptions must fail cleanly or
// decode, never crash or read out of bounds (run under ASan in CI).
static void testCorruptInput() {
    const char* files[] = {"png_c3_d4_i.png", "png_c6_d16_i.png", "bmp32.bmp",
                           "tga32_rle.tga", "png_c2_d16_trns.png"};
    for (const char* file : files) {
        std::vector<uint8_t> data = readFile(kDir + file);
        CHECK(!data.empty());
        uint32_t w, h;
        std::vector<uint8_t> out;
        for (size_t len = 0; len < data.size(); len += 7) {
            ion::loadImageFromMemory(data.data(), len, w, h, out);
        }
        for (size_t i = 0; i < data.size(); i += 3) {
            std::vector<uint8_t> broken = data;
            broken[i] ^= 0xA5;
            ion::loadImageFromMemory(broken.data(), broken.size(), w, h, out);
        }
    }
    uint32_t w, h;
    std::vector<uint8_t> out;
    const uint8_t junk[] = {0xFF, 0xD8, 0xFF, 0xE0, 0, 0x10, 'J', 'F', 'I', 'F', 0, 1,
                            1, 0, 0, 1, 0, 1, 0, 0}; // JPEG header: unsupported
    CHECK(!ion::loadImageFromMemory(junk, sizeof(junk), w, h, out));
    CHECK(!ion::loadImageFromMemory(nullptr, 10, w, h, out));
    CHECK(!ion::loadImage(kDir + "does_not_exist.png", w, h, out));

    // A PNG header claiming an enormous size is rejected before allocating.
    std::vector<uint8_t> png = ion::encodePng(1, 1, (const uint8_t*)"\0\0\0\0");
    png[16] = 0x7F; // width high byte
    CHECK(!ion::loadImageFromMemory(png.data(), png.size(), w, h, out));
}

int main() {
    testFixtures();
    testEncodeRoundTrip();
    testHelpers();
    testCorruptInput();
    if (failures == 0) {
        std::printf("All image tests passed\n");
        return 0;
    }
    std::printf("%d image test(s) FAILED\n", failures);
    return 1;
}
