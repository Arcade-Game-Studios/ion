# Ion Engine Wiki

Welcome to the Ion Engine wiki. Ion is a lightweight, modern C++20 game framework
with native support for **Windows, macOS, and Linux**. It ships a clean public
API, a Metal and OpenGL rendering backends, and a 2D rendering layer.

Current version: **0.2.0 "Two Dimensional"**

## Getting started

- [Getting Started](Getting-Started.md) — build the engine and run your first app
- [Examples](Examples.md) — what the bundled examples demonstrate

## API guides

- [Core](Core.md) — application lifecycle, config, logging, error handling, memory, timing
- [Platform](Platform.md) — windows, input (keyboard/mouse/gamepad), asset paths
- [Rendering](Rendering.md) — the renderer abstraction, backends, shaders, textures, buffers
- [2D Rendering](Rendering-2D.md) — sprite batches, cameras, atlases, animation, tilemaps, particles, text
- [Math](Math.md) — vectors and matrices
- [ECS](ECS.md) — the entity/component/system foundation

## Project

- [Roadmap](../todo.md) — phase-by-phase development plan
- [Documentation](../docs/README.md) — repository layout

## Status

- Phases 0–4 complete (foundation, core, window/platform, input, renderer)
- Phase 5 (2D rendering) implemented except SVG rendering
- Phases 6–12 planned (3D rendering, ECS, audio, terminal renderer, tools, assets, release)
