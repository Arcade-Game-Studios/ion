#include <ion/core/Json.hpp>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace ion {

namespace {

constexpr int kMaxDepth = 256;

struct ParseError : std::runtime_error {
    size_t offset;
    ParseError(const char* what, size_t offset) : std::runtime_error(what), offset(offset) {}
};

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    JsonValue parseDocument() {
        skipWhitespace();
        JsonValue value = parseValue(0);
        skipWhitespace();
        if (pos_ != text_.size()) {
            fail("unexpected trailing characters");
        }
        return value;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const char* message) { throw ParseError(message, pos_); }

    void skipWhitespace() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
                text_[pos_] == '\r')) {
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
            fail(c == ':' ? "expected ':'" : c == ',' ? "expected ',' or closing bracket"
                                                      : "unexpected character");
        }
    }

    void expectWord(const char* word) {
        for (const char* p = word; *p; p++) {
            if (!consume(*p)) {
                fail("invalid literal");
            }
        }
    }

    JsonValue parseValue(int depth) {
        if (depth > kMaxDepth) {
            fail("nesting too deep");
        }
        skipWhitespace();
        if (pos_ >= text_.size()) {
            fail("unexpected end of input");
        }
        char c = text_[pos_];
        switch (c) {
            case '{': return parseObject(depth);
            case '[': return parseArray(depth);
            case '"': return JsonValue(parseString());
            case 't': expectWord("true"); return JsonValue(true);
            case 'f': expectWord("false"); return JsonValue(false);
            case 'n': expectWord("null"); return JsonValue();
            default:
                if (c == '-' || (c >= '0' && c <= '9')) {
                    return JsonValue(parseNumber());
                }
                fail("unexpected character");
        }
    }

    JsonValue parseObject(int depth) {
        JsonValue value = JsonValue::makeObject();
        expect('{');
        skipWhitespace();
        if (consume('}')) {
            return value;
        }
        while (true) {
            skipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                fail("expected a string key");
            }
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            JsonValue member = parseValue(depth + 1);
            value.object.emplace_back(std::move(key), std::move(member));
            skipWhitespace();
            if (consume('}')) {
                break;
            }
            expect(',');
        }
        return value;
    }

    JsonValue parseArray(int depth) {
        JsonValue value = JsonValue::makeArray();
        expect('[');
        skipWhitespace();
        if (consume(']')) {
            return value;
        }
        while (true) {
            value.array.push_back(parseValue(depth + 1));
            skipWhitespace();
            if (consume(']')) {
                break;
            }
            expect(',');
        }
        return value;
    }

    static void appendUtf8(std::string& out, uint32_t codepoint) {
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
            fail("truncated unicode escape");
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
                fail("bad hex digit in unicode escape");
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
            if ((unsigned char)c < 0x20) {
                fail("unescaped control character in string");
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
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
                    if (code >= 0xD800 && code <= 0xDBFF && pos_ + 1 < text_.size() &&
                        text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        uint32_t low = parseHex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            appendUtf8(out, 0xFFFD); // unpaired high surrogate
                            code = low;
                        }
                    } else if (code >= 0xD800 && code <= 0xDFFF) {
                        code = 0xFFFD; // lone surrogate
                    }
                    appendUtf8(out, code);
                    break;
                }
                default: fail("invalid escape sequence");
            }
        }
        fail("unterminated string");
    }

    double parseNumber() {
        size_t start = pos_;
        consume('-');
        if (consume('0')) {
            // no leading zeros
        } else if (pos_ < text_.size() && std::isdigit((unsigned char)text_[pos_])) {
            while (pos_ < text_.size() && std::isdigit((unsigned char)text_[pos_])) {
                pos_++;
            }
        } else {
            fail("invalid number");
        }
        if (consume('.')) {
            if (pos_ >= text_.size() || !std::isdigit((unsigned char)text_[pos_])) {
                fail("expected digits after decimal point");
            }
            while (pos_ < text_.size() && std::isdigit((unsigned char)text_[pos_])) {
                pos_++;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            pos_++;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                pos_++;
            }
            if (pos_ >= text_.size() || !std::isdigit((unsigned char)text_[pos_])) {
                fail("expected exponent digits");
            }
            while (pos_ < text_.size() && std::isdigit((unsigned char)text_[pos_])) {
                pos_++;
            }
        }
        return std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr);
    }
};

void writeString(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out.push_back((char)c);
                }
        }
    }
    out.push_back('"');
}

void writeNumber(std::string& out, double n) {
    if (!std::isfinite(n)) {
        out += "null";
        return;
    }
    char buffer[32];
    // Integers print without a fraction; everything else uses the shortest
    // of %.15g / %.17g that round-trips.
    if (n == std::floor(n) && std::fabs(n) < 1e15) {
        std::snprintf(buffer, sizeof(buffer), "%.0f", n);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.15g", n);
        if (std::strtod(buffer, nullptr) != n) {
            std::snprintf(buffer, sizeof(buffer), "%.17g", n);
        }
    }
    out += buffer;
}

void writeValue(std::string& out, const JsonValue& v, int indent, int depth) {
    auto newline = [&](int level) {
        if (indent > 0) {
            out.push_back('\n');
            out.append((size_t)(indent * level), ' ');
        }
    };
    switch (v.type) {
        case JsonValue::Type::Null: out += "null"; break;
        case JsonValue::Type::Bool: out += v.boolean ? "true" : "false"; break;
        case JsonValue::Type::Number: writeNumber(out, v.number); break;
        case JsonValue::Type::String: writeString(out, v.string); break;
        case JsonValue::Type::Array:
            if (v.array.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (size_t i = 0; i < v.array.size(); i++) {
                if (i) {
                    out.push_back(',');
                }
                newline(depth + 1);
                writeValue(out, v.array[i], indent, depth + 1);
            }
            newline(depth);
            out.push_back(']');
            break;
        case JsonValue::Type::Object:
            if (v.object.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            for (size_t i = 0; i < v.object.size(); i++) {
                if (i) {
                    out.push_back(',');
                }
                newline(depth + 1);
                writeString(out, v.object[i].first);
                out += indent > 0 ? ": " : ":";
                writeValue(out, v.object[i].second, indent, depth + 1);
            }
            newline(depth);
            out.push_back('}');
            break;
    }
}

} // namespace

JsonValue& JsonValue::set(const std::string& key, JsonValue value) {
    if (type == Type::Null) {
        type = Type::Object;
    }
    if (type != Type::Object) {
        return *this;
    }
    for (auto& member : object) {
        if (member.first == key) {
            member.second = std::move(value);
            return *this;
        }
    }
    object.emplace_back(key, std::move(value));
    return *this;
}

bool JsonValue::remove(const std::string& key) {
    if (type != Type::Object) {
        return false;
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it->first == key) {
            object.erase(it);
            return true;
        }
    }
    return false;
}

JsonValue& JsonValue::push(JsonValue value) {
    if (type == Type::Null) {
        type = Type::Array;
    }
    if (type == Type::Array) {
        array.push_back(std::move(value));
    }
    return *this;
}

size_t JsonValue::size() const {
    if (type == Type::Array) {
        return array.size();
    }
    if (type == Type::Object) {
        return object.size();
    }
    return 0;
}

std::string JsonValue::dump(int indent) const {
    std::string out;
    writeValue(out, *this, indent < 0 ? 0 : indent, 0);
    return out;
}

bool JsonValue::parse(const std::string& text, JsonValue& out, std::string* error) {
    try {
        out = Parser(text).parseDocument();
        return true;
    } catch (const ParseError& e) {
        if (error) {
            *error = std::string(e.what()) + " at offset " + std::to_string(e.offset);
        }
        return false;
    }
}

bool JsonValue::load(const std::string& path, JsonValue& out, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) {
            *error = "cannot open '" + path + "'";
        }
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // Tolerate a UTF-8 byte order mark.
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF) {
        text.erase(0, 3);
    }
    return parse(text, out, error);
}

bool JsonValue::save(const std::string& path, int indent) const {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::string text = dump(indent);
    text.push_back('\n');
    file.write(text.data(), (std::streamsize)text.size());
    return (bool)file;
}

bool operator==(const JsonValue& a, const JsonValue& b) {
    if (a.type != b.type) {
        return false;
    }
    switch (a.type) {
        case JsonValue::Type::Null: return true;
        case JsonValue::Type::Bool: return a.boolean == b.boolean;
        case JsonValue::Type::Number: return a.number == b.number;
        case JsonValue::Type::String: return a.string == b.string;
        case JsonValue::Type::Array: return a.array == b.array;
        case JsonValue::Type::Object: return a.object == b.object;
    }
    return false;
}

} // namespace ion
