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

`examples/input` — keyboard, mouse, and gamepad input showcase.

## multi_window

`examples/multi_window` — creates and manages multiple windows.
