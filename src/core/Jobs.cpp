#include "Jobs.h"

#include <algorithm>

unsigned vrtc::JobSystem::DefaultWorkerCount() {
    const unsigned hardware = std::thread::hardware_concurrency();
    return std::max(2u, hardware > 2 ? hardware - 2 : 2u);
}

vrtc::JobSystem::JobSystem(unsigned workerCount) {
    workerCount = std::max(1u, workerCount);
    workers.reserve(workerCount);
    for (unsigned i = 0; i < workerCount; ++i) {
        workers.emplace_back([this](std::stop_token stop) { Worker(stop); });
    }
}

vrtc::JobSystem::~JobSystem() {
    for (auto &worker: workers) worker.request_stop();
    wake.notify_all();
    workers.clear(); // joins
}

void vrtc::JobSystem::Worker(std::stop_token stop) {
    while (true) {
        Job job;
        {
            std::unique_lock lock(mutex);
            if (!wake.wait(lock, stop, [this] { return !queue.empty(); })) return; // stop requested
            // priority_queue::top is const; the job is move-only, so take it out by const_cast and pop.
            job = std::move(const_cast<Job &>(queue.top()));
            queue.pop();
            queued.fetch_sub(1, std::memory_order_relaxed);
            running.fetch_add(1, std::memory_order_relaxed);
        }
        job.fn();
        completed.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(mutex);
            running.fetch_sub(1, std::memory_order_relaxed);
            if (queue.empty() && running.load(std::memory_order_relaxed) == 0) idle.notify_all();
        }
    }
}

void vrtc::JobSystem::Submit(int32_t priority, std::move_only_function<void()> fn) {
    {
        std::lock_guard lock(mutex);
        queue.push(Job{priority, nextSequence++, std::move(fn)});
        queued.fetch_add(1, std::memory_order_relaxed);
    }
    wake.notify_one();
}

void vrtc::JobSystem::WaitIdle() {
    std::unique_lock lock(mutex);
    idle.wait(lock, [this] { return queue.empty() && running.load(std::memory_order_relaxed) == 0; });
}
