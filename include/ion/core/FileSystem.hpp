#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ion {

//
// File helpers. Paths are UTF-8; '/' works as a separator on every
// platform. Functions report failure through their return value and never
// throw.
//
namespace fs {

bool readTextFile(const std::string& path, std::string& out);
bool readBinaryFile(const std::string& path, std::vector<uint8_t>& out);

// Writes atomically: data goes to a temporary file that then replaces the
// target, so a crash mid-write never leaves a truncated save game.
bool writeTextFile(const std::string& path, const std::string& text);
bool writeBinaryFile(const std::string& path, const void* data, size_t size);

bool exists(const std::string& path);
bool isFile(const std::string& path);
bool isDirectory(const std::string& path);
// Creates the directory and any missing parents.
bool createDirectories(const std::string& path);
bool removeFile(const std::string& path);

// -1 if the file doesn't exist.
int64_t fileSize(const std::string& path);
// Opaque modification stamp; changes when the file is written (0 if
// missing). Used for hot reloading.
int64_t lastWriteTime(const std::string& path);

// Entries directly inside a directory (names, not full paths), sorted.
// With an extension filter (e.g. ".png", case-insensitive) only matching
// files are returned.
std::vector<std::string> listDirectory(const std::string& path,
                                       const std::string& extension = "");

// Pure string helpers (no file access).
std::string joinPath(const std::string& a, const std::string& b);
std::string fileName(const std::string& path);   // "a/b/c.png" -> "c.png"
std::string stem(const std::string& path);       // "a/b/c.png" -> "c"
std::string extension(const std::string& path);  // "a/b/c.png" -> ".png" (lowercase)
std::string parentPath(const std::string& path); // "a/b/c.png" -> "a/b"

// Per-user writable directory for saves and settings, created on demand:
//   Linux:   $XDG_DATA_HOME/<app> or ~/.local/share/<app>
//   macOS:   ~/Library/Application Support/<app>
//   Windows: %APPDATA%\<app>
// Returns "" if no home directory can be determined.
std::string userDataDirectory(const std::string& appName);

} // namespace fs

} // namespace ion
