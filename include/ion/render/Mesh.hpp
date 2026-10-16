#pragma once

#include <ion/math/Geometry.hpp>
#include <ion/render/Buffer.hpp>
#include <ion/render/Vertex.hpp>

#include <cstdint>
#include <vector>

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
    AABB bounds; // local-space bounds of the vertex positions

    bool isValid() const {
        return vertexBuffer.isValid();
    }
};

// Uploads vertices and indices (32-bit source, stored as 16-bit when the
// vertex count allows, 32-bit otherwise) into a mesh and computes its
// bounds. Indices may be null to render unindexed.
Mesh createMesh(Renderer& renderer, const Vertex* vertices,
                uint32_t vertexCount, const uint32_t* indices,
                uint32_t indexCount);

void destroyMesh(Renderer& renderer, Mesh& mesh);

// Procedural meshes (centered on the origin, CCW front faces, with normals
// and UVs). Useful for prototyping and debug drawing.
Mesh createCubeMesh(Renderer& renderer, float size = 1.0f);
Mesh createPlaneMesh(Renderer& renderer, float width = 1.0f, float depth = 1.0f);
Mesh createSphereMesh(Renderer& renderer, float radius = 0.5f, uint32_t segments = 24,
                      uint32_t rings = 16);

// CPU-side geometry for the shapes above (vertices + 32-bit indices).
void buildCube(float size, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
void buildPlane(float width, float depth, std::vector<Vertex>& vertices,
                std::vector<uint32_t>& indices);
void buildSphere(float radius, uint32_t segments, uint32_t rings,
                 std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

// Recomputes smooth per-vertex normals from triangle faces (area-weighted).
void computeNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

} // namespace ion
