#include <ion/platform/Paths.hpp>

#include <Cocoa/Cocoa.h>
#include <mach-o/dyld.h>

#include <unistd.h>
#include <climits>
#include <cstdio>

namespace ion {

std::string executableDirectory() {
    char buf[PATH_MAX];
    uint32_t size = PATH_MAX;
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::string path(buf);
        std::string::size_type pos = path.find_last_of('/');
        if (pos != std::string::npos) {
            return path.substr(0, pos);
        }
    }
    return ".";
}

std::string getAssetPath(const std::string& relative) {
    NSString* resources = [[NSBundle mainBundle] resourcePath];
    if (resources) {
        std::string bundlePath = std::string([resources UTF8String]) + "/" + relative;
        if (access(bundlePath.c_str(), R_OK) == 0) {
            return bundlePath;
        }
    }

    std::string exePath = executableDirectory() + "/" + relative;
    if (access(exePath.c_str(), R_OK) == 0) {
        return exePath;
    }

    if (access(relative.c_str(), R_OK) == 0) {
        return relative;
    }

    return relative;
}

} // namespace ion
