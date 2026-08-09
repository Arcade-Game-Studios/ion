#include <ion/render/Model.hpp>

#include <ion/core/Log.hpp>
#include <ion/render/Image.hpp>
#include <ion/render/Renderer.hpp>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ion {

// ---------------------------------------------------------------------------
// Minimal JSON parser (glTF documents are plain JSON).
// ---------------------------------------------------------------------------
namespace {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool isNull() const { return type == Type::Null; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    double asNumber(double def = 0.0) const {
        return type == Type::Number ? number : def;
    }

    std::string asString(const std::string& def = "") const {
        return type == Type::String ? string : def;
    }

    // Object member lookup; returns nullptr when missing or not an object.
    const JsonValue* get(const std::string& key) const {
        if (type != Type::Object) {
            return nullptr;
        }
        for (const auto& member : object) {
            if (member.first == key) {
                return &member.second;
            }
        }
        return nullptr;
    }

    const JsonValue* at(size_t index) const {
        if (type != Type::Array || index >= array.size()) {
            return nullptr;
        }
        return &array[index];
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    JsonValue parse() {
        pos_ = 0;
        skipWhitespace();
        JsonValue value = parseValue();
        return value;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    void skipWhitespace() {
        while (pos_ < text_.size() &&
               std::isspace((unsigned char)text_[pos_])) {
            pos_++;
        }
    }

    bool consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            pos_++;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!consume(c)) {
            throw std::runtime_error("expected character");
        }
    }

    JsonValue parseValue() {
        skipWhitespace();
        if (pos_ >= text_.size()) {
            throw std::runtime_error("unexpected end");
        }
        char c = text_[pos_];
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': {
                JsonValue value;
                value.type = JsonValue::Type::String;
                value.string = parseString();
                return value;
            }
            case 't': {
                expect('t');
                expect('r');
                expect('u');
                expect('e');
                JsonValue value;
                value.type = JsonValue::Type::Bool;
                value.boolean = true;
                return value;
            }
            case 'f': {
                expect('f');
                expect('a');
                expect('l');
                expect('s');
                expect('e');
                JsonValue value;
                value.type = JsonValue::Type::Bool;
                value.boolean = false;
                return value;
            }
            case 'n': {
                expect('n');
                expect('u');
                expect('l');
                expect('l');
                return JsonValue();
            }
            default: {
                if (c == '-' || (c >= '0' && c <= '9')) {
                    JsonValue value;
                    value.type = JsonValue::Type::Number;
                    value.number = parseNumber();
                    return value;
                }
                throw std::runtime_error("unexpected token");
            }
        }
    }

    JsonValue parseObject() {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        expect('{');
        skipWhitespace();
        if (consume('}')) {
            return value;
        }
        while (true) {
            skipWhitespace();
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            JsonValue member = parseValue();
            value.object.emplace_back(std::move(key), std::move(member));
            skipWhitespace();
            if (consume('}')) {
                break;
            }
            expect(',');
        }
        return value;
    }

    JsonValue parseArray() {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        expect('[');
        skipWhitespace();
        if (consume(']')) {
            return value;
        }
        while (true) {
            value.array.push_back(parseValue());
            skipWhitespace();
            if (consume(']')) {
                break;
            }
            expect(',');
        }
        return value;
    }

    void appendUtf8(std::string& out, uint32_t codepoint) {
        if (codepoint <= 0x7F) {
            out.push_back((char)codepoint);
        } else if (codepoint <= 0x7FF) {
            out.push_back((char)(0xC0 | (codepoint >> 6)));
            out.push_back((char)(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            out.push_back((char)(0xE0 | (codepoint >> 12)));
            out.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (codepoint & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (codepoint >> 18)));
            out.push_back((char)(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (codepoint & 0x3F)));
        }
    }

    uint32_t parseHex4() {
        if (pos_ + 4 > text_.size()) {
            throw std::runtime_error("bad unicode escape");
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; i++) {
            char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= (uint32_t)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= (uint32_t)(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= (uint32_t)(c - 'A' + 10);
            } else {
                throw std::runtime_error("bad hex digit");
            }
        }
        return value;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    break;
                }
                char escape = text_[pos_++];
                switch (escape) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t code = parseHex4();
                        if (code >= 0xD800 && code <= 0xDBFF &&
                            pos_ + 1 < text_.size() && text_[pos_] == '\\' &&
                            text_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            uint32_t low = parseHex4();
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                code =
                                    0x10000 + ((code - 0xD800) << 10) +
                                    (low - 0xDC00);
                            }
                        }
                        appendUtf8(out, code);
                        break;
                    }
                    default: throw std::runtime_error("bad escape");
                }
            } else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("unterminated string");
    }

    double parseNumber() {
        size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') {
            pos_++;
        }
        while (pos_ < text_.size() &&
               (std::isdigit((unsigned char)text_[pos_]))) {
            pos_++;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            pos_++;
            while (pos_ < text_.size() &&
                   std::isdigit((unsigned char)text_[pos_])) {
                pos_++;
            }
        }
        if (pos_ < text_.size() &&
            (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            pos_++;
            if (pos_ < text_.size() &&
                (text_[pos_] == '+' || text_[pos_] == '-')) {
                pos_++;
            }
            while (pos_ < text_.size() &&
                   std::isdigit((unsigned char)text_[pos_])) {
                pos_++;
            }
        }
        return std::strtod(text_.c_str() + start, nullptr);
    }
};

bool parseJsonText(const std::string& text, JsonValue& out) {
    try {
        out = JsonParser(text).parse();
        return out.isObject();
    } catch (const std::exception&) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// glTF document model.
// ---------------------------------------------------------------------------

struct GltfBuffer {
    std::vector<uint8_t> data;
};

struct GltfBufferView {
    int buffer = -1;
    size_t byteOffset = 0;
    size_t byteLength = 0;
    size_t byteStride = 0;  // 0 when tightly packed
};

struct GltfAccessor {
    int bufferView = -1;
    size_t byteOffset = 0;
    int componentType = 5126;  // FLOAT
    std::string type = "SCALAR";
    size_t count = 0;
    size_t componentCount = 1;
    size_t componentSize = 4;
    size_t elementSize = 4;
    bool normalized = false;
};

struct GltfImage {
    int bufferView = -1;
    std::string mimeType;
    std::string uri;
    std::vector<uint8_t> decoded;  // pixels when loadImageFromMemory succeeded
    uint32_t width = 0;
    uint32_t height = 0;
    bool loaded = false;
};

struct GltfDocument {
    std::vector<GltfBuffer> buffers;
    std::vector<GltfBufferView> bufferViews;
    std::vector<GltfAccessor> accessors;
    std::vector<GltfImage> images;
    std::vector<int> imageByTexture;   // texture index -> image index
    std::vector<Material> materials;
    std::vector<int> rootNodes;
    bool valid = false;
};

int componentSizeOf(int componentType) {
    switch (componentType) {
        case 5120: return 1;  // BYTE
        case 5121: return 1;  // UNSIGNED_BYTE
        case 5122: return 2;  // SHORT
        case 5123: return 2;  // UNSIGNED_SHORT
        case 5125: return 4;  // UNSIGNED_INT
        case 5126: return 4;  // FLOAT
        default: return 0;
    }
}

size_t componentCountOf(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    return 0;
}

bool base64Decode(const std::string& input, std::vector<uint8_t>& output) {
    static int8_t table[256];
    static bool initialized = false;
    if (!initialized) {
        memset(table, -1, sizeof(table));
        for (int i = 0; i < 26; i++) {
            table['A' + i] = (int8_t)i;
            table['a' + i] = (int8_t)(26 + i);
        }
        for (int i = 0; i < 10; i++) {
            table['0' + i] = (int8_t)(52 + i);
        }
        table['+'] = 62;
        table['/'] = 63;
        initialized = true;
    }
    output.clear();
    uint32_t accumulator = 0;
    int bits = 0;
    for (char c : input) {
        if (std::isspace((unsigned char)c)) {
            continue;
        }
        if (c == '=') {
            break;
        }
        int8_t value = table[(unsigned char)c];
        if (value < 0) {
            return false;
        }
        accumulator = (accumulator << 6) | (uint32_t)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back((uint8_t)((accumulator >> bits) & 0xFF));
        }
    }
    return true;
}

bool readBinaryFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file),
               std::istreambuf_iterator<char>());
    return true;
}

std::string baseDirectory(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return ".";
    }
    return path.substr(0, slash);
}

// Reads a single accessor as floats. Supports FLOAT plus normalized integer
// component types. Returns false on invalid accessor.
bool readAccessorFloats(const GltfDocument& doc, int accessorIndex,
                        float* output) {
    if (accessorIndex < 0 ||
        accessorIndex >= (int)doc.accessors.size()) {
        return false;
    }
    const GltfAccessor& accessor = doc.accessors[accessorIndex];
    if (accessor.count == 0) {
        return true;
    }
    const GltfBufferView* view = nullptr;
    const uint8_t* base = nullptr;
    if (accessor.bufferView >= 0) {
        if (accessor.bufferView >= (int)doc.bufferViews.size()) {
            return false;
        }
        view = &doc.bufferViews[accessor.bufferView];
        if (view->buffer < 0 || view->buffer >= (int)doc.buffers.size()) {
            return false;
        }
        base = doc.buffers[view->buffer].data.data() + view->byteOffset +
               accessor.byteOffset;
    }

    size_t stride = accessor.elementSize;
    if (view && view->byteStride > 0) {
        stride = view->byteStride;
    }
    size_t componentSize = accessor.componentSize;
    size_t components = accessor.componentCount;

    for (size_t i = 0; i < accessor.count; i++) {
        const uint8_t* src = base + i * stride;
        for (size_t c = 0; c < components; c++) {
            float value = 0.0f;
            switch (accessor.componentType) {
                case 5126:  // FLOAT
                    std::memcpy(&value, src + c * componentSize, 4);
                    break;
                case 5121: {  // UNSIGNED_BYTE
                    uint8_t v = src[c];
                    value = accessor.normalized ? v / 255.0f : (float)v;
                    break;
                }
                case 5120: {  // BYTE
                    int8_t v = (int8_t)src[c];
                    value = accessor.normalized
                                ? std::max((float)v / 127.0f, -1.0f)
                                : (float)v;
                    break;
                }
                case 5123: {  // UNSIGNED_SHORT
                    uint16_t v;
                    std::memcpy(&v, src + c * componentSize, 2);
                    value = accessor.normalized ? v / 65535.0f : (float)v;
                    break;
                }
                case 5122: {  // SHORT
                    int16_t v;
                    std::memcpy(&v, src + c * componentSize, 2);
                    value = accessor.normalized
                                ? std::max((float)v / 32767.0f, -1.0f)
                                : (float)v;
                    break;
                }
                default:
                    ION_LOG_WARN("glTF: unsupported component type %d",
                                 accessor.componentType);
                    return false;
            }
            *output++ = value;
        }
    }
    return true;
}

// Reads an index accessor as 32-bit indices.
bool readAccessorIndices(const GltfDocument& doc, int accessorIndex,
                         std::vector<uint32_t>& output) {
    if (accessorIndex < 0 ||
        accessorIndex >= (int)doc.accessors.size()) {
        return false;
    }
    const GltfAccessor& accessor = doc.accessors[accessorIndex];
    if (accessor.type != "SCALAR") {
        return false;
    }
    if (accessor.bufferView < 0 ||
        accessor.bufferView >= (int)doc.bufferViews.size()) {
        return false;
    }
    const GltfBufferView& view = doc.bufferViews[accessor.bufferView];
    if (view.buffer < 0 || view.buffer >= (int)doc.buffers.size()) {
        return false;
    }
    const uint8_t* base = doc.buffers[view.buffer].data.data() +
                          view.byteOffset + accessor.byteOffset;
    size_t stride = accessor.elementSize;
    if (view.byteStride > 0) {
        stride = view.byteStride;
    }
    output.resize(accessor.count);
    for (size_t i = 0; i < accessor.count; i++) {
        const uint8_t* src = base + i * stride;
        switch (accessor.componentType) {
            case 5121: output[i] = src[0]; break;
            case 5123: {
                uint16_t v;
                std::memcpy(&v, src, 2);
                output[i] = v;
                break;
            }
            case 5125: {
                uint32_t v;
                std::memcpy(&v, src, 4);
                output[i] = v;
                break;
            }
            default:
                return false;
        }
    }
    return true;
}

bool parseGltfJson(const JsonValue& root, const std::string& baseDir,
                   GltfDocument& doc) {
    // Buffers.
    if (const JsonValue* buffers = root.get("buffers")) {
        for (const JsonValue& buffer : buffers->array) {
            GltfBuffer gltfBuffer;
            if (const JsonValue* uri = buffer.get("uri")) {
                std::string uriValue = uri->asString();
                const std::string prefix = "data:";
                if (uriValue.compare(0, prefix.size(), prefix) == 0) {
                    size_t comma = uriValue.find(";base64,");
                    if (comma == std::string::npos) {
                        ION_LOG_WARN("glTF: unsupported data URI encoding");
                        doc.valid = false;
                        return false;
                    }
                    std::string encoded =
                        uriValue.substr(comma + 8);  // after ";base64,"
                    if (!base64Decode(encoded, gltfBuffer.data)) {
                        ION_LOG_WARN("glTF: failed to decode embedded buffer");
                        doc.valid = false;
                        return false;
                    }
                } else {
                    std::string externalPath = uriValue;
                    if (baseDir != ".") {
                        externalPath = baseDir + "/" + uriValue;
                    }
                    if (!readBinaryFile(externalPath, gltfBuffer.data)) {
                        ION_LOG_WARN("glTF: cannot read buffer '%s'",
                                     externalPath.c_str());
                        doc.valid = false;
                        return false;
                    }
                }
            }
            doc.buffers.push_back(std::move(gltfBuffer));
        }
    }

    // Buffer views.
    if (const JsonValue* views = root.get("bufferViews")) {
        for (const JsonValue& view : views->array) {
            GltfBufferView gltfView;
            const JsonValue* bufferIndex = view.get("buffer");
            gltfView.buffer = bufferIndex ? (int)bufferIndex->asNumber(-1) : -1;
            const JsonValue* viewOffset = view.get("byteOffset");
            gltfView.byteOffset =
                viewOffset ? (size_t)viewOffset->asNumber(0) : 0;
            const JsonValue* viewLength = view.get("byteLength");
            gltfView.byteLength =
                viewLength ? (size_t)viewLength->asNumber(0) : 0;
            const JsonValue* stride = view.get("byteStride");
            gltfView.byteStride = stride ? (size_t)stride->asNumber(0) : 0;
            doc.bufferViews.push_back(gltfView);
        }
    }

    // Accessors.
    if (const JsonValue* accessors = root.get("accessors")) {
        for (const JsonValue& accessor : accessors->array) {
            GltfAccessor gltfAccessor;
            if (const JsonValue* view = accessor.get("bufferView")) {
                gltfAccessor.bufferView = (int)view->asNumber(-1);
            }
            if (const JsonValue* offset = accessor.get("byteOffset")) {
                gltfAccessor.byteOffset = (size_t)offset->asNumber(0);
            }
            if (const JsonValue* component = accessor.get("componentType")) {
                gltfAccessor.componentType = (int)component->asNumber(5126);
            }
            if (const JsonValue* type = accessor.get("type")) {
                gltfAccessor.type = type->asString("SCALAR");
            }
            if (const JsonValue* count = accessor.get("count")) {
                gltfAccessor.count = (size_t)count->asNumber(0);
            }
            gltfAccessor.normalized = accessor.get("normalized") != nullptr;
            gltfAccessor.componentCount = componentCountOf(gltfAccessor.type);
            gltfAccessor.componentSize =
                componentSizeOf(gltfAccessor.componentType);
            gltfAccessor.elementSize =
                gltfAccessor.componentCount * gltfAccessor.componentSize;
            if (gltfAccessor.componentCount == 0 ||
                gltfAccessor.componentSize == 0) {
                ION_LOG_WARN("glTF: unsupported accessor (type '%s', component "
                             "%d)",
                             gltfAccessor.type.c_str(),
                             gltfAccessor.componentType);
                doc.valid = false;
                return false;
            }
            if (accessor.get("sparse") != nullptr) {
                ION_LOG_WARN("glTF: sparse accessors are not supported");
            }
            doc.accessors.push_back(gltfAccessor);
        }
    }
    return true;
}

}  // namespace

bool loadGltfModel(Renderer& renderer, const std::string& path, Model& out) {
    std::vector<uint8_t> fileData;
    if (!readBinaryFile(path, fileData)) {
        ION_LOG_ERROR("glTF: cannot open '%s'", path.c_str());
        return false;
    }

    // GLB container: 12-byte header (magic, version, length) then chunks of
    // { length, type, data }. JSON chunk type is 0x4E4F534A, BIN is 0x004E4942.
    bool isGlb = fileData.size() >= 12 &&
                 std::memcmp(fileData.data(), "glTF", 4) == 0;
    std::string jsonText;
    std::vector<uint8_t> binChunk;
    if (isGlb) {
        size_t pos = 12;
        while (pos + 8 <= fileData.size()) {
            uint32_t length;
            uint32_t type;
            std::memcpy(&length, fileData.data() + pos, 4);
            std::memcpy(&type, fileData.data() + pos + 4, 4);
            pos += 8;
            if (pos + length > fileData.size()) {
                break;
            }
            if (type == 0x4E4F534Au) {  // "JSON"
                jsonText.assign((const char*)fileData.data() + pos, length);
            } else if (type == 0x004E4942u) {  // "BIN\0"
                binChunk.assign(fileData.data() + pos,
                                fileData.data() + pos + length);
            }
            pos += length;
        }
        if (jsonText.empty()) {
            ION_LOG_ERROR("glTF: GLB contains no JSON chunk");
            return false;
        }
    } else {
        jsonText.assign((const char*)fileData.data(), fileData.size());
    }

    JsonValue root;
    if (!parseJsonText(jsonText, root)) {
        ION_LOG_ERROR("glTF: failed to parse JSON in '%s'", path.c_str());
        return false;
    }

    std::string baseDir = baseDirectory(path);

    GltfDocument doc;
    if (!parseGltfJson(root, baseDir, doc)) {
        return false;
    }
    if (isGlb) {
        // In GLB, buffer 0 is the BIN chunk (the JSON references it but
        // declares no uri).
        if (!doc.buffers.empty()) {
            doc.buffers[0].data = std::move(binChunk);
        } else {
            GltfBuffer glbBuffer;
            glbBuffer.data = std::move(binChunk);
            doc.buffers.push_back(std::move(glbBuffer));
        }
    }

    // Images.
    if (const JsonValue* images = root.get("images")) {
        for (const JsonValue& image : images->array) {
            GltfImage gltfImage;
            if (const JsonValue* view = image.get("bufferView")) {
                gltfImage.bufferView = (int)view->asNumber(-1);
            }
            if (const JsonValue* mime = image.get("mimeType")) {
                gltfImage.mimeType = mime->asString();
            }
            if (const JsonValue* uri = image.get("uri")) {
                gltfImage.uri = uri->asString();
            }
            doc.images.push_back(std::move(gltfImage));
        }
    }

    // Decode images (PNG only).
    for (GltfImage& image : doc.images) {
        std::vector<uint8_t> encoded;
        if (image.bufferView >= 0 &&
            image.bufferView < (int)doc.bufferViews.size()) {
            const GltfBufferView& view = doc.bufferViews[image.bufferView];
            if (view.buffer >= 0 && view.buffer < (int)doc.buffers.size() &&
                view.byteOffset + view.byteLength <=
                    doc.buffers[view.buffer].data.size()) {
                encoded.assign(
                    doc.buffers[view.buffer].data.begin() +
                        view.byteOffset,
                    doc.buffers[view.buffer].data.begin() +
                        view.byteOffset + view.byteLength);
            }
        } else if (!image.uri.empty()) {
            const std::string prefix = "data:";
            if (image.uri.compare(0, prefix.size(), prefix) == 0) {
                size_t comma = image.uri.find(";base64,");
                if (comma != std::string::npos) {
                    base64Decode(image.uri.substr(comma + 8), encoded);
                }
            } else {
                std::string externalPath = baseDir + "/" + image.uri;
                readBinaryFile(externalPath, encoded);
            }
        }
        if (!encoded.empty()) {
            uint32_t w = 0, h = 0;
            if (loadImageFromMemory(encoded.data(), encoded.size(), w, h,
                                    image.decoded)) {
                image.width = w;
                image.height = h;
                image.loaded = true;
            } else {
                ION_LOG_WARN("glTF: unsupported or corrupt image (mime '%s')",
                             image.mimeType.c_str());
            }
        }
    }

    // Textures -> images.
    if (const JsonValue* textures = root.get("textures")) {
        for (const JsonValue& texture : textures->array) {
            const JsonValue* source = texture.get("source");
            doc.imageByTexture.push_back(source ? (int)source->asNumber(-1)
                                                : -1);
        }
    }

    // Materials.
    if (const JsonValue* materials = root.get("materials")) {
        for (const JsonValue& material : materials->array) {
            Material mat;
            if (const JsonValue* name = material.get("name")) {
                mat.name = name->asString();
            }
            if (const JsonValue* pbr = material.get("pbrMetallicRoughness")) {
                if (const JsonValue* factor = pbr->get("baseColorFactor")) {
                    if (factor->isArray() && factor->array.size() >= 4) {
                        mat.baseColor = Color(
                            (float)factor->at(0)->asNumber(1.0),
                            (float)factor->at(1)->asNumber(1.0),
                            (float)factor->at(2)->asNumber(1.0),
                            (float)factor->at(3)->asNumber(1.0));
                    }
                }
                if (const JsonValue* metallic = pbr->get("metallicFactor")) {
                    mat.metallic = (float)metallic->asNumber(0.0);
                }
                if (const JsonValue* roughness = pbr->get("roughnessFactor")) {
                    mat.roughness = (float)roughness->asNumber(1.0);
                }
                if (const JsonValue* baseTexture =
                        pbr->get("baseColorTexture")) {
                    if (const JsonValue* index = baseTexture->get("index")) {
                        int textureIndex = (int)index->asNumber(-1);
                        if (textureIndex >= 0 &&
                            textureIndex < (int)doc.imageByTexture.size()) {
                            int imageIndex = doc.imageByTexture[textureIndex];
                            if (imageIndex >= 0 &&
                                imageIndex < (int)doc.images.size() &&
                                doc.images[imageIndex].loaded) {
                                GltfImage& gltfImage =
                                    doc.images[imageIndex];
                                TextureDesc desc;
                                desc.width = gltfImage.width;
                                desc.height = gltfImage.height;
                                desc.format = TextureFormat::RGBA8;
                                desc.filterLinear = true;
                                desc.generateMipmaps = false;
                                mat.texture = renderer.createTexture(
                                    desc, gltfImage.decoded.data());
                            }
                        }
                    }
                }
            }
            doc.materials.push_back(std::move(mat));
        }
    }

    // Meshes.
    struct GltfPrimitive {
        int position = -1;
        int normal = -1;
        int uv = -1;
        int indices = -1;
        int material = -1;
        int mode = 4;
    };
    struct GltfMeshData {
        std::vector<GltfPrimitive> primitives;
    };
    std::vector<GltfMeshData> meshes;
    if (const JsonValue* meshArray = root.get("meshes")) {
        for (const JsonValue& mesh : meshArray->array) {
            GltfMeshData meshData;
            if (const JsonValue* primitives = mesh.get("primitives")) {
                for (const JsonValue& primitive : primitives->array) {
                    GltfPrimitive prim;
                    if (const JsonValue* attributes = primitive.get("attributes")) {
                        if (const JsonValue* pos = attributes->get("POSITION")) {
                            prim.position = (int)pos->asNumber(-1);
                        }
                        if (const JsonValue* normal = attributes->get("NORMAL")) {
                            prim.normal = (int)normal->asNumber(-1);
                        }
                        if (const JsonValue* uv = attributes->get("TEXCOORD_0")) {
                            prim.uv = (int)uv->asNumber(-1);
                        }
                    }
                    if (const JsonValue* indices = primitive.get("indices")) {
                        prim.indices = (int)indices->asNumber(-1);
                    }
                    if (const JsonValue* material = primitive.get("material")) {
                        prim.material = (int)material->asNumber(-1);
                    }
                    if (const JsonValue* mode = primitive.get("mode")) {
                        prim.mode = (int)mode->asNumber(4);
                    }
                    meshData.primitives.push_back(prim);
                }
            }
            meshes.push_back(std::move(meshData));
        }
    }

    // Nodes.
    struct GltfNodeData {
        int mesh = -1;
        Matrix4 transform = Matrix4::identity();
        std::vector<int> children;
    };
    std::vector<GltfNodeData> nodes;
    if (const JsonValue* nodeArray = root.get("nodes")) {
        for (const JsonValue& node : nodeArray->array) {
            GltfNodeData nodeData;
            if (const JsonValue* mesh = node.get("mesh")) {
                nodeData.mesh = (int)mesh->asNumber(-1);
            }
            if (const JsonValue* children = node.get("children")) {
                for (const JsonValue& child : children->array) {
                    nodeData.children.push_back((int)child.asNumber(-1));
                }
            }
            if (const JsonValue* matrix = node.get("matrix")) {
                // glTF matrices are column-major, matching Matrix4 storage.
                if (matrix->isArray() && matrix->array.size() >= 16) {
                    for (int i = 0; i < 16; i++) {
                        nodeData.transform.m[i] =
                            (float)matrix->at(i)->asNumber(0.0);
                    }
                }
            } else {
                Vector3 translation(0.0f, 0.0f, 0.0f);
                Vector3 scale(1.0f, 1.0f, 1.0f);
                Vector4 rotation(0.0f, 0.0f, 0.0f, 1.0f);  // x,y,z,w
                if (const JsonValue* t = node.get("translation")) {
                    if (t->isArray() && t->array.size() >= 3) {
                        translation = Vector3(
                            (float)t->at(0)->asNumber(0.0),
                            (float)t->at(1)->asNumber(0.0),
                            (float)t->at(2)->asNumber(0.0));
                    }
                }
                if (const JsonValue* s = node.get("scale")) {
                    if (s->isArray() && s->array.size() >= 3) {
                        scale = Vector3((float)s->at(0)->asNumber(1.0),
                                        (float)s->at(1)->asNumber(1.0),
                                        (float)s->at(2)->asNumber(1.0));
                    }
                }
                if (const JsonValue* r = node.get("rotation")) {
                    if (r->isArray() && r->array.size() >= 4) {
                        rotation = Vector4((float)r->at(0)->asNumber(0.0),
                                           (float)r->at(1)->asNumber(0.0),
                                           (float)r->at(2)->asNumber(0.0),
                                           (float)r->at(3)->asNumber(1.0));
                    }
                }
                // Compose T * R * S (column-major storage).
                Matrix4 mat = Matrix4::identity();
                float x = rotation.x, y = rotation.y, z = rotation.z,
                      w = rotation.w;
                mat(0, 0) = 1 - 2 * (y * y + z * z);
                mat(1, 0) = 2 * (x * y + z * w);
                mat(2, 0) = 2 * (x * z - y * w);
                mat(0, 1) = 2 * (x * y - z * w);
                mat(1, 1) = 1 - 2 * (x * x + z * z);
                mat(2, 1) = 2 * (y * z + x * w);
                mat(0, 2) = 2 * (x * z + y * w);
                mat(1, 2) = 2 * (y * z - x * w);
                mat(2, 2) = 1 - 2 * (x * x + y * y);
                mat(0, 3) = translation.x;
                mat(1, 3) = translation.y;
                mat(2, 3) = translation.z;
                for (int col = 0; col < 3; col++) {
                    mat(0, col) *= scale.x;
                    mat(1, col) *= scale.y;
                    mat(2, col) *= scale.z;
                }
                nodeData.transform = mat;
            }
            nodes.push_back(std::move(nodeData));
        }
    }

    // Scenes (first scene or "scene" property).
    if (const JsonValue* scenes = root.get("scenes")) {
        int sceneIndex = 0;
        if (const JsonValue* scene = root.get("scene")) {
            sceneIndex = (int)scene->asNumber(0);
        }
        if (sceneIndex >= 0 && sceneIndex < (int)scenes->array.size()) {
            const JsonValue& scene = scenes->array[sceneIndex];
            if (const JsonValue* sceneNodes = scene.get("nodes")) {
                for (const JsonValue& nodeIndex : sceneNodes->array) {
                    doc.rootNodes.push_back((int)nodeIndex.asNumber(-1));
                }
            }
        }
    }

    // Traverse node hierarchy, emitting one part per mesh primitive.
    out.parts.clear();
    struct Pending {
        int nodeIndex;
        Matrix4 world;
    };
    std::vector<Pending> pending;
    for (int root : doc.rootNodes) {
        if (root >= 0 && root < (int)nodes.size()) {
            pending.push_back({root, nodes[root].transform});
        }
    }
    size_t consumed = 0;
    while (consumed < pending.size()) {
        Pending item = pending[consumed++];
        const GltfNodeData& node = nodes[item.nodeIndex];
        for (int child : node.children) {
            if (child >= 0 && child < (int)nodes.size()) {
                pending.push_back({child, item.world * nodes[child].transform});
            }
        }
        if (node.mesh < 0 || node.mesh >= (int)meshes.size()) {
            continue;
        }
        for (const GltfPrimitive& primitive : meshes[node.mesh].primitives) {
            if (primitive.mode != 4) {
                ION_LOG_WARN("glTF: unsupported primitive mode %d (only "
                             "TRIANGLES is supported)",
                             primitive.mode);
                continue;
            }
            if (primitive.position < 0) {
                continue;
            }
            const GltfAccessor& positionAccessor =
                doc.accessors[primitive.position];
            size_t vertexCount = positionAccessor.count;
            if (positionAccessor.type != "VEC3") {
                ION_LOG_WARN("glTF: POSITION accessor must be VEC3");
                continue;
            }

            std::vector<float> positions(vertexCount * 3);
            std::vector<float> normals(vertexCount * 3, 0.0f);
            std::vector<float> uvs(vertexCount * 2, 0.0f);
            if (!readAccessorFloats(doc, primitive.position, positions.data())) {
                continue;
            }
            if (primitive.normal >= 0) {
                readAccessorFloats(doc, primitive.normal, normals.data());
            }
            if (primitive.uv >= 0) {
                readAccessorFloats(doc, primitive.uv, uvs.data());
            }

            std::vector<uint32_t> indices;
            bool indexed = primitive.indices >= 0 &&
                           readAccessorIndices(doc, primitive.indices, indices);

            std::vector<Vertex> vertices;
            vertices.reserve(vertexCount);
            for (size_t i = 0; i < vertexCount; i++) {
                Vertex vertex;
                vertex.position =
                    Vector3(positions[i * 3], positions[i * 3 + 1],
                            positions[i * 3 + 2]);
                Vector3 normal(normals[i * 3], normals[i * 3 + 1],
                               normals[i * 3 + 2]);
                vertex.normal =
                    normal.length() > 1e-8f ? normal.normalized() : normal;
                vertex.uv = Vector2(uvs[i * 2], uvs[i * 2 + 1]);
                vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};
                vertices.push_back(vertex);
            }

            ModelPart part;
            part.mesh = createMesh(
                renderer, vertices.data(), (uint32_t)vertices.size(),
                indexed ? indices.data() : nullptr,
                indexed ? (uint32_t)indices.size() : 0);
            if (primitive.material >= 0 &&
                primitive.material < (int)doc.materials.size()) {
                part.material = doc.materials[primitive.material];
            } else {
                part.material = Material();
                part.material.name = "default";
            }
            part.transform = item.world;
            out.parts.push_back(std::move(part));
        }
    }

    if (out.parts.empty()) {
        ION_LOG_ERROR("glTF: '%s' produced no renderable parts", path.c_str());
        return false;
    }
    ION_LOG_INFO("glTF: loaded '%s' (%zu parts)", path.c_str(),
                 out.parts.size());
    return true;
}

}  // namespace ion
