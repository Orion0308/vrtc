#pragma once
// The server's world (section 6): a hash map of loaded chunks. Owned by the server thread.
#include <memory>
#include <unordered_map>

#include "Chunk.h"
#include "Coords.h"

namespace vrtc {
    struct LoadedChunk {
        std::unique_ptr<Chunk> chunk;
        uint32_t lastTicketTick = 0; // last tick a ticket covered this position
    };

    class World {
        std::unordered_map<ChunkPos, LoadedChunk, ChunkPosHash> chunks;
        size_t bytes = 0;

    public:
        [[nodiscard]] Chunk *GetChunk(ChunkPos pos); // nullptr when not loaded

        [[nodiscard]] const Chunk *GetChunk(ChunkPos pos) const;

        [[nodiscard]] LoadedChunk *GetLoaded(ChunkPos pos);

        // Air when not loaded. Never call this in a per-block loop (section 6): look the chunk up once instead.
        [[nodiscard]] BlockId GetBlock(BlockPos pos) const;

        // Takes ownership and links the six face neighbours both ways.
        Chunk &Insert(ChunkPos pos, std::unique_ptr<Chunk> chunk, uint32_t tick);

        // Unlinks neighbours and returns the chunk, or nullptr if it was not loaded.
        std::unique_ptr<Chunk> Remove(ChunkPos pos);

        [[nodiscard]] size_t Count() const { return chunks.size(); }

        [[nodiscard]] size_t MemoryBytes() const { return bytes; }

        auto &Chunks() { return chunks; }
    };
}
