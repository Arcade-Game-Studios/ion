#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace ion {

//
// Fixed-capacity pool of T with O(1) acquire/release and stable addresses.
// Avoids per-object heap allocation for short-lived objects such as
// bullets or pickups.
//
//   ObjectPool<Bullet> bullets(256);
//   Bullet* b = bullets.acquire(position, velocity); // null when full
//   ...
//   bullets.forEach([&](Bullet& b) { ... });
//   bullets.release(b);
//
template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t capacity)
        : storage_(new Slot[capacity]), alive_(capacity, false), capacity_(capacity) {
        freeList_.reserve(capacity);
        for (size_t i = capacity; i-- > 0;) {
            freeList_.push_back(i);
        }
    }

    ~ObjectPool() { clear(); }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    // Constructs a T in a free slot; returns null when the pool is full.
    template <typename... Args>
    T* acquire(Args&&... args) {
        if (freeList_.empty()) {
            return nullptr;
        }
        size_t index = freeList_.back();
        freeList_.pop_back();
        T* object = new (&storage_[index]) T(std::forward<Args>(args)...);
        alive_[index] = true;
        return object;
    }

    // Destroys an object from this pool. Ignores null and foreign pointers.
    void release(T* object) {
        if (!owns(object)) {
            return;
        }
        size_t index = indexOf(object);
        if (!alive_[index]) {
            return; // double release
        }
        object->~T();
        alive_[index] = false;
        freeList_.push_back(index);
    }

    bool owns(const T* object) const {
        if (!object) {
            return false;
        }
        auto p = reinterpret_cast<uintptr_t>(object);
        auto begin = reinterpret_cast<uintptr_t>(storage_.get());
        auto end = begin + sizeof(Slot) * capacity_;
        return p >= begin && p < end && (p - begin) % sizeof(Slot) == 0;
    }

    // Calls fn(T&) for every live object. fn may release the object it is
    // given.
    template <typename Fn>
    void forEach(Fn&& fn) {
        for (size_t i = 0; i < capacity_; i++) {
            if (alive_[i]) {
                fn(*objectAt(i));
            }
        }
    }

    void clear() {
        for (size_t i = 0; i < capacity_; i++) {
            if (alive_[i]) {
                objectAt(i)->~T();
                alive_[i] = false;
            }
        }
        freeList_.clear();
        for (size_t i = capacity_; i-- > 0;) {
            freeList_.push_back(i);
        }
    }

    size_t size() const { return capacity_ - freeList_.size(); }
    size_t capacity() const { return capacity_; }
    bool full() const { return freeList_.empty(); }
    bool empty() const { return size() == 0; }

private:
    struct Slot {
        alignas(T) unsigned char bytes[sizeof(T)];
    };

    T* objectAt(size_t index) { return std::launder(reinterpret_cast<T*>(&storage_[index])); }
    size_t indexOf(const T* object) const {
        return (reinterpret_cast<uintptr_t>(object) - reinterpret_cast<uintptr_t>(storage_.get())) /
               sizeof(Slot);
    }

    std::unique_ptr<Slot[]> storage_;
    std::vector<bool> alive_;
    std::vector<size_t> freeList_;
    size_t capacity_;
};

} // namespace ion
