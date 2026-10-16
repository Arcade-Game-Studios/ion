#include <ion/core/Config.hpp>

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <sstream>
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

std::string lower(std::string text) {
    for (char& c : text) {
        c = (char)std::tolower((unsigned char)c);
    }
    return text;
}

std::string unquote(const std::string& value) {
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool needsQuotes(const std::string& value) {
    return !value.empty() &&
           (std::isspace((unsigned char)value.front()) ||
            std::isspace((unsigned char)value.back()) || value.front() == '"' ||
            value.front() == '\'');
}

} // namespace

void Config::parse(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    std::string section;
    while (std::getline(stream, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') {
            continue;
        }
        if (t.front() == '[' && t.back() == ']') {
            section = trim(t.substr(1, t.size() - 2));
            continue;
        }
        std::string::size_type eq = t.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim(t.substr(0, eq));
        std::string value = unquote(trim(t.substr(eq + 1)));
        if (key.empty()) {
            continue;
        }
        values_[section.empty() ? key : section + "." + key] = value;
    }
}

bool Config::load(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    std::string text;
    char buffer[4096];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, n);
    }
    std::fclose(file);
    parse(text);
    return true;
}

std::string Config::toString() const {
    std::string out;
    for (const auto& entry : values_) {
        out += entry.first;
        out += '=';
        if (needsQuotes(entry.second)) {
            out += '"' + entry.second + '"';
        } else {
            out += entry.second;
        }
        out += '\n';
    }
    return out;
}

bool Config::save(const std::string& path) const {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }
    std::string text = toString();
    bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    ok = (std::fclose(file) == 0) && ok;
    return ok;
}

std::string Config::getString(const std::string& key,
                              const std::string& defaultValue) const {
    auto it = values_.find(key);
    return it != values_.end() ? it->second : defaultValue;
}

int Config::getInt(const std::string& key, int defaultValue) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) {
        return defaultValue;
    }
    const char* begin = it->second.c_str();
    char* end = nullptr;
    errno = 0;
    // Decimal, plus an explicit 0x prefix for hex. (Base 0 would also treat
    // a leading zero as octal, turning "010" into 8.)
    const char* digits = begin + (*begin == '-' || *begin == '+' ? 1 : 0);
    bool hex = digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X');
    long value = std::strtol(begin, &end, hex ? 16 : 10);
    if (end == begin || *end != '\0' || errno == ERANGE || value < INT_MIN ||
        value > INT_MAX) {
        return defaultValue;
    }
    return (int)value;
}

float Config::getFloat(const std::string& key, float defaultValue) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) {
        return defaultValue;
    }
    const char* begin = it->second.c_str();
    char* end = nullptr;
    float value = std::strtof(begin, &end);
    if (end == begin || *end != '\0') {
        return defaultValue;
    }
    return value;
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    auto it = values_.find(key);
    if (it == values_.end()) {
        return defaultValue;
    }
    std::string value = lower(it->second);
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return defaultValue;
}

void Config::set(const std::string& key, const std::string& value) {
    values_[key] = value;
}

void Config::set(const std::string& key, const char* value) {
    values_[key] = value ? value : "";
}

void Config::set(const std::string& key, int value) {
    values_[key] = std::to_string(value);
}

void Config::set(const std::string& key, float value) {
    char buffer[64];
    // 9 significant digits round-trip any float exactly.
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    values_[key] = buffer;
}

void Config::set(const std::string& key, bool value) {
    values_[key] = value ? "true" : "false";
}

bool Config::has(const std::string& key) const {
    return values_.find(key) != values_.end();
}

bool Config::remove(const std::string& key) {
    return values_.erase(key) > 0;
}

void Config::clear() {
    values_.clear();
}

std::vector<std::string> Config::keys() const {
    std::vector<std::string> out;
    out.reserve(values_.size());
    for (const auto& entry : values_) {
        out.push_back(entry.first);
    }
    return out;
}

void Config::merge(const Config& other) {
    for (const auto& entry : other.values_) {
        values_[entry.first] = entry.second;
    }
}

} // namespace ion
