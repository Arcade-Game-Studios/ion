# Ion Engine

Ion Engine is a modern, open-source game engine written in C++ with native support for Windows, macOS, and Linux.

## Features
- Native C++20 architecture
- Cross-platform
- Modern rendering pipeline
- Editor included
- CMake build system

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

## Documentation

- [Wiki](wiki/Home.md)
- [Documentation](docs/README.md)
- [Roadmap](todo.md)

## License

See [LICENSE](LICENSE).