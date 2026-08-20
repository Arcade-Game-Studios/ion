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

`ion::Vertex` is `{Vector3 position; Vector4 color; Vector2 uv; Vector3
normal}` (stride 48 bytes; position at 0, color at 12, uv at 28, normal at
40). The normal feeds per-vertex lighting in the 3D examples. Buffers can be
updated in place with `updateVertexBuffer` / `updateIndexBuffer` and must be
destroyed with `destroyVertexBuffer` / `destroyIndexBuffer`.

Draw without indices with `renderer.draw(vertexCount)`.

## Images

Decode image files into RGBA8 pixels for use with `createTexture`:

```cpp
uint32_t w, h;
std::vector<uint8_t> rgba;
if (ion::loadImage("assets/checker.png", w, h, rgba)) {
    ion::TextureDesc desc;
    desc.width = w;
    desc.height = h;
    desc.format = ion::TextureFormat::RGBA8;
    desc.filterLinear = true;
    ion::Texture tex = renderer.createTexture(desc, rgba.data());
}
```

`loadImage` (and the in-memory `loadImageFromMemory`) decode PNG files:
8-bit gray, RGB, palette, gray+alpha, and RGBA, including `tRNS` transparency
chunks. Unsupported formats and corrupt data return `false`.

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

## Render targets

Offscreen render targets (framebuffer objects) for post-processing, mirrors,
or rendering-to-texture.

```cpp
ion::RenderTargetDesc rtDesc;
rtDesc.width = 512;
rtDesc.height = 512;
rtDesc.format = ion::TextureFormat::RGBA8;
rtDesc.withDepth = true;

ion::RenderTarget target = renderer.createRenderTarget(rtDesc);

// Render into the target:
renderer.setRenderTarget(target);
renderer.beginFrame();
renderer.clear(ion::Color(0.0f, 0.0f, 0.0f));
// ... draw scene ...
renderer.endFrame();

// Switch back to the window:
renderer.setDefaultRenderTarget();

// Use target.color as a texture in subsequent passes:
renderer.setTexture(0, target.color);
```

API: `createRenderTarget(desc)`, `destroyRenderTarget(target)`,
`setRenderTarget(target)`, `setDefaultRenderTarget()`.

## Lighting

Up to 8 dynamic lights (directional or point) with per-material ambient color.

```cpp
ion::Lighting lighting;
lighting.ambient = ion::Color(0.1f, 0.1f, 0.15f);

lighting.lights[0].type = ion::LightType::Directional;
lighting.lights[0].direction = ion::Vector3(-0.4f, -0.7f, -0.5f);
lighting.lights[0].color = ion::Color(1.0f, 0.95f, 0.9f);
lighting.lights[0].intensity = 1.0f;
lighting.lightCount = 1;

ion::setLighting(renderer, lighting);
```

Light types: `Directional` (infinite distance, direction only) and `Point`
(position + range falloff). `kMaxLights` is 8.

## Skybox

A procedural gradient skybox rendered as a fullscreen background pass.

```cpp
ion::SkyboxConfig skyConfig;
skyConfig.top = ion::Vector3(0.3f, 0.5f, 0.9f);     // sky blue
skyConfig.horizon = ion::Vector3(0.8f, 0.85f, 0.9f); // haze
skyConfig.bottom = ion::Vector3(0.2f, 0.2f, 0.3f);  // dark ground
skyConfig.size = 100.0f;

ion::Texture skyboxTex = ion::createSkyboxTexture(renderer, skyConfig);
```

Use `createCubemap()` for custom 6-face cubemaps from loaded images.

## SVG

Parse and rasterize SVG images to RGBA8 pixels.

```cpp
ion::SvgImage svg;
if (svg.parseFile("assets/icon.svg")) {
    std::vector<uint8_t> pixels = svg.rasterize();
    // Create a texture from the rasterized pixels:
    ion::TextureDesc desc;
    desc.width = svg.width();
    desc.height = svg.height();
    desc.filterLinear = true;
    ion::Texture tex = renderer.createTexture(desc, pixels.data());
}

// Or use the one-shot helper:
ion::Texture tex = svg.createTexture(renderer);
```

API: `parse(svgString)`, `parseFile(path)`, `isValid()`, `width()`, `height()`,
`rasterize()`, `createTexture(renderer)`.

## Performance overlay

A built-in FPS and frame-time overlay that draws directly to the screen.

```cpp
ion::PerformanceOverlay overlay;
overlay.initialize(&renderer, window.width(), window.height());
overlay.setEnabled(true);

// Each frame, between beginFrame and endFrame:
overlay.render();
```

API: `initialize(renderer, viewportW, viewportH)`, `shutdown()`,
`setEnabled(bool)`, `isEnabled()`, `setViewport(w, h)`, `setFontSize(height)`.

## Render commands

`Renderer` records `RenderCommand`s between frames. For debugging and tests:
`recordedCommandCount()` and `recordedCommands()` expose the current frame's
command list. See `include/ion/render/RenderCommand.hpp` for the command types
(`Clear`, `UseShader`, `SetTexture`, `BindVertexBuffer`, `BindIndexBuffer`,
`SetUniform*`, `Draw`, `DrawIndexed`).

## Meshes, materials, models

`ion::Mesh` wraps a vertex buffer and an optional index buffer. Build one from
a CPU vertex array (32-bit indices are packed down to 16-bit when possible):

```cpp
ion::Mesh mesh = ion::createMesh(renderer, vertices, vertexCount,
                                 indices, indexCount);
renderer.useShader(shader);
renderer.setUniform("uMVP", camera.viewProjection() * modelMatrix);
renderer.setVertexBuffer(mesh.vertexBuffer);
renderer.setIndexBuffer(mesh.indexBuffer);
renderer.drawIndexed(mesh.indexCount);
// ...
ion::destroyMesh(renderer, mesh);
```

`ion::Material` describes a surface: `name`, `baseColor` (multiplied by the
optional `texture`, or the final color when no texture is set), and `metallic`
/ `roughness` (informational until the lighting system lands).

Models are loaded by extension from disk:

```cpp
ion::Model model;
if (ion::loadModel(renderer, "assets/cube.glb", model)) {
    for (const ion::ModelPart& part : model.parts) {
        // part.mesh, part.material, part.transform (node transform from
        // the source file, if any)
    }
    // ...draw each part with part.mesh...
    model.destroy(renderer); // frees meshes + textures
}
```

Supported formats:
- `.obj` (Wavefront) with an optional `.mtl` sidecar: `Kd` color, `d`/`Tr`
  opacity, `map_Kd` texture. Geometry is split into one part per material.
- `.gltf` / `.glb` (glTF 2.0) for static meshes: accessors, primitives,
  `pbrMetallicRoughness` materials (`baseColorFactor` + `baseColorTexture`),
  and node transforms. Skinning, morph targets, animations, and
  camera/light nodes are ignored.

See `examples/model` for a scene that loads all three formats side by side.

## 2D rendering

For sprites, cameras, atlases, animation, tilemaps, particles, and text, see
[2D Rendering](Rendering-2D.md).
