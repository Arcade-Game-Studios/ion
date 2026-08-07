#include <ion/core/Config.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace ion {

namespace {

std::string trim(const std::string& text) {
    std::string::size_type start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    std::string::size_type end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

} // namespace

bool Config::load(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "r");
    if (!file) {
        return false;
    }

    char line[4096];
    while (std::fgets(line, sizeof(line), file)) {
        std::string text = trim(line);
        if (text.empty() || text[0] == '#' || text[0] == ';') {
            continue;
        }
        std::string::size_type eq = text.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim(text.substr(0, eq));
        std::string value = trim(text.substr(eq + 1));
        if (!key.empty()) {
            values_[key] = value;
        }
    }

    std::fclose(file);
    return true;
}

bool Config::save(const std::string& path) const {
    std::FILE* file = std::fopen(path.c_str(), "w");
    if (!file) {
        return false;
    }
    for (const auto& entry : values_) {
        std::fprintf(file, "%s=%s\n", entry.first.c_str(), entry.second.c_str());
    }
    std::fclose(file);
    return true;
}

std::string Config::getString(const std::string& key,
                              const std::string& defaultValue) const {
    auto it = values_.find(key);
    return it != values_.end() ? it->second : defaultValue;
}

int Config::getInt(const std::string& key, int defaultValue) const {
    auto it = values_.find(key);
    return it != values_.end() ? std::atoi(it->second.c_str()) : defaultValue;
}

float Config::getFloat(const std::string& key, float defaultValue) const {
    auto it = values_.find(key);
    return it != values_.end() ? std::atof(it->second.c_str()) : defaultValue;
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    auto it = values_.find(key);
    if (it == values_.end()) {
        return defaultValue;
    }
    const std::string& value = it->second;
    return value == "1" || value == "true" || value == "True" || value == "yes";
}

void Config::set(const std::string& key, const std::string& value) {
    values_[key] = value;
}

void Config::set(const std::string& key, const char* value) {
    values_[key] = value;
}

void Config::set(const std::string& key, int value) {
    values_[key] = std::to_string(value);
}

void Config::set(const std::string& key, float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    values_[key] = buffer;
}

void Config::set(const std::string& key, bool value) {
    values_[key] = value ? "true" : "false";
}

bool Config::has(const std::string& key) const {
    return values_.find(key) != values_.end();
}

void Config::clear() {
    values_.clear();
}

} // namespace ion
