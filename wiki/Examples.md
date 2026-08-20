# Examples

Examples live in `examples/` and build to `build/examples/`.

## basic

`examples/basic` — the smallest possible app. Creates a window, prints the
engine version, and runs a `pollEvents -> update -> render` loop. A good
template for new projects.

```sh
./build/examples/basic_example
```

## render

`examples/render` — low-level rendering with the `Renderer` API. Draws a
rotating triangle and quad with a custom shader, camera, checkerboard texture,
and vertex/index buffers.

```sh
./build/examples/render_example metal
./build/examples/render_example gl
./build/examples/render_example null
```

## 2d

`examples/2d` — the 2D rendering showcase. Demonstrates:

- a procedurally generated 64x64 sprite sheet (tiles + hero frames + star)
- a 48x48 tilemap with walls, a lake, and a sand patch
- an animated, rotating hero driven with WASD/arrow keys
- a particle emitter with a Space-bar burst
- a screen-space font HUD via `SpriteBatch` + `Camera2D`

```sh
./build/examples/2d_example metal
./build/examples/2d_example gl
./build/examples/2d_example null
```

Press `Escape` to quit.

## input

`examples/input` — keyboard, mouse, and gamepad input showcase. Displays
connected gamepads, axes, buttons, and live keyboard/mouse state. Demonstrates
the action-map system for binding abstract names to physical inputs.

```sh
./build/examples/input_example metal
./build/examples/input_example gl
```

## multi_window

`examples/multi_window` — creates and manages multiple OS windows. Each window
gets its own Renderer and render pass. Demonstrates the Null backend's
multi-window support and per-window resource isolation.

```sh
./build/examples/multi_window_example metal
./build/examples/multi_window_example null
```

## 3d

`examples/3d` — a minimal 3D scene using the `Renderer` and `Camera` APIs.
Draws a colored cube, pyramid, and checkerboard floor with a free-look camera.
Demonstrates depth testing, vertex/index buffers, and per-vertex coloring.

```sh
./build/examples/3d_example metal
./build/examples/3d_example gl
```

## model

`examples/model` — loads and displays 3D models from OBJ and glTF files.
Demonstrates `ion::loadModel` with `ion::Model`, `ion::Material`,
`ion::Mesh`, and node transforms. Builds a textured cube, pyramid, and
teapot from the `examples/model/assets/` directory.

```sh
./build/examples/model_example metal
./build/examples/model_example gl
```

## lighting

`examples/lighting` — a dynamic lighting showcase. Renders a sphere, cube,
and floor with multiple directional and point lights using `ion::Lighting`.
Demonstrates ambient color, light intensity, and range falloff.

```sh
./build/examples/lighting_example metal
./build/examples/lighting_example gl
```
