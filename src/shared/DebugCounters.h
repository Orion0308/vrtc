#pragma once
// Counters the server publishes for the debug UI (hard rule 10). Plain atomics, written on the server thread and
// read on the main thread. They carry no game state, so they sit beside the message protocol rather than in it.
#include <atomic>
#include <cstdint>

namespace vrtc {
    struct ServerStats {
        std::atomic<double> tickMs{0};
        std::atomic<double> tickMsMax{0}; // worst tick in the last second
        std::atomic<uint64_t> ticks{0};
        std::atomic<uint32_t> clients{0};
        std::atomic<uint32_t> chunksLoaded{0};
        std::atomic<uint32_t> chunksLoading{0}; // generation jobs in flight
        std::atomic<uint32_t> loadQueue{0}; // wanted but not yet dispatched
        std::atomic<uint64_t> chunksGenerated{0};
        std::atomic<uint64_t> chunksUnloaded{0};
        std::atomic<uint64_t> chunksSent{0};
        std::atomic<uint64_t> bytesSent{0};
        std::atomic<uint64_t> messagesReceived{0};
        std::atomic<uint64_t> worldBytes{0};
        std::atomic<uint64_t> edits{0}; // Dig and Place messages applied
        std::atomic<uint64_t> editsRejected{0}; // out of reach, too large, too fast
        std::atomic<uint64_t> bitsCarved{0}; // micro-voxels removed (4096 per whole block)
        std::atomic<uint64_t> bitsFilled{0};
        std::atomic<uint64_t> blocksChanged{0}; // block delta entries sent
        std::atomic<uint32_t> damagedBlocks{0}; // blocks with a mask, across loaded chunks
        std::atomic<double> lastEditMs{0};
        std::atomic<uint32_t> entities{0};
        std::atomic<double> systemsMs{0}; // player movement + AI + physics for the last tick
    };
}
