#include <ion/platform/Paths.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace ion {

namespace {

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0,
                                    nullptr, nullptr);
    std::string out((size_t)count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], count, nullptr,
                        nullptr);
    return out;
}

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring out((size_t)count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], count);
    return out;
}

bool fileExists(const std::string& path) {
    DWORD attributes = GetFileAttributesW(widen(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES;
}

} // namespace

std::string executableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, &buffer[0], (DWORD)buffer.size());
        if (length == 0) {
            return ".";
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2); // truncated; retry with more room
    }
    std::string path = narrow(buffer);
    std::string::size_type slash = path.find_last_of("\\/");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

std::string getAssetPath(const std::string& relative) {
    if (relative.empty() || fileExists(relative)) {
        return relative;
    }
    std::string besideExe = executableDirectory() + "\\" + relative;
    if (fileExists(besideExe)) {
        return besideExe;
    }
    return relative;
}

} // namespace ion
