#include <ion/platform/Paths.hpp>

#include <cstdio>

namespace ion {

std::string executableDirectory() {
    return ".";
}

std::string getAssetPath(const std::string& relative) {
    std::FILE* file = std::fopen(relative.c_str(), "r");
    if (file) {
        std::fclose(file);
    }
    return relative;
}

} // namespace ion
