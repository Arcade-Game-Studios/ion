#include <ion/core/Log.hpp>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>

namespace ion {

namespace {

struct Logger {
    std::mutex mutex;
    LogLevel level = LogLevel::Info;
    LogCallback callback;
    std::FILE* file = nullptr;
    bool console = true;
    bool timestamps = false;

    ~Logger() {
        if (file) {
            std::fclose(file);
        }
    }
};

Logger& logger() {
    static Logger instance;
    return instance;
}

void formatTimestamp(char* out, size_t size) {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t seconds = system_clock::to_time_t(now);
    int millis = (int)(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    std::snprintf(out, size, "%02d:%02d:%02d.%03d ", local.tm_hour, local.tm_min,
                  local.tm_sec, millis);
}

} // namespace

const char* logLevelName(LogLevel level) {
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

void setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> guard(logger().mutex);
    logger().level = level;
}

LogLevel logLevel() {
    std::lock_guard<std::mutex> guard(logger().mutex);
    return logger().level;
}

void setLogCallback(LogCallback callback) {
    std::lock_guard<std::mutex> guard(logger().mutex);
    logger().callback = std::move(callback);
}

bool setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> guard(logger().mutex);
    Logger& l = logger();
    if (l.file) {
        std::fclose(l.file);
        l.file = nullptr;
    }
    if (path.empty()) {
        return true;
    }
    l.file = std::fopen(path.c_str(), "a");
    return l.file != nullptr;
}

void setLogConsole(bool enabled) {
    std::lock_guard<std::mutex> guard(logger().mutex);
    logger().console = enabled;
}

void setLogTimestamps(bool enabled) {
    std::lock_guard<std::mutex> guard(logger().mutex);
    logger().timestamps = enabled;
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
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const char* name = logLevelName(level);
    {
        std::lock_guard<std::mutex> guard(logger().mutex);
        Logger& l = logger();
        char stamp[32] = "";
        if (l.timestamps) {
            formatTimestamp(stamp, sizeof(stamp));
        }
        if (l.console) {
            std::FILE* out = (level >= LogLevel::Error) ? stderr : stdout;
            std::fprintf(out, "%s[%s] %s\n", stamp, name, buffer);
            std::fflush(out);
        }
        if (l.file) {
            std::fprintf(l.file, "%s[%s] %s\n", stamp, name, buffer);
            std::fflush(l.file);
        }
        if (l.callback) {
            l.callback(level, buffer);
        }
    }

    if (level == LogLevel::Fatal) {
        std::abort();
    }
}

} // namespace ion
