#include <ion/core/FileSystem.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace ion {
namespace fs {

namespace stdfs = std::filesystem;

namespace {

stdfs::path toPath(const std::string& utf8) {
    // u8path is deprecated in C++20 but is the portable way to get UTF-8
    // semantics on Windows; char8_t round-trip does the same.
    return stdfs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string fromPath(const stdfs::path& path) {
    std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

std::string lower(std::string text) {
    for (char& c : text) {
        c = (char)std::tolower((unsigned char)c);
    }
    return text;
}

bool writeAtomically(const std::string& path, const void* data, size_t size) {
    stdfs::path target = toPath(path);
    stdfs::path temp = target;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        if (size > 0) {
            file.write(static_cast<const char*>(data), (std::streamsize)size);
        }
        file.flush();
        if (!file) {
            std::error_code ignored;
            stdfs::remove(temp, ignored);
            return false;
        }
    }
    std::error_code ec;
    stdfs::rename(temp, target, ec); // replaces an existing file
    if (ec) {
        stdfs::remove(temp, ec);
        return false;
    }
    return true;
}

} // namespace

bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream file(toPath(path), std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !file.bad();
}

bool readBinaryFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(toPath(path), std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !file.bad();
}

bool writeTextFile(const std::string& path, const std::string& text) {
    return writeAtomically(path, text.data(), text.size());
}

bool writeBinaryFile(const std::string& path, const void* data, size_t size) {
    if (!data && size > 0) {
        return false;
    }
    return writeAtomically(path, data, size);
}

bool exists(const std::string& path) {
    std::error_code ec;
    return stdfs::exists(toPath(path), ec);
}

bool isFile(const std::string& path) {
    std::error_code ec;
    return stdfs::is_regular_file(toPath(path), ec);
}

bool isDirectory(const std::string& path) {
    std::error_code ec;
    return stdfs::is_directory(toPath(path), ec);
}

bool createDirectories(const std::string& path) {
    std::error_code ec;
    stdfs::create_directories(toPath(path), ec);
    return isDirectory(path);
}

bool removeFile(const std::string& path) {
    std::error_code ec;
    return stdfs::remove(toPath(path), ec);
}

int64_t fileSize(const std::string& path) {
    std::error_code ec;
    uintmax_t size = stdfs::file_size(toPath(path), ec);
    return ec ? -1 : (int64_t)size;
}

int64_t lastWriteTime(const std::string& path) {
    std::error_code ec;
    auto time = stdfs::last_write_time(toPath(path), ec);
    if (ec) {
        return 0;
    }
    return (int64_t)time.time_since_epoch().count();
}

std::vector<std::string> listDirectory(const std::string& path, const std::string& ext) {
    std::vector<std::string> names;
    std::error_code ec;
    stdfs::directory_iterator it(toPath(path), ec);
    if (ec) {
        return names;
    }
    std::string wanted = lower(ext);
    for (const auto& entry : it) {
        std::string name = fromPath(entry.path().filename());
        if (!wanted.empty() && (!entry.is_regular_file(ec) || extension(name) != wanted)) {
            continue;
        }
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    char last = a.back();
    if (last == '/' || last == '\\') {
        return a + b;
    }
    return a + "/" + b;
}

std::string fileName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stem(const std::string& path) {
    std::string name = fileName(path);
    size_t dot = name.find_last_of('.');
    return (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
}

std::string extension(const std::string& path) {
    std::string name = fileName(path);
    size_t dot = name.find_last_of('.');
    return (dot == std::string::npos || dot == 0) ? "" : lower(name.substr(dot));
}

std::string parentPath(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return "";
    }
    return slash == 0 ? path.substr(0, 1) : path.substr(0, slash);
}

std::string userDataDirectory(const std::string& appName) {
    std::string base;
#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA")) {
        base = appData;
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        base = std::string(home) + "/Library/Application Support";
    }
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::string(home) + "/.local/share";
    }
#endif
    if (base.empty()) {
        return "";
    }
    std::string dir = joinPath(base, appName.empty() ? "ion" : appName);
    createDirectories(dir);
    return dir;
}

} // namespace fs
} // namespace ion
