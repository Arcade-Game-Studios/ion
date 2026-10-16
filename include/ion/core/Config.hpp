#pragma once

#include <map>
#include <string>
#include <vector>

namespace ion {

//
// Key/value settings stored as text, loaded from and saved to INI-style
// files:
//
//   # comment            ; also a comment
//   width = 1280
//   title = "My Game"    (surrounding quotes are stripped)
//   [audio]
//   volume = 0.8         (stored under the key "audio.volume")
//
// load() merges into the existing values. Getters return the default when
// the key is missing or its value does not parse as the requested type.
//
class Config {
public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // Parses INI text directly (same rules as load()).
    void parse(const std::string& text);
    // Serializes to the text save() writes.
    std::string toString() const;

    std::string getString(const std::string& key,
                          const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    float getFloat(const std::string& key, float defaultValue = 0.0f) const;
    // Accepts true/false, yes/no, on/off and 1/0, case-insensitively.
    bool getBool(const std::string& key, bool defaultValue = false) const;

    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, const char* value);
    void set(const std::string& key, int value);
    void set(const std::string& key, float value);
    void set(const std::string& key, bool value);

    bool has(const std::string& key) const;
    bool remove(const std::string& key);
    void clear();

    size_t size() const { return values_.size(); }
    bool empty() const { return values_.empty(); }

    // All keys in sorted order.
    std::vector<std::string> keys() const;

    // Merges every key from other, overwriting existing ones.
    void merge(const Config& other);

private:
    std::map<std::string, std::string> values_;
};

} // namespace ion
