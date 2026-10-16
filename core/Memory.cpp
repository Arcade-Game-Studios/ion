#include <ion/core/Memory.hpp>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

namespace ion {
namespace Memory {

namespace {

// The header that stores the allocation size must preserve malloc's
// alignment: operator new has to return memory aligned to
// __STDCPP_DEFAULT_NEW_ALIGNMENT__ (16 on x86-64 and ARM64), and a bare
// sizeof(size_t) header would leave every allocation 8-byte aligned.
constexpr size_t HEADER_SIZE = alignof(std::max_align_t) > sizeof(size_t)
                                   ? alignof(std::max_align_t)
                                   : sizeof(size_t);

struct Tracker {
    std::mutex mutex;
    size_t allocations = 0;
    size_t bytes = 0;
    size_t peakBytes = 0;
    size_t totalAllocations = 0;
};

size_t* headerOf(void* ptr) {
    return reinterpret_cast<size_t*>(static_cast<unsigned char*>(ptr) - HEADER_SIZE);
}

Tracker& tracker() {
    static Tracker instance;
    return instance;
}

} // namespace

void* allocate(size_t size) {
    if (size > static_cast<size_t>(-1) - HEADER_SIZE) {
        throw std::bad_alloc();
    }
    void* raw = std::malloc(size + HEADER_SIZE);
    if (!raw) {
        throw std::bad_alloc();
    }
    *static_cast<size_t*>(raw) = size;
    {
        std::lock_guard<std::mutex> guard(tracker().mutex);
        Tracker& t = tracker();
        t.allocations++;
        t.totalAllocations++;
        t.bytes += size;
        if (t.bytes > t.peakBytes) {
            t.peakBytes = t.bytes;
        }
    }
    return static_cast<unsigned char*>(raw) + HEADER_SIZE;
}

void free(void* ptr) {
    if (!ptr) {
        return;
    }
    size_t* raw = headerOf(ptr);
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
        size_t oldSize = *headerOf(ptr);
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

size_t peakAllocatedBytes() {
    std::lock_guard<std::mutex> guard(tracker().mutex);
    return tracker().peakBytes;
}

size_t totalAllocationCount() {
    std::lock_guard<std::mutex> guard(tracker().mutex);
    return tracker().totalAllocations;
}

void resetPeak() {
    std::lock_guard<std::mutex> guard(tracker().mutex);
    tracker().peakBytes = tracker().bytes;
}

size_t allocationSize(const void* ptr) {
    return ptr ? *headerOf(const_cast<void*>(ptr)) : 0;
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
