#pragma once

#include <map>
#include <string>

namespace ion {

class Config {
public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    std::string getString(const std::string& key,
                          const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    float getFloat(const std::string& key, float defaultValue = 0.0f) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;

    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, const char* value);
    void set(const std::string& key, int value);
    void set(const std::string& key, float value);
    void set(const std::string& key, bool value);

    bool has(const std::string& key) const;
    void clear();

private:
    std::map<std::string, std::string> values_;
};

} // namespace ion
