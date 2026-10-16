#include <ion/render/Mesh.hpp>

#include <ion/core/Log.hpp>
#include <ion/render/Renderer.hpp>

#include <cmath>
#include <vector>

namespace ion {

Mesh createMesh(Renderer& renderer, const Vertex* vertices,
                uint32_t vertexCount, const uint32_t* indices,
                uint32_t indexCount) {
    Mesh mesh;
    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;

    if (vertexCount > 0 && vertices) {
        mesh.bounds = AABB(vertices[0].position, vertices[0].position);
        for (uint32_t i = 1; i < vertexCount; i++) {
            mesh.bounds = mesh.bounds.expandedToInclude(vertices[i].position);
        }
        mesh.vertexBuffer = renderer.createVertexBuffer(
            vertexCount * sizeof(Vertex), vertices);
    }
    if (indexCount > 0 && indices) {
        if (vertexCount <= 65536) {
            // Every index fits in 16 bits: halve the index buffer size.
            std::vector<uint16_t> packed(indexCount);
            for (uint32_t i = 0; i < indexCount; i++) {
                packed[i] = (uint16_t)indices[i];
            }
            mesh.indexBuffer =
                renderer.createIndexBuffer(indexCount, true, packed.data());
        } else {
            // Large meshes keep 32-bit indices (previously their indices
            // were dropped and the mesh rendered as garbage).
            mesh.indexBuffer = renderer.createIndexBuffer(indexCount, false, indices);
        }
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

namespace ion {

namespace {

Vertex makeVertex(const Vector3& p, const Vector3& n, const Vector2& uv) {
    Vertex v;
    v.position = p;
    v.normal = n;
    v.uv = uv;
    v.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    return v;
}

} // namespace

void buildCube(float size, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();
    float h = size * 0.5f;
    // normal, then the face's "right" and "up" axes (right x up == normal).
    const Vector3 faces[6][3] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    for (const auto& f : faces) {
        uint32_t base = (uint32_t)vertices.size();
        Vector3 n = f[0], r = f[1], u = f[2];
        vertices.push_back(makeVertex((n - r - u) * h, n, {0, 1}));
        vertices.push_back(makeVertex((n + r - u) * h, n, {1, 1}));
        vertices.push_back(makeVertex((n + r + u) * h, n, {1, 0}));
        vertices.push_back(makeVertex((n - r + u) * h, n, {0, 0}));
        uint32_t quad[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        indices.insert(indices.end(), quad, quad + 6);
    }
}

void buildPlane(float width, float depth, std::vector<Vertex>& vertices,
                std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();
    float hw = width * 0.5f, hd = depth * 0.5f;
    Vector3 up(0, 1, 0);
    vertices.push_back(makeVertex({-hw, 0, hd}, up, {0, 1}));
    vertices.push_back(makeVertex({hw, 0, hd}, up, {1, 1}));
    vertices.push_back(makeVertex({hw, 0, -hd}, up, {1, 0}));
    vertices.push_back(makeVertex({-hw, 0, -hd}, up, {0, 0}));
    indices = {0, 1, 2, 0, 2, 3};
}

void buildSphere(float radius, uint32_t segments, uint32_t rings,
                 std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    vertices.clear();
    indices.clear();
    segments = segments < 3 ? 3 : segments;
    rings = rings < 2 ? 2 : rings;
    const float pi = 3.14159265358979f;
    for (uint32_t r = 0; r <= rings; r++) {
        float v = (float)r / (float)rings;
        float phi = v * pi; // 0 at the north pole
        for (uint32_t s = 0; s <= segments; s++) {
            float u = (float)s / (float)segments;
            float theta = u * 2.0f * pi;
            Vector3 n(std::sin(phi) * std::sin(theta), std::cos(phi),
                      std::sin(phi) * std::cos(theta));
            vertices.push_back(makeVertex(n * radius, n, {u, v}));
        }
    }
    uint32_t stride = segments + 1;
    for (uint32_t r = 0; r < rings; r++) {
        for (uint32_t s = 0; s < segments; s++) {
            uint32_t a = r * stride + s;
            uint32_t b = a + stride;
            uint32_t quad[6] = {a, b, b + 1, a, b + 1, a + 1};
            indices.insert(indices.end(), quad, quad + 6);
        }
    }
}

void computeNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    for (Vertex& v : vertices) {
        v.normal = Vector3();
    }
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) {
            continue;
        }
        // Unnormalized cross product: longer for bigger triangles.
        Vector3 n = Vector3::cross(vertices[b].position - vertices[a].position,
                                   vertices[c].position - vertices[a].position);
        vertices[a].normal += n;
        vertices[b].normal += n;
        vertices[c].normal += n;
    }
    for (Vertex& v : vertices) {
        v.normal = v.normal.normalized();
    }
}

Mesh createCubeMesh(Renderer& renderer, float size) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    buildCube(size, vertices, indices);
    return createMesh(renderer, vertices.data(), (uint32_t)vertices.size(), indices.data(),
                      (uint32_t)indices.size());
}

Mesh createPlaneMesh(Renderer& renderer, float width, float depth) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    buildPlane(width, depth, vertices, indices);
    return createMesh(renderer, vertices.data(), (uint32_t)vertices.size(), indices.data(),
                      (uint32_t)indices.size());
}

Mesh createSphereMesh(Renderer& renderer, float radius, uint32_t segments, uint32_t rings) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    buildSphere(radius, segments, rings, vertices, indices);
    return createMesh(renderer, vertices.data(), (uint32_t)vertices.size(), indices.data(),
                      (uint32_t)indices.size());
}

} // namespace ion
