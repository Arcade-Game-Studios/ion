# Rendering

Rendering types live in `ion::` and are declared under `include/ion/render/`.
The renderer is a command-recording abstraction: draw calls are recorded
between `beginFrame()` and `endFrame()`, then executed by the active backend.

## Backends

`RendererBackend`: `Automatic`, `Metal`, `OpenGL`, `Null`.

The Null backend needs no window or GPU and is used for tests and CI.

## Initialize

```cpp
ion::RendererConfig config;
config.backend = ion::RendererBackend::Metal; // or OpenGL / Automatic / Null
config.vsync = true;
config.antialiasSamples = 1;

if (!renderer.initialize(&window, config)) {
    return 1;
}

const ion::GPUInfo& gpu = renderer.gpuInfo();
// gpu.backend, gpu.vendor, gpu.name, gpu.driverVersion, gpu.videoMemoryBytes
```

## Frame

```cpp
renderer.beginFrame();
renderer.clear(ion::Color(0.08f, 0.09f, 0.12f));

// ... record draw commands ...

renderer.endFrame();
```

`ion::Color` is an RGBA float color. Helpers: `Color::white()`, `red()`,
`green()`, `blue()`, `yellow()`, `cyan()`, `magenta()`, `black()`,
`transparent()`, and `Color::fromRGB(r, g, b, a)` for byte values.

## Shaders

Shaders are supplied as Metal or GLSL source; pick based on the active backend.

```cpp
ion::ShaderSource source;
source.vertex = isMetal ? kMetalVertexShader : kGLSLVertexShader;
source.fragment = isMetal ? kMetalFragmentShader : kGLSLFragmentShader;

ion::Shader shader = renderer.createShader(source);
renderer.useShader(shader);
renderer.setUniform("uMVP", matrix4);
renderer.setUniform("tint", ion::Vector4(1.0f, 1.0f, 1.0f, 1.0f));
renderer.destroyShader(shader);
```

Uniforms are set by name with overloads for `float`, `Vector2`, `Vector3`,
`Vector4`, and `Matrix4`.

## Textures

```cpp
ion::TextureDesc desc;
desc.width = 8;
desc.height = 8;
desc.format = ion::TextureFormat::RGBA8; // RGBA8, RGBA16F, Depth
desc.filterLinear = true;
desc.generateMipmaps = false;

uint32_t pixels[8 * 8] = {0xFFFFFFFFu};
ion::Texture texture = renderer.createTexture(desc, pixels);
renderer.setTexture(0, texture);
renderer.destroyTexture(texture);
```

`Texture` stores its `id` and `desc`; `texture.isValid()` checks the id.

## Buffers

```cpp
ion::Vertex triangle[3] = {
    {{-0.8f, -0.8f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{ 0.8f, -0.8f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
    {{ 0.0f,  0.8f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
};
ion::VertexBuffer vb = renderer.createVertexBuffer(sizeof(triangle), triangle);

const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
ion::IndexBuffer ib = renderer.createIndexBuffer(6, /*is16Bit=*/true, indices);

renderer.setVertexBuffer(vb);
renderer.setIndexBuffer(ib);
renderer.drawIndexed(6);
```

`ion::Vertex` is `{Vector3 position; Vector4 color; Vector2 uv}` (stride 36
bytes; position at 0, color at 12, uv at 28). Buffers can be updated in place
with `updateVertexBuffer` / `updateIndexBuffer` and must be destroyed with
`destroyVertexBuffer` / `destroyIndexBuffer`.

Draw without indices with `renderer.draw(vertexCount)`.

## Camera

`ion::Camera` builds view and projection matrices for 3D scenes.

```cpp
ion::Camera camera;
camera.setPosition(ion::Vector3(0.0f, 0.0f, 3.0f));
camera.lookAt(ion::Vector3(0.0f, 0.0f, 0.0f), ion::Vector3(0.0f, 1.0f, 0.0f));

// Per frame:
camera.setPerspective(fovRadians, aspect, 0.1f, 100.0f);
ion::Matrix4 mvp = camera.projection() * camera.view();
renderer.setUniform("uMVP", mvp);
```

`setOrthographic(left, right, bottom, top, near, far)` is also available.

## Render commands

`Renderer` records `RenderCommand`s between frames. For debugging and tests:
`recordedCommandCount()` and `recordedCommands()` expose the current frame's
command list. See `include/ion/render/RenderCommand.hpp` for the command types
(`Clear`, `UseShader`, `SetTexture`, `BindVertexBuffer`, `BindIndexBuffer`,
`SetUniform*`, `Draw`, `DrawIndexed`).

## 2D rendering

For sprites, cameras, atlases, animation, tilemaps, particles, and text, see
[2D Rendering](Rendering-2D.md).
