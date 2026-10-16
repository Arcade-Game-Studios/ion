#pragma once

#include <ion/core/FileSystem.hpp>
#include <ion/core/Log.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ion {

//
// Loads each asset once and shares it, keyed by path, with optional hot
// reloading when the file changes on disk:
//
//   AssetCache<SoundData> sounds([](const std::string& path) {
//       return std::const_pointer_cast<SoundData>(loadWav(path));
//   });
//   std::shared_ptr<SoundData> boom = sounds.get("sfx/boom.wav");
//   ...
//   sounds.reloadChanged(); // e.g. once per second in development builds
//
// Handles stay valid after a reload: reloadChanged() swaps the new data into
// the existing object (T must be move-assignable), so everything holding the
// shared_ptr sees the update.
//
template <typename T>
class AssetCache {
public:
    using Loader = std::function<std::shared_ptr<T>(const std::string& path)>;
    // Called after an asset was hot reloaded.
    using ReloadCallback = std::function<void(const std::string& path, T& asset)>;

    explicit AssetCache(Loader loader) : loader_(std::move(loader)) {}

    // Returns the cached asset, loading it on first use. Returns null (and
    // does not cache the failure) if loading fails.
    std::shared_ptr<T> get(const std::string& path) {
        auto it = entries_.find(path);
        if (it != entries_.end()) {
            return it->second.asset;
        }
        std::shared_ptr<T> asset = loader_ ? loader_(path) : nullptr;
        if (!asset) {
            ION_LOG_WARN("AssetCache: failed to load '%s'", path.c_str());
            return nullptr;
        }
        entries_[path] = Entry{asset, fs::lastWriteTime(path)};
        return asset;
    }

    bool contains(const std::string& path) const { return entries_.count(path) > 0; }

    // Inserts an asset that wasn't loaded from disk (generated data).
    void put(const std::string& key, std::shared_ptr<T> asset) {
        entries_[key] = Entry{std::move(asset), 0};
    }

    // Drops the cache's reference; holders keep their copy.
    bool evict(const std::string& path) { return entries_.erase(path) > 0; }

    // Drops assets nobody else references. Returns how many were removed.
    size_t evictUnused() {
        size_t removed = 0;
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second.asset.use_count() == 1) {
                it = entries_.erase(it);
                removed++;
            } else {
                ++it;
            }
        }
        return removed;
    }

    // Reloads one asset in place. Returns false if the load fails (the old
    // data is kept).
    bool reload(const std::string& path) {
        auto it = entries_.find(path);
        if (it == entries_.end() || !loader_) {
            return false;
        }
        std::shared_ptr<T> fresh = loader_(path);
        if (!fresh) {
            ION_LOG_WARN("AssetCache: reload of '%s' failed; keeping the old version",
                         path.c_str());
            return false;
        }
        *it->second.asset = std::move(*fresh);
        it->second.stamp = fs::lastWriteTime(path);
        if (onReload_) {
            onReload_(path, *it->second.asset);
        }
        return true;
    }

    // Reloads every cached file whose modification time changed. Returns
    // the paths that were reloaded.
    std::vector<std::string> reloadChanged() {
        std::vector<std::string> changed;
        for (auto& [path, entry] : entries_) {
            if (entry.stamp == 0) {
                continue; // put() entries have no file
            }
            int64_t stamp = fs::lastWriteTime(path);
            if (stamp != 0 && stamp != entry.stamp) {
                changed.push_back(path);
            }
        }
        std::vector<std::string> reloaded;
        for (const std::string& path : changed) {
            if (reload(path)) {
                reloaded.push_back(path);
            } else {
                entries_[path].stamp = fs::lastWriteTime(path); // don't retry every poll
            }
        }
        return reloaded;
    }

    void onReload(ReloadCallback callback) { onReload_ = std::move(callback); }

    size_t size() const { return entries_.size(); }
    void clear() { entries_.clear(); }

private:
    struct Entry {
        std::shared_ptr<T> asset;
        int64_t stamp = 0;
    };

    Loader loader_;
    ReloadCallback onReload_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace ion
