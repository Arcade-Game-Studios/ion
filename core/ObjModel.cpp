#include <ion/render/Model.hpp>

#include <ion/core/Log.hpp>
#include <ion/render/Image.hpp>
#include <ion/render/Renderer.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ion {

namespace {

struct ObjVertexRef {
    int32_t position = -1;
    int32_t uv = -1;
    int32_t normal = -1;
};

struct ObjTriangle {
    ObjVertexRef v[3];
};

struct ObjUsemtlEvent {
    std::string name;
    size_t firstTriangle = 0;  // triangle index where the material takes over
};

struct ObjData {
    std::vector<Vector3> positions;
    std::vector<Vector2> uvs;
    std::vector<Vector3> normals;
    std::vector<ObjTriangle> triangles;
    std::vector<std::string> materialLibraries;
    std::vector<ObjUsemtlEvent> usemtlEvents;
};

std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = s.find_last_not_of(" \t\r");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        parts.push_back(item);
    }
    return parts;
}

bool parseInt(const std::string& token, int32_t& value) {
    if (token.empty()) {
        return false;
    }
    const char* begin = token.c_str();
    char* end = nullptr;
    long parsed = std::strtol(begin, &end, 10);
    if (end == begin || *end != '\0') {
        return false;
    }
    value = (int32_t)parsed;
    return true;
}

// Resolves a possibly negative (relative) OBJ index to a 0-based index into a
// list of the given size. Returns false when out of range.
bool resolveIndex(int32_t index, size_t size, int32_t& out) {
    if (index == 0) {
        return false;
    }
    int64_t resolved = (index > 0) ? (int64_t)index - 1 : (int64_t)size + index;
    if (resolved < 0 || resolved >= (int64_t)size) {
        return false;
    }
    out = (int32_t)resolved;
    return true;
}

bool parseObjFile(const std::string& path, ObjData& data) {
    std::ifstream file(path);
    if (!file) {
        ION_LOG_ERROR("OBJ: cannot open '%s'", path.c_str());
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') {
            continue;
        }
        std::string command;
        std::stringstream ss(t);
        ss >> command;

        if (command == "v") {
            std::vector<std::string> toks = split(t, ' ');
            if (toks.size() >= 4) {
                data.positions.push_back(
                    Vector3((float)std::atof(toks[1].c_str()),
                            (float)std::atof(toks[2].c_str()),
                            (float)std::atof(toks[3].c_str())));
            }
        } else if (command == "vt") {
            std::vector<std::string> toks = split(t, ' ');
            if (toks.size() >= 3) {
                data.uvs.push_back(Vector2((float)std::atof(toks[1].c_str()),
                                           (float)std::atof(toks[2].c_str())));
            }
        } else if (command == "vn") {
            std::vector<std::string> toks = split(t, ' ');
            if (toks.size() >= 4) {
                Vector3 n((float)std::atof(toks[1].c_str()),
                          (float)std::atof(toks[2].c_str()),
                          (float)std::atof(toks[3].c_str()));
                float length = n.length();
                if (length > 1e-8f) {
                    data.normals.push_back(n / length);
                }
            }
        } else if (command == "mtllib") {
            std::string name = trim(t.substr(6));
            if (!name.empty()) {
                data.materialLibraries.push_back(name);
            }
        } else if (command == "usemtl") {
            ObjUsemtlEvent event;
            event.name = trim(t.substr(6));
            event.firstTriangle = data.triangles.size();
            data.usemtlEvents.push_back(event);
        } else if (command == "f") {
            std::vector<std::string> toks = split(t, ' ');
            std::vector<ObjVertexRef> face;
            for (size_t i = 1; i < toks.size(); i++) {
                std::vector<std::string> parts = split(toks[i], '/');
                ObjVertexRef ref;
                int32_t value = 0;
                if (parts.size() >= 1 && parseInt(parts[0], value) &&
                    resolveIndex(value, data.positions.size(), ref.position)) {
                    if (parts.size() >= 2 && !parts[1].empty() &&
                        parseInt(parts[1], value)) {
                        resolveIndex(value, data.uvs.size(), ref.uv);
                    }
                    if (parts.size() >= 3 && !parts[2].empty() &&
                        parseInt(parts[2], value)) {
                        resolveIndex(value, data.normals.size(), ref.normal);
                    }
                    face.push_back(ref);
                }
            }
            if (face.size() < 3) {
                continue;
            }
            // Fan triangulation.
            for (size_t i = 1; i + 1 < face.size(); i++) {
                ObjTriangle triangle;
                triangle.v[0] = face[0];
                triangle.v[1] = face[i];
                triangle.v[2] = face[i + 1];
                data.triangles.push_back(triangle);
            }
        }
        // Other commands (o, g, s) are ignored.
    }
    return true;
}

void loadMtllib(const std::string& path, const std::string& baseDir,
                Renderer& renderer,
                std::unordered_map<std::string, Material>& materials,
                std::vector<std::string>& materialOrder) {
    std::ifstream file(path);
    if (!file) {
        ION_LOG_WARN("MTL: cannot open '%s'", path.c_str());
        return;
    }
    std::string currentName;
    std::string line;
    while (std::getline(file, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') {
            continue;
        }
        std::string command;
        std::stringstream ss(t);
        ss >> command;

        if (command == "newmtl") {
            std::string name = trim(t.substr(6));
            if (name.empty()) {
                continue;
            }
            auto result = materials.emplace(name, Material());
            if (result.second) {
                result.first->second.name = name;
                currentName = name;
                materialOrder.push_back(name);
            }
        } else if (!currentName.empty()) {
            auto it = materials.find(currentName);
            if (it == materials.end()) {
                continue;
            }
            Material& current = it->second;
            if (command == "Kd") {
                std::vector<std::string> toks = split(t, ' ');
                if (toks.size() >= 4) {
                    current.baseColor.r = (float)std::atof(toks[1].c_str());
                    current.baseColor.g = (float)std::atof(toks[2].c_str());
                    current.baseColor.b = (float)std::atof(toks[3].c_str());
                }
            } else if (command == "d" || command == "Tr") {
                float value = (float)std::atof(t.substr(2).c_str());
                if (command == "Tr") {
                    value = 1.0f - value;
                }
                current.baseColor.a = value;
            } else if (command == "map_Kd") {
                std::string textureName = trim(t.substr(6));
                if (!textureName.empty()) {
                    std::string texturePath = baseDir + "/" + textureName;
                    uint32_t w = 0, h = 0;
                    std::vector<uint8_t> pixels;
                    if (loadImage(texturePath, w, h, pixels)) {
                        TextureDesc desc;
                        desc.width = w;
                        desc.height = h;
                        desc.format = TextureFormat::RGBA8;
                        desc.filterLinear = true;
                        desc.generateMipmaps = false;
                        current.texture =
                            renderer.createTexture(desc, pixels.data());
                    } else {
                        ION_LOG_WARN("MTL: cannot load texture '%s'",
                                     texturePath.c_str());
                    }
                }
            }
        }
    }
}

struct VertexKey {
    int32_t p, t, n;
    bool operator==(const VertexKey& other) const {
        return p == other.p && t == other.t && n == other.n;
    }
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& key) const {
        size_t h = (size_t)key.p * 73856093u;
        h ^= (size_t)key.t * 19349663u;
        h ^= (size_t)key.n * 83492791u;
        return h;
    }
};

Vector3 computeFaceNormal(const Vector3& a, const Vector3& b,
                          const Vector3& c) {
    Vector3 normal = Vector3::cross(b - a, c - a);
    float length = normal.length();
    if (length < 1e-8f) {
        return Vector3(0.0f, 1.0f, 0.0f);
    }
    return normal / length;
}

// Builds a mesh for the triangles in the given range [begin, end).
Mesh buildMeshForTriangles(Renderer& renderer, const ObjData& data,
                           const std::vector<ObjTriangle>& triangles) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexLookup;
    vertexLookup.reserve(triangles.size() * 3);

    for (const ObjTriangle& triangle : triangles) {
        // Flat (per-face) normal shared by this triangle's corners, used only
        // when the file did not author normals.
        Vector3 flatNormal = computeFaceNormal(
            data.positions[triangle.v[0].position],
            data.positions[triangle.v[1].position],
            data.positions[triangle.v[2].position]);

        uint32_t triIndex[3];
        for (int corner = 0; corner < 3; corner++) {
            const ObjVertexRef& ref = triangle.v[corner];
            // When normals are missing, key on the flat normal identity so
            // each triangle keeps its own corners (flat shading).
            int32_t normalKey = (ref.normal >= 0)
                                    ? ref.normal
                                    : -(1000000 + (int)triangles.size() -
                                        (int)(&triangle - triangles.data()));
            VertexKey key = {ref.position, ref.uv, normalKey};
            auto found = vertexLookup.find(key);
            uint32_t index;
            if (found != vertexLookup.end()) {
                index = found->second;
            } else {
                Vertex vertex;
                vertex.position = data.positions[ref.position];
                vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};
                vertex.uv = (ref.uv >= 0) ? data.uvs[ref.uv] : Vector2(0, 0);
                vertex.normal =
                    (ref.normal >= 0) ? data.normals[ref.normal] : flatNormal;
                index = (uint32_t)vertices.size();
                vertices.push_back(vertex);
                vertexLookup[key] = index;
            }
            triIndex[corner] = index;
        }
        indices.push_back(triIndex[0]);
        indices.push_back(triIndex[1]);
        indices.push_back(triIndex[2]);
    }
    return createMesh(renderer, vertices.data(), (uint32_t)vertices.size(),
                      indices.data(), (uint32_t)indices.size());
}

}  // namespace

bool loadObjModel(Renderer& renderer, const std::string& path, Model& out) {
    ObjData data;
    if (!parseObjFile(path, data)) {
        return false;
    }
    if (data.positions.empty()) {
        ION_LOG_ERROR("OBJ: '%s' contains no vertices", path.c_str());
        return false;
    }

    std::string baseDir = path;
    size_t slash = baseDir.find_last_of("/\\");
    if (slash != std::string::npos) {
        baseDir = baseDir.substr(0, slash);
    } else {
        baseDir = ".";
    }

    std::unordered_map<std::string, Material> materials;
    std::vector<std::string> materialOrder;
    for (const std::string& library : data.materialLibraries) {
        std::string fullPath = library;
        if (baseDir != ".") {
            fullPath = baseDir + "/" + library;
        }
        loadMtllib(fullPath, baseDir, renderer, materials, materialOrder);
    }
    if (materials.empty()) {
        auto result = materials.emplace("default", Material());
        result.first->second.name = "default";
        materialOrder.push_back("default");
    }

    // Resolve usemtl events to material indices over triangle ranges.
    std::unordered_map<std::string, int> materialIndexByName;
    for (size_t i = 0; i < materialOrder.size(); i++) {
        materialIndexByName[materialOrder[i]] = (int)i;
    }
    std::vector<int> triangleMaterials(data.triangles.size(), 0);
    for (size_t e = 0; e < data.usemtlEvents.size(); e++) {
        const ObjUsemtlEvent& event = data.usemtlEvents[e];
        auto found = materialIndexByName.find(event.name);
        if (found == materialIndexByName.end()) {
            continue;
        }
        size_t end = (e + 1 < data.usemtlEvents.size())
                         ? data.usemtlEvents[e + 1].firstTriangle
                         : data.triangles.size();
        for (size_t i = event.firstTriangle; i < end && i < data.triangles.size();
             i++) {
            triangleMaterials[i] = found->second;
        }
    }

    out.parts.clear();
    out.parts.reserve(materialOrder.size());
    std::vector<std::vector<const ObjTriangle*>> facesByMaterial(
        materialOrder.size());
    for (size_t i = 0; i < data.triangles.size(); i++) {
        int m = triangleMaterials[i];
        if (m >= 0 && m < (int)facesByMaterial.size()) {
            facesByMaterial[m].push_back(&data.triangles[i]);
        }
    }

    for (size_t m = 0; m < materialOrder.size(); m++) {
        if (facesByMaterial[m].empty()) {
            continue;
        }
        std::vector<ObjTriangle> faces;
        faces.reserve(facesByMaterial[m].size());
        for (const ObjTriangle* triangle : facesByMaterial[m]) {
            faces.push_back(*triangle);
        }
        ModelPart part;
        part.mesh = buildMeshForTriangles(renderer, data, faces);
        part.material = materials[materialOrder[m]];
        out.parts.push_back(std::move(part));
    }

    ION_LOG_INFO("OBJ: loaded '%s' (%zu parts, %zu triangles)", path.c_str(),
                 out.parts.size(), data.triangles.size());
    return !out.parts.empty();
}

}  // namespace ion
