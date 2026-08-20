# Installing

Ion provides install and update scripts that copy engine headers, libraries,
and a starter template into `~/.ion`. Game projects then `find_package(ion)`
to locate the installed files.

## Install

```sh
./install-ion.sh
```

This builds the engine in Release mode, installs headers and static libraries
to `~/.ion`, and copies a starter template.

## Create a game

With the engine installed:

```sh
mkdir my_game && cd my_game
cp ~/.ion/template/* .
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . && ./my_game
```

Or without installing, with Ion bundled alongside your game:

```sh
./new-game.sh my_game
cd my_game
make run
```

## Update

Rebuild and reinstall from the engine repo:

```sh
cd /path/to/ion-engine
./update-ion.sh
```

For local (add_subdirectory) projects:

```sh
./update-ion.sh --local
```

## New game script

`new-game.sh` creates a full game project with Ion bundled. It copies only
the minimal engine source files — no `third_party/`, no wiki, no GitHub
files — keeping the total around 2 MB.

```sh
./new-game.sh /path/to/my_game    # absolute path
./new-game.sh my_game             # relative path
```

The generated project includes a `Makefile` that builds and runs via CMake:

```sh
cd my_game
make          # configure and build
make run      # build and run
make clean    # remove build directory
```

## Workflow

```sh
1. Install the engine once:
   ./install-ion.sh

2. Create projects from the template:
   mkdir my_game && cd my_game
   cp ~/.ion/template/* .

3. Write code in src/main.cpp and src/Game.cpp

4. Build and run:
   mkdir build && cd build
   cmake .. && cmake --build . && ./my_game

5. Update when Ion is updated:
   cd /path/to/ion-engine
   ./update-ion.sh
```
