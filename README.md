![Ion logo](ion-logo.png)

# Ion Engine

Ion Engine is a modern, open-source game engine written in C++ with native support for Windows, macOS, and Linux.

## Features
- Native C++20 architecture
- Cross-platform
- Modern rendering pipeline
- Editor included
- CMake build system

## Supported Platforms

![Supported operating systems](images/Operating%20Systems.png)

## Rendering Backends

![Metal](images/Metal.png) ![OpenGL](images/OpenGL%20logo.png) ![Vulkan](images/Vulkan%20logo.png)

- **Metal** — macOS
- **OpenGL** — Windows, macOS, Linux
- **Vulkan** — planned (see [roadmap](todo.md))

## Build

Requires CMake 3.16+ and a C++20 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

or via the Makefile wrapper:

```sh
make all       # configure and build
make run       # run the window test
```

Examples build to `build/examples/basic_example`.

## Screenshots

![2D example](screenshots/2d-example.png)

*The 2D example: a tilemap, an animated sprite, a particle emitter, and 2D primitives rendered through `SpriteBatch` (Metal backend).*

![Render example](screenshots/render-example.png)

*The render example: a checkerboard-textured quad and a color-interpolated triangle in 3D perspective (Metal backend).*

## Documentation

- [Wiki](wiki/README.md)
- [Documentation](docs/README.md)
- [Roadmap](todo.md)

## License

See [LICENSE](LICENSE).