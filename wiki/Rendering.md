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

`ion::Camera` is a first-person 3D camera. Its position is a world-space point
and its orientation is stored as yaw and pitch, so the camera is always
upright (roll == 0). It produces a perspective projection by default (60
degree fov, near 0.1, far 1000) and can switch to orthographic.

```cpp
ion::Camera camera;

// Move and aim the camera.
camera.setPosition(ion::Vector3(0.0f, 3.0f, 10.0f));
camera.setYaw(-0.5f);   // turn left/right
camera.setPitch(0.2f);  // look up/down (clamped to +/- ~89 deg)
camera.lookAt(ion::Vector3(0.0f, 0.0f, 0.0f)); // or aim at a point

// The camera derives its aspect ratio from the window size.
camera.setViewport(windowWidth, windowHeight);
camera.setFov(1.2f);

// Per frame:
camera.move(ion::Vector3(0.0f, 0.0f, speed * dt)); // local-space motion
ion::Matrix4 mvp = camera.viewProjection();
renderer.setUniform("uMVP", mvp);
```

`forward()`, `right()` and `up()` return the camera's local-space unit axes.
`setPerspective(fov, aspect, near, far)` and `setOrthographic(left, right,
bottom, top, near, far)` replace the projection mode.

## Depth testing

Both the Metal and OpenGL backends render into a depth buffer and test against
it with `LessEqual` (depth write enabled), so overlapping triangles are
occluded correctly. The depth buffer is resized automatically with the window
and matches the MSAA sample count. No API is needed to enable depth testing; it
is always active. See `examples/3d` for a complete basic 3D scene (colored
cube, pyramid, checkerboard floor, free-look camera).

```cpp
// A simple 3D frame with a depth-tested mesh.
renderer.beginFrame();
renderer.clear(ion::Color(0.06f, 0.07f, 0.10f));
renderer.useShader(shader);
renderer.setUniform("uMVP", camera.viewProjection() * modelMatrix);
renderer.setVertexBuffer(mesh.vertexBuffer);
renderer.setIndexBuffer(mesh.indexBuffer);
renderer.drawIndexed(mesh.indexCount);
renderer.endFrame();
```

## Render commands

`Renderer` records `RenderCommand`s between frames. For debugging and tests:
`recordedCommandCount()` and `recordedCommands()` expose the current frame's
command list. See `include/ion/render/RenderCommand.hpp` for the command types
(`Clear`, `UseShader`, `SetTexture`, `BindVertexBuffer`, `BindIndexBuffer`,
`SetUniform*`, `Draw`, `DrawIndexed`).

## 2D rendering

For sprites, cameras, atlases, animation, tilemaps, particles, and text, see
[2D Rendering](Rendering-2D.md).
