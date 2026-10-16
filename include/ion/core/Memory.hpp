#pragma once

#include <cstddef>

namespace ion {

inline constexpr bool isPowerOfTwo(size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

inline constexpr size_t alignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

inline constexpr size_t alignDown(size_t value, size_t alignment) {
    return value & ~(alignment - 1);
}

inline void* alignUpPtr(void* ptr, size_t alignment) {
    size_t value = reinterpret_cast<size_t>(ptr);
    return reinterpret_cast<void*>(alignUp(value, alignment));
}

inline void* alignDownPtr(void* ptr, size_t alignment) {
    size_t value = reinterpret_cast<size_t>(ptr);
    return reinterpret_cast<void*>(alignDown(value, alignment));
}

namespace Memory {

// Tracked heap allocation. Returned pointers are aligned to
// alignof(std::max_align_t), like malloc. allocate throws std::bad_alloc.
void* allocate(size_t size);
void free(void* ptr);
void* reallocate(void* ptr, size_t size);

// Size requested for a block returned by allocate (0 for null).
size_t allocationSize(const void* ptr);

size_t allocationCount();      // live allocations
size_t allocatedBytes();       // live bytes
size_t peakAllocatedBytes();   // high-water mark of allocatedBytes()
size_t totalAllocationCount(); // allocations made since startup
void resetPeak();              // sets the peak to the current live bytes

} // namespace Memory

} // namespace ion
