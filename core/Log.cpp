#include <ion/core/Log.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace ion {

namespace {

constexpr const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "???";
}

struct Logger {
    std::mutex mutex;
    LogLevel level = LogLevel::Info;
};

Logger& logger() {
    static Logger instance;
    return instance;
}

} // namespace

void setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> guard(logger().mutex);
    logger().level = level;
}

LogLevel logLevel() {
    std::lock_guard<std::mutex> guard(logger().mutex);
    return logger().level;
}

void log(LogLevel level, const char* fmt, ...) {
    {
        std::lock_guard<std::mutex> guard(logger().mutex);
        if (level < logger().level) {
            return;
        }
    }

    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const char* name = levelName(level);
    FILE* out = (level >= LogLevel::Error) ? stderr : stdout;
    std::lock_guard<std::mutex> guard(logger().mutex);
    std::fprintf(out, "[%s] %s\n", name, buffer);
    std::fflush(out);

    if (level == LogLevel::Fatal) {
        std::abort();
    }
}

} // namespace ion
