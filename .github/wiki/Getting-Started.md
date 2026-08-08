# Getting Started

## Requirements

- CMake 3.16+
- A C++20 compiler
- macOS, Windows, or Linux

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or via the Makefile wrapper:

```sh
make all       # configure and build
make run       # run the basic window test
```

Examples build to `build/examples/`.

## Backends

The renderer selects a backend at runtime via `RendererConfig`:

- `RendererBackend::Automatic` — pick the best available backend
- `RendererBackend::Metal` — Metal (macOS)
- `RendererBackend::OpenGL` — OpenGL 4.1 (macOS)
- `RendererBackend::Null` — headless, no window or GPU required

The Null backend is useful for tests and CI. It accepts a `nullptr` window and
records commands without executing them.

## Run an example

```sh
./build/examples/basic_example
./build/examples/render_example metal
./build/examples/render_example gl
./build/examples/2d_example metal
```

## Your first app

```cpp
#include <ion/core/Application.hpp>
#include <ion/core/Version.hpp>
#include <ion/platform/Window.hpp>

#include <cstdio>

class MyApp : public ion::Application {
public:
    void initialize() override {
        std::printf("Ion Engine v%s initializing\n", ion::VERSION_STRING);
    }

    void update(float deltaTime) override {
        // Called once per frame with the elapsed time in seconds.
    }

    void render() override {
        // Draw here.
    }

    void shutdown() override {
        // Clean up.
    }
};

int main() {
    ion::WindowConfig config;
    config.title = "My Game";
    config.width = 960;
    config.height = 540;

    ion::Window window(config);
    if (!window.create()) {
        return 1;
    }

    MyApp app;
    app.initialize();
    while (window.isOpen()) {
        window.pollEvents();
        app.update(1.0f / 60.0f);
        app.render();
    }
    app.shutdown();
    window.destroy();
    return 0;
}
```

> Use `ion::Engine` when you want the loop, input pump, and window lifecycle
> handled for you instead of driving a `Window` manually.
