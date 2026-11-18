#pragma once
#include <mutex>
#include <utility>
#include <vector>

namespace vrtc {
    // Hard rule 5: workers return results through a queue that the owning thread drains.
    // Push from any thread; Drain from the owner. Drain swaps the whole batch out under one lock.
    template<class T>
    class ResultQueue {
        std::mutex mutex;
        std::vector<T> items;

    public:
        void Push(T &&item) {
            std::lock_guard lock(mutex);
            items.push_back(std::move(item));
        }

        // Appends everything queued so far to `out` and empties the queue.
        void Drain(std::vector<T> &out) {
            std::lock_guard lock(mutex);
            if (out.empty()) {
                out.swap(items);
            } else {
                for (T &item: items) out.push_back(std::move(item));
                items.clear();
            }
        }

        [[nodiscard]] size_t Size() {
            std::lock_guard lock(mutex);
            return items.size();
        }
    };
}
