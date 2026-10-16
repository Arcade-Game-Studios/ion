#include <ion/render/Model.hpp>

#include <ion/core/Log.hpp>
#include <ion/render/Renderer.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ion {

void Model::destroy(Renderer& renderer) {
    // Parts can share a material texture (e.g. glTF primitives using the
    // same material); destroy each texture id only once.
    std::vector<uint64_t> destroyed;
    for (ModelPart& part : parts) {
        destroyMesh(renderer, part.mesh);
        Texture& texture = part.material.texture;
        if (texture.isValid() &&
            std::find(destroyed.begin(), destroyed.end(), texture.id) == destroyed.end()) {
            destroyed.push_back(texture.id);
            renderer.destroyTexture(texture);
        }
        texture = Texture();
    }
    parts.clear();
}

AABB Model::bounds() const {
    AABB result;
    bool any = false;
    for (const ModelPart& part : parts) {
        if (!part.mesh.isValid()) {
            continue;
        }
        AABB box = part.mesh.bounds.transformed(part.transform);
        result = any ? result.merged(box) : box;
        any = true;
    }
    return result;
}

uint32_t Model::triangleCount() const {
    uint32_t total = 0;
    for (const ModelPart& part : parts) {
        total += (part.mesh.indexCount > 0 ? part.mesh.indexCount : part.mesh.vertexCount) / 3;
    }
    return total;
}

bool loadModel(Renderer& renderer, const std::string& path, Model& out) {
    std::string lower = path;
    for (char& c : lower) {
        c = (char)std::tolower((unsigned char)c);
    }
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".obj") == 0) {
        return loadObjModel(renderer, path, out);
    }
    if (lower.size() >= 5 &&
        (lower.compare(lower.size() - 5, 5, ".gltf") == 0 ||
         lower.compare(lower.size() - 4, 4, ".glb") == 0)) {
        return loadGltfModel(renderer, path, out);
    }
    ION_LOG_ERROR("Model: unsupported format for '%s' (expected .obj, .gltf "
                  "or .glb)",
                  path.c_str());
    return false;
}

} // namespace ion
