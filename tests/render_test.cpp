#include <ion/math/Matrix4.hpp>
#include <ion/math/Vector3.hpp>
#include <ion/render/Camera.hpp>
#include <ion/render/RenderCommand.hpp>
#include <ion/render/Renderer.hpp>
#include <ion/render/Vertex.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

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

static void testNullBackend() {
    ion::Renderer renderer;
    ion::RendererConfig config;
    config.backend = ion::RendererBackend::Null;

    CHECK(renderer.initialize(nullptr, config));
    CHECK(renderer.isInitialized());
    CHECK(renderer.gpuInfo().backend == ion::RendererBackend::Null);

    renderer.beginFrame();
    renderer.clear(ion::Color::blue());
    renderer.draw(3);
    renderer.drawIndexed(6, 0);
    CHECK(renderer.recordedCommandCount() == 3);

    const ion::RenderCommand* commands = renderer.recordedCommands();
    CHECK(commands[0].type == ion::RenderCommandType::Clear);
    CHECK(commands[0].clearColor.b == 1.0f);
    CHECK(commands[1].type == ion::RenderCommandType::Draw);
    CHECK(commands[1].count == 3);
    CHECK(commands[2].type == ion::RenderCommandType::DrawIndexed);
    CHECK(commands[2].count == 6);

    renderer.endFrame();
    CHECK(renderer.recordedCommandCount() == 0);
    renderer.shutdown();
    CHECK(!renderer.isInitialized());
}

static void testCommandRecording() {
    ion::Renderer renderer;
    ion::RendererConfig config;
    config.backend = ion::RendererBackend::Null;
    CHECK(renderer.initialize(nullptr, config));

    ion::ShaderSource source;
    source.vertex = "null";
    source.fragment = "null";
    ion::Shader shader = renderer.createShader(source);
    CHECK(shader.isValid());

    const uint8_t texPixels[16] = {255, 0, 0, 255};
    ion::TextureDesc texDesc;
    texDesc.width = 2;
    texDesc.height = 2;
    ion::Texture texture = renderer.createTexture(texDesc, texPixels);
    CHECK(texture.isValid());

    const float vertexData[9] = {0.0f};
    ion::VertexBuffer vb = renderer.createVertexBuffer(sizeof(vertexData),
                                                       vertexData);
    CHECK(vb.isValid());
    const uint16_t indices[3] = {0, 1, 2};
    ion::IndexBuffer ib = renderer.createIndexBuffer(3, true, indices);
    CHECK(ib.isValid());

    renderer.beginFrame();
    renderer.useShader(shader);
    renderer.setTexture(1, texture);
    renderer.setVertexBuffer(vb);
    renderer.setIndexBuffer(ib);

    ion::Matrix4 mvp = ion::Matrix4::identity();
    renderer.setUniform("uMVP", mvp);
    renderer.setUniform("tint", ion::Vector4(1.0f, 0.5f, 0.25f, 1.0f));
    renderer.setUniform("scale", 2.0f);
    renderer.drawIndexed(3, 1);

    CHECK(renderer.recordedCommandCount() == 8);

    const ion::RenderCommand* commands = renderer.recordedCommands();
    CHECK(commands[0].type == ion::RenderCommandType::UseShader);
    CHECK(commands[0].shaderId == shader.id);
    CHECK(commands[1].type == ion::RenderCommandType::SetTexture);
    CHECK(commands[1].textureSlot == 1);
    CHECK(commands[2].type == ion::RenderCommandType::BindVertexBuffer);
    CHECK(commands[3].type == ion::RenderCommandType::BindIndexBuffer);

    CHECK(commands[4].type == ion::RenderCommandType::SetUniformMat4);
    CHECK(commands[4].uniformName == "uMVP");
    CHECK(commands[4].uniformBytes == sizeof(float) * 16);
    CHECK(commands[4].uniformData[0] == 1.0f);

    CHECK(commands[5].type == ion::RenderCommandType::SetUniformVec4);
    CHECK(commands[5].uniformName == "tint");
    CHECK(commands[5].uniformData[1] == 0.5f);

    CHECK(commands[6].type == ion::RenderCommandType::SetUniformFloat);
    CHECK(commands[6].uniformName == "scale");
    CHECK(commands[6].uniformData[0] == 2.0f);

    CHECK(commands[7].type == ion::RenderCommandType::DrawIndexed);
    CHECK(commands[7].count == 3);
    CHECK(commands[7].startIndex == 1);

    renderer.endFrame();
    renderer.destroyShader(shader);
    renderer.destroyTexture(texture);
    renderer.destroyVertexBuffer(vb);
    renderer.destroyIndexBuffer(ib);
    renderer.shutdown();
}

static void testVertexFormat() {
    CHECK(sizeof(ion::Vertex) == 36);
    ion::Vertex vertex = {{1.0f, 2.0f, 3.0f}, {0.5f, 0.5f, 0.5f, 1.0f},
                          {0.25f, 0.75f}};
    CHECK(vertex.position.x == 1.0f);
    CHECK(vertex.color.w == 1.0f);
    CHECK(vertex.uv.y == 0.75f);
}

static void testCameraOrthographic() {
    ion::Camera camera;
    camera.setPosition(ion::Vector3(0.0f, 0.0f, 0.0f));
    camera.lookAt(ion::Vector3(0.0f, 0.0f, -1.0f), ion::Vector3(0, 1, 0));
    camera.setOrthographic(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 3.0f);

    ion::Vector3 near = camera.projection() *
                        (camera.view() * ion::Vector3(0.0f, 0.0f, -1.0f));
    CHECK_NEAR(near.z, -1.0f, 1e-4f);

    ion::Vector3 far = camera.projection() *
                       (camera.view() * ion::Vector3(0.0f, 0.0f, -3.0f));
    CHECK_NEAR(far.z, 1.0f, 1e-4f);

    ion::Vector3 mid = camera.projection() *
                       (camera.view() * ion::Vector3(1.0f, 1.0f, -2.0f));
    CHECK_NEAR(mid.x, 1.0f, 1e-4f);
    CHECK_NEAR(mid.y, 1.0f, 1e-4f);
    CHECK_NEAR(mid.z, 0.0f, 1e-4f);
}

static void testCameraPerspective() {
    ion::Camera camera;
    camera.setPosition(ion::Vector3(0.0f, 0.0f, 0.0f));
    camera.lookAt(ion::Vector3(0.0f, 0.0f, -1.0f), ion::Vector3(0, 1, 0));
    camera.setPerspective(1.0f, 1.0f, 0.1f, 10.0f);

    // Pre-divide values: far point (camera z=-10, w=10) maps to NDC z=1,
    // near point (camera z=-0.1, w=0.1) maps to NDC z=-1.
    ion::Vector3 far = camera.projection() *
                       (camera.view() * ion::Vector3(0.0f, 0.0f, -10.0f));
    CHECK_NEAR(far.z, 10.0f, 1e-4f);

    ion::Vector3 near = camera.projection() *
                        (camera.view() * ion::Vector3(0.0f, 0.0f, -0.1f));
    CHECK_NEAR(near.z, -0.1f, 1e-4f);

    ion::Vector3 center = camera.projection() *
                          (camera.view() * ion::Vector3(0.0f, 0.0f, -1.0f));
    CHECK_NEAR(center.x, 0.0f, 1e-4f);
    CHECK_NEAR(center.y, 0.0f, 1e-4f);
}

int main() {
    testNullBackend();
    testCommandRecording();
    testVertexFormat();
    testCameraOrthographic();
    testCameraPerspective();

    if (failures == 0) {
        std::printf("render_test: all tests passed\n");
        return 0;
    }
    std::printf("render_test: %d failure(s)\n", failures);
    return 1;
}
