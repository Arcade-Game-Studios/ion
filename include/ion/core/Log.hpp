#pragma once

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

void log(LogLevel level, const char* fmt, ...);

} // namespace ion

#define ION_LOG_TRACE(...) ::ion::log(::ion::LogLevel::Trace, __VA_ARGS__)
#define ION_LOG_DEBUG(...) ::ion::log(::ion::LogLevel::Debug, __VA_ARGS__)
#define ION_LOG_INFO(...) ::ion::log(::ion::LogLevel::Info, __VA_ARGS__)
#define ION_LOG_WARN(...) ::ion::log(::ion::LogLevel::Warn, __VA_ARGS__)
#define ION_LOG_ERROR(...) ::ion::log(::ion::LogLevel::Error, __VA_ARGS__)
#define ION_LOG_FATAL(...) ::ion::log(::ion::LogLevel::Fatal, __VA_ARGS__)
