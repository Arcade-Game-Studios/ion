#include <ion/render/Image.hpp>
#include <ion/render/Model.hpp>
#include <ion/render/Renderer.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <zlib.h>

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

static std::string tempDir() {
    return "/tmp/ion_model_test";
}

static void writeFile(const std::string& path, const void* data, size_t size) {
    std::ofstream file(path, std::ios::binary);
    file.write((const char*)data, (std::streamsize)size);
}

static void writeString(const std::string& path, const std::string& text) {
    writeFile(path, text.data(), text.size());
}

static void writeU32(std::ofstream& file, uint32_t value) {
    char bytes[4];
    bytes[0] = (char)((value >> 24) & 0xFF);
    bytes[1] = (char)((value >> 16) & 0xFF);
    bytes[2] = (char)((value >> 8) & 0xFF);
    bytes[3] = (char)(value & 0xFF);
    file.write(bytes, 4);
}

static void writeChunk(std::ofstream& file, const char type[4],
                       const std::vector<uint8_t>& data) {
    writeU32(file, (uint32_t)data.size());
    file.write(type, 4);
    file.write((const char*)data.data(), (std::streamsize)data.size());
    uint32_t crc = (uint32_t)crc32(0L, Z_NULL, 0);
    crc = (uint32_t)crc32(crc, (const Bytef*)type, 4);
    crc = (uint32_t)crc32(crc, (const Bytef*)data.data(),
                          (uInt)data.size());
    writeU32(file, crc);
}

static void encodePng(const std::string& path, uint32_t width,
                      uint32_t height, uint8_t colorType,
                      const std::vector<uint8_t>& raw,
                      const uint8_t* palette, size_t paletteBytes,
                      const uint8_t* trns, size_t trnsBytes) {
    std::ofstream file(path, std::ios::binary);
    static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    file.write((const char*)signature, 8);

    std::vector<uint8_t> ihdr;
    auto pushU32 = [&ihdr](uint32_t value) {
        ihdr.push_back((uint8_t)((value >> 24) & 0xFF));
        ihdr.push_back((uint8_t)((value >> 16) & 0xFF));
        ihdr.push_back((uint8_t)((value >> 8) & 0xFF));
        ihdr.push_back((uint8_t)(value & 0xFF));
    };
    pushU32(width);
    pushU32(height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(colorType);
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // interlace
    writeChunk(file, "IHDR", ihdr);

    if (palette && paletteBytes > 0) {
        std::vector<uint8_t> plte(palette, palette + paletteBytes);
        writeChunk(file, "PLTE", plte);
    }
    if (trns && trnsBytes > 0) {
        std::vector<uint8_t> trnsData(trns, trns + trnsBytes);
        writeChunk(file, "tRNS", trnsData);
    }

    uLongf compressedSize = compressBound((uLong)raw.size());
    std::vector<uint8_t> compressed(compressedSize);
    compress2(compressed.data(), &compressedSize, raw.data(), (uLong)raw.size(),
              6);
    compressed.resize(compressedSize);
    writeChunk(file, "IDAT", compressed);
    writeChunk(file, "IEND", {});
}

static void ensureTempDir() {
    system("mkdir -p /tmp/ion_model_test");
}

static void testPngRgba() {
    // 2x2 RGBA PNG with distinct pixels.
    std::vector<uint8_t> raw;
    auto row = [&raw](const std::vector<uint8_t>& pixels) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), pixels.begin(), pixels.end());
    };
    row({255, 0, 0, 255, 0, 255, 0, 255});          // (0,0) red, (1,0) green
    row({0, 0, 255, 255, 255, 255, 255, 128});      // (0,1) blue, (1,1) alpha
    encodePng(tempDir() + "/rgba.png", 2, 2, 6, raw, nullptr, 0, nullptr, 0);

    uint32_t w = 0, h = 0;
    std::vector<uint8_t> pixels;
    CHECK(ion::loadImage(tempDir() + "/rgba.png", w, h, pixels));
    CHECK(w == 2);
    CHECK(h == 2);
    CHECK(pixels.size() == 16);
    CHECK(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0 &&
          pixels[3] == 255);
    CHECK(pixels[4] == 0 && pixels[5] == 255 && pixels[6] == 0);
    CHECK(pixels[12] == 255 && pixels[15] == 128);
}

static void testPngPalette() {
    // 2x2 palette PNG; index 1 is translucent.
    std::vector<uint8_t> raw = {0, 0, 1, 0, 2, 0};
    const uint8_t palette[9] = {255, 0, 0, 0, 255, 0, 0, 0, 255};
    const uint8_t trns[3] = {255, 128, 255};
    encodePng(tempDir() + "/palette.png", 2, 2, 3, raw, palette, 9, trns, 3);

    uint32_t w = 0, h = 0;
    std::vector<uint8_t> pixels;
    CHECK(ion::loadImage(tempDir() + "/palette.png", w, h, pixels));
    CHECK(w == 2 && h == 2);
    CHECK(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0);
    CHECK(pixels[4] == 0 && pixels[5] == 255 && pixels[6] == 0 &&
          pixels[7] == 128);
    CHECK(pixels[8] == 0 && pixels[9] == 0 && pixels[10] == 255);
    CHECK(pixels[12] == 255 && pixels[13] == 0 && pixels[14] == 0);
}

static void testLoadObj() {
    ensureTempDir();
    std::string obj =
        "mtllib cube.mtl\n"
        "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n"
        "v -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
        "usemtl Red\n"
        "f 1 2 3 4\nf 5 6 7 8\nf 1 2 6 5\n"
        "usemtl Blue\n"
        "f 4 3 7 8\nf 1 4 8 5\nf 2 3 7 6\n";
    writeString(tempDir() + "/cube.obj", obj);
    writeString(tempDir() + "/cube.mtl",
                "newmtl Red\nKd 1 0 0\n"
                "newmtl Blue\nKd 0 0 1\nmap_Kd rgba.png\n");

    ion::Renderer renderer;
    ion::RendererConfig config;
    config.backend = ion::RendererBackend::Null;
    CHECK(renderer.initialize(nullptr, config));

    ion::Model model;
    CHECK(ion::loadModel(renderer, tempDir() + "/cube.obj", model));
    CHECK(model.parts.size() == 2);

    const ion::ModelPart& red = model.parts[0];
    CHECK(red.material.name == "Red");
    CHECK(red.material.baseColor.r > 0.9f);
    CHECK(!red.material.texture.isValid());
    CHECK(red.mesh.isValid());
    CHECK(red.mesh.indexCount == 18);
    CHECK(red.mesh.vertexBuffer.size == 18 * 48);

    const ion::ModelPart& blue = model.parts[1];
    CHECK(blue.material.name == "Blue");
    CHECK(blue.material.baseColor.b > 0.9f);
    CHECK(blue.material.texture.isValid());

    model.destroy(renderer);
    renderer.shutdown();
}

static std::string base64Encode(const std::vector<uint8_t>& data) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t a = data[i];
        uint32_t b = (i + 1 < data.size()) ? data[i + 1] : 0;
        uint32_t c = (i + 2 < data.size()) ? data[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out.push_back(alphabet[(triple >> 18) & 63]);
        out.push_back(alphabet[(triple >> 12) & 63]);
        out.push_back((i + 1 < data.size()) ? alphabet[(triple >> 6) & 63]
                                            : '=');
        out.push_back((i + 2 < data.size()) ? alphabet[triple & 63] : '=');
    }
    return out;
}

static std::string triangleBufferBase64() {
    // 3 positions (VEC3 float) + 3 indices (uint16).
    std::vector<uint8_t> data;
    auto pushF32 = [&data](float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        data.push_back((uint8_t)(bits & 0xFF));
        data.push_back((uint8_t)((bits >> 8) & 0xFF));
        data.push_back((uint8_t)((bits >> 16) & 0xFF));
        data.push_back((uint8_t)((bits >> 24) & 0xFF));
    };
    pushF32(0.0f); pushF32(0.0f); pushF32(0.0f);
    pushF32(1.0f); pushF32(0.0f); pushF32(0.0f);
    pushF32(0.0f); pushF32(1.0f); pushF32(0.0f);
    data.push_back(0); data.push_back(0);
    data.push_back(1); data.push_back(0);
    data.push_back(2); data.push_back(0);
    return base64Encode(data);
}

static std::string triangleGltfJson() {
    std::string base64 = triangleBufferBase64();
    std::stringstream ss;
    ss << "{\"asset\":{\"version\":\"2.0\"},"
       << "\"scene\":0,"
       << "\"scenes\":[{\"nodes\":[0]}],"
       << "\"nodes\":[{\"mesh\":0,\"translation\":[1,2,3]}],"
       << "\"materials\":[{\"name\":\"gold\","
       << "\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.5,0.25,1],"
       << "\"metallicFactor\":0.3,\"roughnessFactor\":0.7}}],"
       << "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},"
       << "\"indices\":1,\"material\":0}]}],"
       << "\"accessors\":["
       << "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":"
       << "\"VEC3\"},"
       << "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":"
       << "\"SCALAR\"}],"
       << "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
       << "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
       << "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,"
       << base64 << "\",\"byteLength\":42}]}";
    return ss.str();
}

static void checkTriangleParts(const ion::Model& model) {
    CHECK(model.parts.size() == 1);
    const ion::ModelPart& part = model.parts[0];
    CHECK(part.mesh.isValid());
    CHECK(part.mesh.vertexCount == 3);
    CHECK(part.mesh.indexCount == 3);
    CHECK_NEAR(part.transform(0, 3), 1.0f, 1e-4f);
    CHECK_NEAR(part.transform(1, 3), 2.0f, 1e-4f);
    CHECK_NEAR(part.transform(2, 3), 3.0f, 1e-4f);
    CHECK(part.material.name == "gold");
    CHECK_NEAR(part.material.baseColor.g, 0.5f, 1e-4f);
    CHECK_NEAR(part.material.metallic, 0.3f, 1e-4f);
    CHECK_NEAR(part.material.roughness, 0.7f, 1e-4f);
}

static void testLoadGltf() {
    ensureTempDir();
    writeString(tempDir() + "/triangle.gltf", triangleGltfJson());

    ion::Renderer renderer;
    ion::RendererConfig config;
    config.backend = ion::RendererBackend::Null;
    CHECK(renderer.initialize(nullptr, config));

    ion::Model model;
    CHECK(ion::loadModel(renderer, tempDir() + "/triangle.gltf", model));
    checkTriangleParts(model);
    model.destroy(renderer);
    renderer.shutdown();
}

static void testLoadGlb() {
    ensureTempDir();

    // GLB: header + JSON chunk + BIN chunk (both padded to 4 bytes).
    std::string gltfText =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"mesh\":0}],"
        "\"materials\":[{\"name\":\"gold\","
        "\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.5,0.25,1]}}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},"
        "\"indices\":1,\"material\":0}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"buffers\":[{\"byteLength\":42}]}";

    std::vector<uint8_t> jsonBytes(gltfText.begin(), gltfText.end());
    while (jsonBytes.size() % 4 != 0) {
        jsonBytes.push_back(' ');
    }

    std::vector<uint8_t> binBytes;
    auto pushF32 = [&binBytes](float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        binBytes.push_back((uint8_t)(bits & 0xFF));
        binBytes.push_back((uint8_t)((bits >> 8) & 0xFF));
        binBytes.push_back((uint8_t)((bits >> 16) & 0xFF));
        binBytes.push_back((uint8_t)((bits >> 24) & 0xFF));
    };
    pushF32(0.0f); pushF32(0.0f); pushF32(0.0f);
    pushF32(1.0f); pushF32(0.0f); pushF32(0.0f);
    pushF32(0.0f); pushF32(1.0f); pushF32(0.0f);
    binBytes.push_back(0); binBytes.push_back(0);
    binBytes.push_back(1); binBytes.push_back(0);
    binBytes.push_back(2); binBytes.push_back(0);
    while (binBytes.size() % 4 != 0) {
        binBytes.push_back(0);
    }

    std::ofstream file(tempDir() + "/triangle.glb", std::ios::binary);
    file.write("glTF", 4);
    uint32_t version = 2;
    file.write((const char*)&version, 4);
    uint32_t totalLength =
        12 + 8 + (uint32_t)jsonBytes.size() + 8 + (uint32_t)binBytes.size();
    file.write((const char*)&totalLength, 4);
    uint32_t jsonLength = (uint32_t)jsonBytes.size();
    uint32_t jsonType = 0x4E4F534A;
    file.write((const char*)&jsonLength, 4);
    file.write((const char*)&jsonType, 4);
    file.write((const char*)jsonBytes.data(), (std::streamsize)jsonBytes.size());
    uint32_t binLength = (uint32_t)binBytes.size();
    uint32_t binType = 0x004E4942;
    file.write((const char*)&binLength, 4);
    file.write((const char*)&binType, 4);
    file.write((const char*)binBytes.data(), (std::streamsize)binBytes.size());
    file.close();

    ion::Renderer renderer;
    ion::RendererConfig config;
    config.backend = ion::RendererBackend::Null;
    CHECK(renderer.initialize(nullptr, config));

    ion::Model model;
    CHECK(ion::loadModel(renderer, tempDir() + "/triangle.glb", model));
    CHECK(model.parts.size() == 1);
    CHECK(model.parts[0].mesh.isValid());
    CHECK(model.parts[0].mesh.vertexCount == 3);
    CHECK(model.parts[0].mesh.indexCount == 3);
    model.destroy(renderer);
    renderer.shutdown();
}

static void testBadFile() {
    ensureTempDir();
    writeString(tempDir() + "/bad.xyz", "not a model");

    ion::Renderer renderer;
    ion::RendererConfig config;
    config.backend = ion::RendererBackend::Null;
    CHECK(renderer.initialize(nullptr, config));

    ion::Model model;
    CHECK(!ion::loadModel(renderer, tempDir() + "/bad.xyz", model));
    CHECK(model.parts.empty());
    renderer.shutdown();
}

int main() {
    ensureTempDir();
    testPngRgba();
    testPngPalette();
    testLoadObj();
    testLoadGltf();
    testLoadGlb();
    testBadFile();

    if (failures == 0) {
        std::printf("model_test: all tests passed\n");
        return 0;
    }
    std::printf("model_test: %d failures\n", failures);
    return 1;
}
