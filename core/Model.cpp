#include <ion/render/Model.hpp>

#include <ion/core/Log.hpp>
#include <ion/render/Renderer.hpp>

#include <cctype>
#include <string>

namespace ion {

void Model::destroy(Renderer& renderer) {
    for (ModelPart& part : parts) {
        destroyMesh(renderer, part.mesh);
        if (part.material.texture.isValid()) {
            renderer.destroyTexture(part.material.texture);
            part.material.texture = Texture();
        }
    }
    parts.clear();
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
