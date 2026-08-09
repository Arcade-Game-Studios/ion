#pragma once

#include <ion/math/Matrix4.hpp>
#include <ion/render/Material.hpp>
#include <ion/render/Mesh.hpp>

#include <string>
#include <vector>

namespace ion {

class Renderer;

//
// A model is a list of renderable parts. Each part owns its mesh and material
// and carries a local-to-world transform (from the source file's node
// hierarchy, if any).
//
struct ModelPart {
    Mesh mesh;
    Material material;
    Matrix4 transform = Matrix4::identity();
};

struct Model {
    std::vector<ModelPart> parts;

    // Releases all GPU resources owned by the model (meshes and textures).
    void destroy(Renderer& renderer);
};

// Loads a model from disk, dispatching on the file extension:
//   .obj  -> loadObjModel
//   .gltf / .glb -> loadGltfModel
bool loadModel(Renderer& renderer, const std::string& path, Model& out);

// Wavefront OBJ with optional MTL material sidecar (Kd color, d/Tr opacity,
// map_Kd texture). Materials and geometry are split into one part per
// material group.
bool loadObjModel(Renderer& renderer, const std::string& path, Model& out);

// glTF 2.0 (JSON or GLB) for static meshes: accessors, meshes/primitives,
// materials (pbrMetallicRoughness baseColorFactor + baseColorTexture), node
// transforms and scene traversal. Skinning, morph targets, animations and
// camera/light nodes are ignored.
bool loadGltfModel(Renderer& renderer, const std::string& path, Model& out);

} // namespace ion
