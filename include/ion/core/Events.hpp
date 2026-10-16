#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace ion {

//
// Type-safe publish/subscribe. Any copyable type can be an event:
//
//   struct PlayerDied { int lives; };
//   EventBus bus;
//   auto handle = bus.subscribe<PlayerDied>([](const PlayerDied& e) { ... });
//   bus.publish(PlayerDied{2});           // delivered immediately
//   bus.enqueue(PlayerDied{1});           // delivered by dispatchQueued()
//   bus.unsubscribe(handle);
//
// Handlers may subscribe, unsubscribe (including themselves) and publish
// from inside a handler. Handlers added during a publish only see later
// events. Not thread-safe: use from one thread.
//
class EventBus {
public:
    using Handle = uint64_t;
    static constexpr Handle INVALID_HANDLE = 0;

    template <typename E, typename Fn>
    Handle subscribe(Fn&& handler) {
        Handle handle = nextHandle_++;
        auto& list = channel<E>().handlers;
        list.push_back({handle, std::function<void(const E&)>(std::forward<Fn>(handler))});
        return handle;
    }

    // Returns whether the handle was subscribed.
    bool unsubscribe(Handle handle) {
        for (auto& channel : channels_) {
            if (channel && channel->remove(handle)) {
                return true;
            }
        }
        return false;
    }

    template <typename E>
    void publish(const E& event) {
        Channel<E>& c = channel<E>();
        c.depth++;
        size_t count = c.handlers.size(); // skip handlers added meanwhile
        for (size_t i = 0; i < count && i < c.handlers.size(); i++) {
            if (c.handlers[i].fn) { // null once unsubscribed mid-dispatch
                // Call a copy: the handler may subscribe (reallocating the
                // list) or unsubscribe itself, which would otherwise destroy
                // the function object while it runs.
                auto handler = c.handlers[i].fn;
                handler(event);
            }
        }
        if (--c.depth == 0) {
            c.compact();
        }
    }

    // Queues an event for the next dispatchQueued() (e.g. collisions found
    // during physics, handled after the step).
    template <typename E>
    void enqueue(E event) {
        queue_.push_back([this, e = std::move(event)]() { publish(e); });
    }

    // Delivers queued events in order, including ones queued while
    // dispatching. Returns how many were delivered.
    size_t dispatchQueued() {
        size_t delivered = 0;
        while (!queue_.empty()) {
            std::vector<std::function<void()>> batch;
            batch.swap(queue_);
            for (auto& deliver : batch) {
                deliver();
                delivered++;
            }
        }
        return delivered;
    }

    size_t queuedCount() const { return queue_.size(); }

    template <typename E>
    size_t subscriberCount() const {
        size_t index = typeIndex<E>();
        if (index >= channels_.size() || !channels_[index]) {
            return 0;
        }
        return static_cast<const Channel<E>*>(channels_[index].get())->live();
    }

    // Removes every subscription and queued event.
    void clear() {
        channels_.clear();
        queue_.clear();
    }

private:
    struct ChannelBase {
        virtual ~ChannelBase() = default;
        virtual bool remove(Handle handle) = 0;
    };

    template <typename E>
    struct Channel : ChannelBase {
        struct Entry {
            Handle handle;
            std::function<void(const E&)> fn;
        };
        std::vector<Entry> handlers;
        int depth = 0;

        bool remove(Handle handle) override {
            for (auto it = handlers.begin(); it != handlers.end(); ++it) {
                if (it->handle == handle && it->fn) {
                    if (depth > 0) {
                        it->fn = nullptr; // erased after the publish finishes
                    } else {
                        handlers.erase(it);
                    }
                    return true;
                }
            }
            return false;
        }

        void compact() {
            for (size_t i = handlers.size(); i-- > 0;) {
                if (!handlers[i].fn) {
                    handlers.erase(handlers.begin() + (long)i);
                }
            }
        }

        size_t live() const {
            size_t n = 0;
            for (const Entry& e : handlers) {
                n += e.fn ? 1 : 0;
            }
            return n;
        }
    };

    static size_t nextTypeIndex() {
        static size_t counter = 0;
        return counter++;
    }

    template <typename E>
    static size_t typeIndex() {
        static const size_t index = nextTypeIndex();
        return index;
    }

    template <typename E>
    Channel<E>& channel() {
        size_t index = typeIndex<E>();
        if (index >= channels_.size()) {
            channels_.resize(index + 1);
        }
        if (!channels_[index]) {
            channels_[index] = std::make_unique<Channel<E>>();
        }
        return *static_cast<Channel<E>*>(channels_[index].get());
    }

    std::vector<std::unique_ptr<ChannelBase>> channels_;
    std::vector<std::function<void()>> queue_;
    Handle nextHandle_ = 1;
};

} // namespace ion
