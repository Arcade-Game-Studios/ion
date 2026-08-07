#pragma once

#include <ion/math/Vector2.hpp>
#include <ion/math/Vector3.hpp>
#include <ion/math/Vector4.hpp>

namespace ion {

//
// Standard interleaved vertex format used by the renderer backends.
// Shaders must declare attributes in this order:
//   position (float3) at attribute 0, offset 0
//   color    (float4) at attribute 1, offset 12
//   uv       (float2) at attribute 2, offset 28
// Stride is sizeof(Vertex) = 36 bytes.
//
struct Vertex {
    Vector3 position;
    Vector4 color;
    Vector2 uv;
};

static_assert(sizeof(Vector3) == 12, "Vector3 must be 12 bytes");
static_assert(sizeof(Vector4) == 16, "Vector4 must be 16 bytes");
static_assert(sizeof(Vector2) == 8, "Vector2 must be 8 bytes");
static_assert(sizeof(Vertex) == 36, "Vertex must be tightly packed at 36 bytes");

} // namespace ion
