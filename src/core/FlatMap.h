#pragma once
// A small sorted-vector map for sparse per-chunk tables (section 6: block index -> micro slot). Lookups are a
// binary search over contiguous memory; inserts and erases shift the tail, which is cheap at the sizes involved.
#include <algorithm>
#include <utility>
#include <vector>

namespace vrtc {
    template<class Key, class Value>
    class FlatMap {
        std::vector<std::pair<Key, Value>> items; // sorted by key

        auto LowerBound(Key key) {
            return std::lower_bound(items.begin(), items.end(), key,
                                    [](const std::pair<Key, Value> &item, Key k) { return item.first < k; });
        }

        auto LowerBound(Key key) const {
            return std::lower_bound(items.begin(), items.end(), key,
                                    [](const std::pair<Key, Value> &item, Key k) { return item.first < k; });
        }

    public:
        Value *Find(Key key) {
            const auto it = LowerBound(key);
            return it != items.end() && it->first == key ? &it->second : nullptr;
        }

        const Value *Find(Key key) const {
            const auto it = LowerBound(key);
            return it != items.end() && it->first == key ? &it->second : nullptr;
        }

        // Inserts or overwrites.
        Value &Set(Key key, Value value) {
            const auto it = LowerBound(key);
            if (it != items.end() && it->first == key) {
                it->second = std::move(value);
                return it->second;
            }
            return items.insert(it, {key, std::move(value)})->second;
        }

        bool Erase(Key key) {
            const auto it = LowerBound(key);
            if (it == items.end() || it->first != key) return false;
            items.erase(it);
            return true;
        }

        void Clear() { items.clear(); }

        [[nodiscard]] size_t Size() const { return items.size(); }

        [[nodiscard]] bool Empty() const { return items.empty(); }

        [[nodiscard]] size_t MemoryBytes() const { return items.capacity() * sizeof(std::pair<Key, Value>); }

        auto begin() const { return items.begin(); }

        auto end() const { return items.end(); }
    };
}
