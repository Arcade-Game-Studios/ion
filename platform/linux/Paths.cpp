#include <ion/platform/Paths.hpp>

#include <climits>
#include <unistd.h>

namespace ion {

std::string executableDirectory() {
    char buffer[PATH_MAX];
    ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return ".";
    }
    std::string path(buffer, (size_t)length);
    std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    return slash == 0 ? "/" : path.substr(0, slash);
}

std::string getAssetPath(const std::string& relative) {
    // Prefer the working directory (development builds run from the repo),
    // then fall back to next to the executable (installed games).
    if (relative.empty() || relative[0] == '/' || ::access(relative.c_str(), R_OK) == 0) {
        return relative;
    }
    std::string besideExe = executableDirectory() + "/" + relative;
    if (::access(besideExe.c_str(), R_OK) == 0) {
        return besideExe;
    }
    return relative;
}

} // namespace ion
