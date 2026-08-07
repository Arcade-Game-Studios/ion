#pragma once

#include <ion/core/Log.hpp>

#include <string>
#include <utility>

namespace ion {

enum class ErrorCode {
    None,
    Unknown,
    InvalidArgument,
    NotFound,
    OutOfMemory,
    IoError,
    PlatformError,
};

struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;

    bool ok() const {
        return code == ErrorCode::None;
    }

    explicit operator bool() const {
        return ok();
    }
};

inline Error makeError(ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : error_(std::move(error)) {}

    bool ok() const {
        return error_.ok();
    }

    explicit operator bool() const {
        return ok();
    }

    T& value() {
        return value_;
    }

    const T& value() const {
        return value_;
    }

    T& operator*() {
        return value_;
    }

    const T& operator*() const {
        return value_;
    }

    const Error& error() const {
        return error_;
    }

private:
    T value_;
    Error error_;
};

} // namespace ion

#define ION_ASSERT(condition, message)                                                    \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            ::ion::log(::ion::LogLevel::Error,                                             \
                       "Assertion failed: %s \"%s\" at %s:%d", #condition, message,        \
                       __FILE__, __LINE__);                                               \
        }                                                                                 \
    } while (0)

#define ION_VERIFY(condition, message)                                                    \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            ::ion::log(::ion::LogLevel::Error,                                             \
                       "Verify failed: %s \"%s\" at %s:%d", #condition, message,          \
                       __FILE__, __LINE__);                                               \
        }                                                                                 \
    } while (0)
