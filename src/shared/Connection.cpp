#include "Connection.h"

void vrtc::MessageQueue::Push(std::vector<uint8_t> &&message) {
    const size_t size = message.size();
    std::lock_guard lock(mutex);
    messages.push_back(std::move(message));
    count.fetch_add(1, std::memory_order_relaxed);
    bytes.fetch_add(size, std::memory_order_relaxed);
}

bool vrtc::MessageQueue::Pop(std::vector<uint8_t> &out) {
    std::lock_guard lock(mutex);
    if (messages.empty()) return false;
    out = std::move(messages.front());
    messages.pop_front();
    count.fetch_sub(1, std::memory_order_relaxed);
    bytes.fetch_sub(out.size(), std::memory_order_relaxed);
    return true;
}

void vrtc::LocalEndpoint::Send(std::span<const uint8_t> message, Channel) {
    // Both channels are reliable and ordered in memory; the distinction matters only for real transports.
    outgoing.Push(std::vector<uint8_t>(message.begin(), message.end()));
}

bool vrtc::LocalEndpoint::Poll(std::vector<uint8_t> &out) {
    return incoming.Pop(out);
}
