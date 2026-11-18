#pragma once
// The connection interface (section 11) and its in-memory implementation, the only one until milestone 10.
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <vector>

#include "Protocol.h"

namespace vrtc {
    class IConnection {
    public:
        virtual ~IConnection() = default;

        virtual void Send(std::span<const uint8_t> message, Channel channel) = 0;

        // Moves the next message into `out`. False when empty.
        virtual bool Poll(std::vector<uint8_t> &out) = 0;
    };

    // One direction of a LocalConnection: a mutex-guarded queue of whole messages.
    class MessageQueue {
        std::mutex mutex;
        std::deque<std::vector<uint8_t>> messages;
        std::atomic<uint32_t> count{0};
        std::atomic<uint64_t> bytes{0};

    public:
        void Push(std::vector<uint8_t> &&message);

        bool Pop(std::vector<uint8_t> &out);

        [[nodiscard]] uint32_t Count() const { return count.load(std::memory_order_relaxed); }

        [[nodiscard]] uint64_t Bytes() const { return bytes.load(std::memory_order_relaxed); }
    };

    class LocalEndpoint final : public IConnection {
        MessageQueue &outgoing;
        MessageQueue &incoming;

    public:
        LocalEndpoint(MessageQueue &out, MessageQueue &in) : outgoing(out), incoming(in) {
        }

        void Send(std::span<const uint8_t> message, Channel channel) override;

        bool Poll(std::vector<uint8_t> &out) override;
    };

    // A pair of thread-safe queues. The client side lives on the main thread, the server side on the server thread.
    class LocalConnection {
        MessageQueue toServer;
        MessageQueue toClient;
        LocalEndpoint clientSide{toServer, toClient};
        LocalEndpoint serverSide{toClient, toServer};

    public:
        IConnection &ClientSide() { return clientSide; }

        IConnection &ServerSide() { return serverSide; }

        // Messages waiting for the client to drain; shown in the debug UI.
        [[nodiscard]] uint32_t PendingForClient() const { return toClient.Count(); }

        [[nodiscard]] uint64_t PendingBytesForClient() const { return toClient.Bytes(); }
    };
}
