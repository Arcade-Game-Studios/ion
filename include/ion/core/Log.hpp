#pragma once

#include <functional>
#include <string>

namespace ion {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

void setLogLevel(LogLevel level);
LogLevel logLevel();

// Returns "TRACE", "DEBUG", ... for a level.
const char* logLevelName(LogLevel level);

// Receives every message at or above the log level, after formatting, in
// addition to the console. Called with the logger lock held, so it must not
// log itself. Pass an empty function to remove it.
using LogCallback = std::function<void(LogLevel level, const char* message)>;
void setLogCallback(LogCallback callback);

// Also appends messages to a file (pass "" to stop). Returns false if the
// file cannot be opened.
bool setLogFile(const std::string& path);

// Enables or disables console output (the callback and file still run).
void setLogConsole(bool enabled);

// Prefixes messages with the wall-clock time (HH:MM:SS.mmm). Off by default.
void setLogTimestamps(bool enabled);

#if defined(__MINGW32__)
// MinGW's C++ runtime formats with its ANSI stdio (C99 %zu etc.).
void log(LogLevel level, const char* fmt, ...) __attribute__((format(gnu_printf, 2, 3)));
#elif defined(__GNUC__) || defined(__clang__)
void log(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#else
void log(LogLevel level, const char* fmt, ...);
#endif

} // namespace ion

#define ION_LOG_TRACE(...) ::ion::log(::ion::LogLevel::Trace, __VA_ARGS__)
#define ION_LOG_DEBUG(...) ::ion::log(::ion::LogLevel::Debug, __VA_ARGS__)
#define ION_LOG_INFO(...) ::ion::log(::ion::LogLevel::Info, __VA_ARGS__)
#define ION_LOG_WARN(...) ::ion::log(::ion::LogLevel::Warn, __VA_ARGS__)
#define ION_LOG_ERROR(...) ::ion::log(::ion::LogLevel::Error, __VA_ARGS__)
#define ION_LOG_FATAL(...) ::ion::log(::ion::LogLevel::Fatal, __VA_ARGS__)
