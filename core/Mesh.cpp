#include <ion/render/Mesh.hpp>

#include <ion/core/Log.hpp>
#include <ion/render/Renderer.hpp>

#include <vector>

namespace ion {

Mesh createMesh(Renderer& renderer, const Vertex* vertices,
                uint32_t vertexCount, const uint32_t* indices,
                uint32_t indexCount) {
    Mesh mesh;
    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;

    if (vertexCount > 0 && vertices) {
        mesh.vertexBuffer = renderer.createVertexBuffer(
            vertexCount * sizeof(Vertex), vertices);
    }
    if (indexCount > 0 && indices) {
        if (vertexCount > 65535) {
            ION_LOG_ERROR("Mesh: vertex count %u exceeds 16-bit index range; "
                          "indices skipped",
                          vertexCount);
            return mesh;
        }
        std::vector<uint16_t> packed(indexCount);
        for (uint32_t i = 0; i < indexCount; i++) {
            packed[i] = (uint16_t)indices[i];
        }
        mesh.indexBuffer =
            renderer.createIndexBuffer(indexCount, true, packed.data());
    }
    return mesh;
}

void destroyMesh(Renderer& renderer, Mesh& mesh) {
    if (mesh.vertexBuffer.isValid()) {
        renderer.destroyVertexBuffer(mesh.vertexBuffer);
    }
    if (mesh.indexBuffer.isValid()) {
        renderer.destroyIndexBuffer(mesh.indexBuffer);
    }
    mesh = Mesh();
}

} // namespace ion
