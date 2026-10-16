# Changelog

## Unreleased

Every bullet is a single change. Bug fixes are listed first; each one was
reproduced (failing test, sanitizer report, cross-compiler error or
comparison against the renderer's own matrices) before being fixed.

### Bug fixes

#### Build
- Six of eight test programs failed to link on Linux (static-library cycle between `ion_core` and the platform library).
- The Windows platform library did not compile: `Window::Impl` was defined inside an anonymous namespace.
- …and used while private from the window procedure.
- …and `create()` called `.get()` on a raw pointer.
- `Xinput.h` casing broke MinGW builds; xinput is now linked through CMake.
- `vulkan_win32.h` was included without `<windows.h>`, so the Vulkan backend could not build on Windows.
- Native Windows builds required a system zlib that Windows does not ship; CMake now fetches zlib when missing.

#### Core
- The replacement global `operator new` returned 8-byte aligned memory (16 is required); every allocation was misaligned.
- `Config::getInt`/`getFloat` returned 0 instead of the default for unparsable values.
- `Config::getBool` ignored `on`/`YES`/`Off` and other casings.
- Float settings were saved with 6 significant digits and did not round-trip.
- Values with leading/trailing spaces were trimmed on reload; they are now quoted.
- `Result<T>` required `T` to be default-constructible and returned a garbage value on error.
- A `Result` built from an `Error` with code `None` reported success with no value.
- `Engine::window()` dereferenced null after a move.
- A long frame (breakpoint, window drag) produced a multi-second delta; deltas are now clamped.

#### Input and platforms
- Linux: Xbox-style triggers were read as the right stick (fixed axis order instead of the kernel's axis map).
- Linux: stick Y was inverted relative to macOS and Windows.
- Linux: unknown gamepad buttons triggered `A`.
- Linux: unknown gamepad axes overwrote `LeftX`.
- Linux: unplugged gamepads stayed "connected" forever.
- Linux: absent gamepad devices were reopened every frame (four syscalls per frame); now once a second.
- Linux: non-resizable windows could be resized.
- Linux: resizable windows could not shrink below their initial size.
- Linux: `onResize` fired on window moves.
- Linux: `setFullscreen` dereferenced a null `impl`.
- Linux: keys stayed held after focus moved to another window.
- Linux and Windows: `executableDirectory()` always returned `"."`, so assets failed to load when launched from elsewhere.
- Windows: modifier keys never registered (`WM_KEYDOWN` reports generic `VK_SHIFT`/`VK_CONTROL`/`VK_MENU`).
- Windows: keypad Enter was reported as Enter.
- Windows: Alt+F4 was swallowed (`WM_SYSKEYDOWN` returned 0).
- Windows: window titles were byte-widened instead of converted from UTF-8.
- Windows: minimizing reported a 0x0 resize.
- Windows: leaving fullscreen restored a resizable frame on non-resizable windows.
- Windows: `setFullscreen` dereferenced a null `impl`.
- macOS: launching with `fullscreen = true` never went fullscreen.
- Gamepad buttons and axes kept their last state after a disconnect on every platform.

#### Rendering
- `Camera2D::screenToWorld`/`worldToScreen` applied zoom inversely to what is rendered (picking off by zoom²).
- `Camera2D::viewProjection` rotated about the world origin instead of the camera.
- Meshes with more than 65,535 vertices silently dropped all their indices.
- `Model::destroy` destroyed a texture once per part sharing it.
- `Renderer::setUniform`/`draw`/`drawIndexed` crashed on a moved-from renderer.
- `SpriteBatch` lost its window pointer when moved (stopped tracking resizes).
- `SpriteBatch::begin` on an uninitialized batch crashed.
- `SpriteBatch::begin` called twice dropped the pending quads.
- `TextureAtlas` accepted regions outside the texture.
- Particles with zero or negative lifetime divided by zero (NaN colors and sizes).
- Particle spawning scanned the whole pool per particle (O(n²) bursts).
- `Tilemap::draw` culled visible tiles when the camera was rotated.
- `Font` leaked its atlas texture and could be copied into a double destroy.
- SVG: `C`, `S`, `Q`, `T` and `A` path commands had x/y swapped on GCC and MSVC (unsequenced argument evaluation).

#### Asset loading
- PNG: grayscale/RGB `tRNS` compared the wrong byte and wrote an unrelated byte into alpha.
- PNG: 1/2/4-bit and 16-bit images failed to load.
- PNG: interlaced images failed to load.
- PNG: image size arithmetic could overflow before allocation.
- OBJ: double spaces or tabs between fields parsed as 0.
- OBJ: a degenerate normal shifted every later normal index.
- glTF: `"normalized": false` was treated as true.
- glTF: accessors without a `bufferView` dereferenced null (crash).
- glTF: accessors were not bounds-checked against their buffers (out-of-bounds reads).
- glTF: primitives referencing missing accessors crashed.
- glTF: attribute accessors with the wrong count overran the vertex arrays.
- glTF: out-of-range indices were uploaded to the GPU.
- glTF: node cycles looped forever.
- glTF: files without `scenes` loaded nothing.
- `loadModel` rejected four-character `.glb` names.

#### Audio
- The mixer read past the end of a sound when a looping voice's step exceeded the sound length (heap-buffer-overflow).
- Volume and pan changes clicked (gains jumped at buffer boundaries).
- Voice stealing could cut off looping music.

### New features

#### Math
- `MathUtils.hpp`: constants, `radians`/`degrees`, `clamp`, `saturate`, `lerp`, `inverseLerp`, `remap`.
- `smoothstep`, `smootherstep`, `moveTowards`, `sign`, `approxEqual`.
- `wrapAngle`, `angleDifference`, `lerpAngle`.
- Frame-rate independent `damp`, plus `repeat`, `pingPong`, `nextPowerOfTwo`.
- Vectors: unary minus, component-wise `*` and `/`, indexing, constants.
- Vectors: `normalize`, `distance`, `lerp`, `min`/`max`/`abs`, `reflect`, `project`, `angleBetween`, `moveTowards`, `clampedLength`, `approxEqual`.
- `Vector2`: `angle`, `rotated`, `fromAngle`, `perpendicular`, 2D `cross`.
- `Vector3`: `projectOnPlane`, swizzles, `up`/`forward`/`right`.
- `Matrix4`: `rotationX/Y`, axis-angle `rotation`, `compose`.
- `Matrix4`: `transpose`, `determinant`, `inverted`.
- `Matrix4`: `transformPoint` (perspective divide), `transformDirection`, `Matrix4 * Vector4`, `getTranslation`, `getScale`, `==`, `approxEqual`.
- `Quaternion` with axis-angle, Euler, `fromTo`, `lookRotation`, matrix conversion, slerp/nlerp.
- 2D geometry: `Rect`, `Circle`, segments, rays.
- 2D collision manifolds for rect/circle pairs.
- Segment intersection, closest point, point-in-polygon.
- 2D raycasts against rects, circles and segments.
- Swept AABB (`sweep`) for tunnelling-free movement.
- 3D `AABB`, `Sphere`, `Plane`, `Ray` with raycasts (including triangles).
- View-frustum culling (`Frustum`).
- `Random`: deterministic PCG32 with ranges, gaussian, unit vectors, weighted picks and shuffle.
- `Color`: hex and HSV conversion, RGBA8 packing, lerp, operators, luminance and more named colors.

#### Core
- Memory stats: peak bytes, total allocations, `resetPeak`, `allocationSize`.
- Log callback sink.
- Log to file.
- Log timestamps and console toggle.
- Compile-time printf format checking for `ION_LOG_*`.
- `Config`: `[section]` support, quoted values, `parse`/`toString`, `remove`/`keys`/`merge`.
- `Result<T>::valueOr`, `operator->`, `errorCodeName`.
- `Engine`: fixed-timestep callback with interpolation factor.
- `Engine`: time scale.
- `Engine`: frame limiter (`setTargetFps`).
- `Engine`: `requestQuit`, frame count, elapsed time, smoothed FPS.
- `Application`: `fixedUpdate` hook and `engine()`/`window()` accessors.
- `JsonValue`: public JSON parser/writer with builder API and file I/O.
- `EventBus`: typed, reentrancy-safe publish/subscribe with a deferred queue.
- `Tween<T>` with 31 easing curves, delay, repeat, yoyo and callbacks.
- `Scheduler` for game-time `after`/`every` callbacks.
- `StateMachine<State>`.
- `ObjectPool<T>`.
- `fs::` file helpers with atomic writes and a per-user data directory.
- `Profiler` with `ION_PROFILE_SCOPE`.
- `AssetCache<T>` with shared loading and hot reload.

#### Platforms and input
- Linux ALSA audio output (loaded at runtime; previously no audio on Linux).
- Windows waveOut audio output (previously silent).
- `Window::setTitle`/`title` on all platforms.
- Text input (`input::textInput`) on all platforms, layout-aware and UTF-8.
- Keys are released when the window loses focus, on all platforms.
- Linux: D-pad hats mapped to D-pad buttons.
- Linux: back/forward mouse buttons.
- Linux: UTF-8 window titles and WM class hint.
- `isAnyKeyPressed`, `keyFromName`, `mousePosition`, `mouseDelta`.
- `gamepadStick` with a radial dead zone, `firstConnectedGamepad`.
- `ActionMap`: gamepad buttons, analog axes, key-pair axes and `value()`.

#### Rendering
- `Camera2D`: `visibleBounds`, `move`, smooth `follow`, `clampTo`.
- `Tilemap`: fill helpers, world/tile conversion, per-id solidity.
- `Tilemap::moveAndSlide` platformer collision with ground/ceiling flags.
- `SpriteBatch`: horizontal/vertical flipping.
- `SpriteBatch`: triangles, convex polygons, polylines.
- `SpriteAnimation`: ping-pong mode, speed, seeking, frame callbacks, progress.
- `TextureAtlas::sequence` and `contains`.
- `ParticleEmitter`: `burstAt`, `prewarm`, spawn radius, seeding, `isFinished`.
- `Font`: tabs, word wrap, aligned drawing, `lineHeight`.
- Procedural cube/plane/sphere meshes and `computeNormals`.
- Mesh bounds and `Model::bounds`/`triangleCount`.
- Performance overlay: vertex count, worst frame, heap usage, custom lines.

#### Asset loading
- BMP decoding (24/32-bit, bitfields, both row orders).
- TGA decoding (24/32-bit and grayscale, raw and RLE).
- `encodePng`/`savePng`.
- `flipImageVertically` and `premultiplyAlpha`.
- OBJ: per-vertex colors, `vt` with one component, `Ns`/`Pr`/`Pm`, texture options.
- glTF: `COLOR_0` vertex colors.

#### Audio
- Pause/resume per voice and globally.
- Fade in/out.
- `stopBus`, `setVoiceLoop`, start offset, `voiceTime`.
- Spatial audio helpers (`computeSpatial`, `spatialize`).

#### Gameplay
- `Scene`/`SceneManager` with deferred transitions and overlays.
- `PhysicsWorld2D`: boxes and circles, restitution, friction, kinematic bodies, triggers, layers, raycasts and queries.
- `TerminalCanvas` and `TerminalInput` for terminal games.
- ECS: `getOrEmplace`, `patch`, `count`, `clearComponent`, `each`, `onDestroy`.
- ECS: `View::count`/`empty`/`front`, `std::hash<Entity>`.
- `SystemManager`: priorities, enable/disable, `get`/`remove`, lifecycle hooks.

### Build, CI and tooling
- CI runs the tests (previously build-only).
- CI: ASan/UBSan job.
- CI: Windows cross-build job running the tests under Wine.
- Linux CI runs under Xvfb so window tests execute.
- `ION_WARNINGS`, `ION_WARNINGS_AS_ERRORS` and `ION_SANITIZE` options.
- `ion_add_test()` replaces per-platform boilerplate.
- MinGW-w64 toolchain file (`cmake/mingw-w64-x86_64.cmake`).
- `scripts/gen_image_fixtures.py` generates and cross-checks image fixtures.
- New example: terminal Snake (`examples/terminal`).

### Tests
- New suites: `math_test`, `image_test`, `systems_test`, `physics_test`, `terminal_test`, `window_x11_test` (drives X11 with synthetic events) and `audio_device_test` (plays through ALSA's null sink).
- Extended: `core_test`, `input_test`, `2d_test`, `model_test`, `audio_test`.
