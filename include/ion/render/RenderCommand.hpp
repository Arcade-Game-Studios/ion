#pragma once

#include <ion/render/Color.hpp>

#include <cstdint>
#include <cstring>
#include <string>

namespace ion {

enum class RenderCommandType : uint8_t {
    Clear,
    UseShader,
    SetTexture,
    BindVertexBuffer,
    BindIndexBuffer,
    SetUniformFloat,
    SetUniformVec2,
    SetUniformVec3,
    SetUniformVec4,
    SetUniformMat4,
    Draw,
    DrawIndexed,
};

//
// A single deferred render command. The renderer records these during the
// frame (after beginFrame) and the backend executes them in order at
// endFrame. Draw commands execute with the state set by preceding commands
// (immediate-mode semantics).
//
struct RenderCommand {
    RenderCommandType type = RenderCommandType::Clear;

    Color clearColor;
    uint64_t shaderId = 0;
    uint64_t textureId = 0;
    int textureSlot = 0;
    uint64_t vertexBufferId = 0;
    uint64_t indexBufferId = 0;
    uint32_t count = 0;
    uint32_t startIndex = 0;

    std::string uniformName;
    float uniformData[16] = {0.0f};
    uint32_t uniformBytes = 0;
};

inline void fillUniformData(RenderCommand& command, const void* data,
                            uint32_t bytes) {
    command.uniformBytes = bytes;
    std::memcpy(command.uniformData, data, bytes);
}

} // namespace ion
