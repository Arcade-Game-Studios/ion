# Core

Core types live in `ion::` and are declared under `include/ion/core/`.

## Application

`ion::Application` is the base class for a program. Override the lifecycle
callbacks and call `run()` to execute the main loop. `quit()` requests that the
loop stop after the current frame.

```cpp
class MyApp : public ion::Application {
public:
    void initialize() override;
    void update(float deltaTime) override;
    void render() override;
    void shutdown() override;
};
```

A subclass owns its `WindowConfig` in `windowConfig_` (protected).

## Engine

`ion::Engine` drives the window, input pump, and loop for you. Provide a frame
callback that returns `false` to stop.

```cpp
ion::Engine engine;
engine.initialize();
engine.setFrameCallback([](float deltaTime) {
    // update + render
    return true; // keep running
});
engine.run();
```

## Config

Key/value configuration with typed getters and setters, persisted to disk.

```cpp
ion::Config config;
config.load("settings.ini");
float volume = config.getFloat("audio.volume", 0.5f);
config.set("audio.volume", 0.75f);
config.save("settings.ini");
```

Supported types: string, int, float, bool. `has()` checks presence; `clear()`
resets all values.

## Logging

```cpp
#include <ion/core/Log.hpp>

ion::setLogLevel(ion::LogLevel::Info);
ION_LOG_TRACE("...");
ION_LOG_DEBUG("...");
ION_LOG_INFO("...");
ION_LOG_WARN("...");
ION_LOG_ERROR("...");
ION_LOG_FATAL("...");
```

Levels: `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Fatal`.

## Error handling

`ion::Error` carries an `ErrorCode` and a message; `ok()` is true when the code
is `None`. `ion::Result<T>` wraps either a value or an `Error`.

```cpp
ion::Result<Texture> result = loadSomething();
if (!result) {
    ion::log(ion::LogLevel::Error, "%s", result.error().message.c_str());
    return;
}
Texture& texture = result.value();
```

Assertion helpers log without aborting:

```cpp
ION_ASSERT(condition, "message");
ION_VERIFY(condition, "message");
```

## Memory

Utilities and a global allocation tracker.

```cpp
ion::isPowerOfTwo(64);       // true
ion::alignUp(13, 8);         // 16
ion::Memory::allocate(size);
ion::Memory::free(ptr);
ion::Memory::allocationCount();
ion::Memory::allocatedBytes();
```

## Timer

`ion::Timer` measures elapsed time with a steady clock.

```cpp
ion::Timer timer;
timer.reset();
float delta = timer.tick();      // seconds since last tick()
double total = timer.elapsedSeconds();
```

## Version

```cpp
ion::VERSION_MAJOR   // 0
ion::VERSION_MINOR   // 2
ion::VERSION_PATCH   // 0
ion::VERSION_STRING  // "0.2.0"
ion::VERSION_NICKNAME // "Two Dimensional"
```

## Umbrella header

Include the whole public API at once:

```cpp
#include <ion/core/ion.hpp>
```
