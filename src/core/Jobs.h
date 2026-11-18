#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace vrtc {
    // The in-house worker pool (section 4): std::jthread workers draining a priority queue.
    // Jobs must be self-contained (hard rule 5): they receive snapshots and report back through a ResultQueue.
    class JobSystem {
        struct Job {
            int32_t priority; // higher runs first
            uint64_t sequence; // earlier runs first among equal priorities
            std::move_only_function<void()> fn;
        };

        struct Compare {
            bool operator()(const Job &a, const Job &b) const {
                if (a.priority != b.priority) return a.priority < b.priority;
                return a.sequence > b.sequence;
            }
        };

        std::mutex mutex;
        std::condition_variable_any wake; // workers wait here
        std::condition_variable idle; // WaitIdle waits here
        std::priority_queue<Job, std::vector<Job>, Compare> queue;
        uint64_t nextSequence = 0;
        std::atomic<uint32_t> queued{0};
        std::atomic<uint32_t> running{0};
        std::atomic<uint64_t> completed{0};
        std::vector<std::jthread> workers;

        void Worker(std::stop_token stop);

    public:
        // Hardware threads minus two, at least two.
        static unsigned DefaultWorkerCount();

        explicit JobSystem(unsigned workerCount = DefaultWorkerCount());

        // Stops accepting work, lets running jobs finish, discards queued ones and joins.
        ~JobSystem();

        JobSystem(const JobSystem &) = delete;

        JobSystem &operator=(const JobSystem &) = delete;

        void Submit(int32_t priority, std::move_only_function<void()> fn);

        // Blocks until the queue is empty and no job is running.
        void WaitIdle();

        [[nodiscard]] unsigned WorkerCount() const { return static_cast<unsigned>(workers.size()); }

        [[nodiscard]] uint32_t Queued() const { return queued.load(std::memory_order_relaxed); }

        [[nodiscard]] uint32_t Running() const { return running.load(std::memory_order_relaxed); }

        [[nodiscard]] uint64_t Completed() const { return completed.load(std::memory_order_relaxed); }
    };
}
