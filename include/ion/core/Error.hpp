#pragma once

#include <ion/core/Log.hpp>

#include <cassert>
#include <optional>
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

inline const char* errorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::None: return "None";
        case ErrorCode::Unknown: return "Unknown";
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::NotFound: return "NotFound";
        case ErrorCode::OutOfMemory: return "OutOfMemory";
        case ErrorCode::IoError: return "IoError";
        case ErrorCode::PlatformError: return "PlatformError";
    }
    return "Unknown";
}

//
// Either a value or an Error. T need not be default-constructible. Calling
// value() on a failed result is a programming error (asserted in debug).
//
template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : error_(std::move(error)) {
        if (error_.ok()) {
            // A "successful" error carries no value; normalise it so ok()
            // and value() agree.
            error_.code = ErrorCode::Unknown;
        }
    }

    bool ok() const {
        return value_.has_value();
    }

    explicit operator bool() const {
        return ok();
    }

    T& value() {
        assert(ok() && "Result::value() called on an error");
        return *value_;
    }

    const T& value() const {
        assert(ok() && "Result::value() called on an error");
        return *value_;
    }

    // Returns the value, or fallback when this holds an error.
    T valueOr(T fallback) const {
        return ok() ? *value_ : std::move(fallback);
    }

    T& operator*() {
        return value();
    }

    const T& operator*() const {
        return value();
    }

    T* operator->() {
        return &value();
    }

    const T* operator->() const {
        return &value();
    }

    const Error& error() const {
        return error_;
    }

private:
    std::optional<T> value_;
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
