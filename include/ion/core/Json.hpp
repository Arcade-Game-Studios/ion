#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ion {

//
// A JSON document value: null, bool, number, string, array or object.
// Objects keep insertion order (so saved files are stable and diffable).
//
//   JsonValue save = JsonValue::makeObject();
//   save.set("level", 3).set("name", "Ada");
//   save.set("inventory", JsonValue::makeArray({"sword", "key"}));
//   std::string text = save.dump(2);
//
//   JsonValue loaded;
//   std::string error;
//   if (JsonValue::parse(text, loaded, &error)) {
//       int level = (int)loaded.getNumber("level", 1);
//   }
//
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    JsonValue() = default;
    JsonValue(std::nullptr_t) {}
    JsonValue(bool value) : type(Type::Bool), boolean(value) {}
    // Any integer or floating-point type (one template avoids ambiguous
    // overloads for size_t, int64_t, etc., which differ between platforms).
    template <typename T,
              typename = std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>>
    JsonValue(T value) : type(Type::Number), number((double)value) {}
    JsonValue(const char* value) : type(Type::String), string(value ? value : "") {}
    JsonValue(std::string value) : type(Type::String), string(std::move(value)) {}

    static JsonValue makeArray(std::initializer_list<JsonValue> items = {}) {
        JsonValue v;
        v.type = Type::Array;
        v.array.assign(items.begin(), items.end());
        return v;
    }

    static JsonValue makeObject() {
        JsonValue v;
        v.type = Type::Object;
        return v;
    }

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    double asNumber(double def = 0.0) const {
        return type == Type::Number ? number : def;
    }

    bool asBool(bool def = false) const {
        return type == Type::Bool ? boolean : def;
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

    JsonValue* get(const std::string& key) {
        return const_cast<JsonValue*>(static_cast<const JsonValue*>(this)->get(key));
    }

    // Array element lookup; returns nullptr when out of range.
    const JsonValue* at(size_t index) const {
        if (type != Type::Array || index >= array.size()) {
            return nullptr;
        }
        return &array[index];
    }

    bool has(const std::string& key) const { return get(key) != nullptr; }

    // Typed member access with defaults (wrong types yield the default).
    double getNumber(const std::string& key, double def = 0.0) const {
        const JsonValue* v = get(key);
        return v ? v->asNumber(def) : def;
    }

    bool getBool(const std::string& key, bool def = false) const {
        const JsonValue* v = get(key);
        return v ? v->asBool(def) : def;
    }

    std::string getString(const std::string& key, const std::string& def = "") const {
        const JsonValue* v = get(key);
        return v ? v->asString(def) : def;
    }

    // Sets (or replaces) an object member, converting a null value into an
    // object first. Returns *this for chaining.
    JsonValue& set(const std::string& key, JsonValue value);

    // Removes an object member; returns whether it existed.
    bool remove(const std::string& key);

    // Appends to an array, converting a null value into an array first.
    JsonValue& push(JsonValue value);

    // Elements in an array or members in an object (0 otherwise).
    size_t size() const;

    // Serializes to JSON text. indent 0 is compact; otherwise members are
    // placed on their own lines indented by `indent` spaces per level.
    // Non-finite numbers are written as null (JSON has no NaN/Infinity).
    std::string dump(int indent = 0) const;

    // Parses JSON text (RFC 8259; nesting limited to 256 levels). On
    // failure returns false, leaves out unchanged and describes the problem
    // (with byte offset) in *error if provided.
    static bool parse(const std::string& text, JsonValue& out, std::string* error = nullptr);

    // Reads / writes a JSON file.
    static bool load(const std::string& path, JsonValue& out, std::string* error = nullptr);
    bool save(const std::string& path, int indent = 2) const;
};

bool operator==(const JsonValue& a, const JsonValue& b);
inline bool operator!=(const JsonValue& a, const JsonValue& b) { return !(a == b); }

} // namespace ion
