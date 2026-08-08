# ECS

The ECS (entity-component-system) foundation lives in `ion::` and is declared
under `include/ion/ecs/`.

> Status: Phase 7 (ECS System) is **planned**. These headers define the
> foundation types; the full entity registry, component storage, systems
> pipeline, and scene management are not yet implemented.

## Entity

```cpp
using ion::EntityId = uint64_t;

ion::Entity entity;
entity.isValid(); // false until given an id
```

`INVALID_ENTITY` is `0`.

## Component

```cpp
enum class ComponentType : uint32_t {};
struct Component {
    ComponentType type{};
};
```

Custom components derive from `ion::Component` and store plain data.

## System

```cpp
class System {
public:
    virtual ~System() = default;
    virtual void update(float deltaTime) = 0;
};
```

Implement `update()` to process entities each frame.
