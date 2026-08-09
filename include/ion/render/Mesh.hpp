#pragma once

#include <ion/render/Buffer.hpp>
#include <ion/render/Vertex.hpp>

#include <cstdint>

namespace ion {

class Renderer;

//
// A renderable mesh: an interleaved vertex buffer plus an optional index
// buffer. Meshes are created through createMesh() and released with
// destroyMesh(). The mesh does not own shaders or materials.
//
struct Mesh {
    VertexBuffer vertexBuffer;
    IndexBuffer indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    bool isValid() const {
        return vertexBuffer.isValid();
    }
};

// Uploads vertices and indices (32-bit source, stored as 16-bit when the
// vertex count allows) into a mesh. Indices may be null to render unindexed.
Mesh createMesh(Renderer& renderer, const Vertex* vertices,
                uint32_t vertexCount, const uint32_t* indices,
                uint32_t indexCount);

void destroyMesh(Renderer& renderer, Mesh& mesh);

} // namespace ion
