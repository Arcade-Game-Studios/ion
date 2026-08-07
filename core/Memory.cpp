#include <ion/core/Memory.hpp>

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

namespace ion {
namespace Memory {

namespace {

constexpr size_t HEADER_SIZE = sizeof(size_t);

struct Tracker {
    std::mutex mutex;
    size_t allocations = 0;
    size_t bytes = 0;
};

Tracker& tracker() {
    static Tracker instance;
    return instance;
}

} // namespace

void* allocate(size_t size) {
    size_t* raw = static_cast<size_t*>(std::malloc(size + HEADER_SIZE));
    if (!raw) {
        throw std::bad_alloc();
    }
    *raw = size;
    {
        std::lock_guard<std::mutex> guard(tracker().mutex);
        tracker().allocations++;
        tracker().bytes += size;
    }
    return raw + 1;
}

void free(void* ptr) {
    if (!ptr) {
        return;
    }
    size_t* raw = static_cast<size_t*>(ptr) - 1;
    {
        std::lock_guard<std::mutex> guard(tracker().mutex);
        if (tracker().allocations > 0) {
            tracker().allocations--;
        }
        if (tracker().bytes >= *raw) {
            tracker().bytes -= *raw;
        }
    }
    std::free(raw);
}

void* reallocate(void* ptr, size_t size) {
    void* result = allocate(size);
    if (ptr) {
        size_t oldSize = *(static_cast<size_t*>(ptr) - 1);
        std::memcpy(result, ptr, oldSize < size ? oldSize : size);
        free(ptr);
    }
    return result;
}

size_t allocationCount() {
    std::lock_guard<std::mutex> guard(tracker().mutex);
    return tracker().allocations;
}

size_t allocatedBytes() {
    std::lock_guard<std::mutex> guard(tracker().mutex);
    return tracker().bytes;
}

} // namespace Memory
} // namespace ion

#if defined(ION_MEMORY_TRACKING)

void* operator new(std::size_t size) {
    return ion::Memory::allocate(size);
}

void* operator new[](std::size_t size) {
    return ion::Memory::allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ion::Memory::allocate(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ion::Memory::allocate(size);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* ptr) noexcept {
    ion::Memory::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    ion::Memory::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    ion::Memory::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    ion::Memory::free(ptr);
}

#endif
