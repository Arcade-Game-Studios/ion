#pragma once

#include <cstdint>

namespace ion {

class Renderer {
public:
    Renderer() = default;
    virtual ~Renderer() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;

    virtual void clearColor(float r, float g, float b, float a) = 0;
};

} // namespace ion
