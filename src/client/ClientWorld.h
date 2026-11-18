#pragma once
// The client's replica of the chunks the server has sent (section 6). Owned by the main thread. It changes only
// by applying server messages (hard rule 3) and tracks which chunks need a new mesh.
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Chunk.h"
#include "Coords.h"
#include "FlatMap.h"
#include "Mesher.h"
#include "Protocol.h"

namespace vrtc {
    inline constexpr uint32_t NO_GPU_CHUNK = ~0u;

    struct ClientChunk {
        std::unique_ptr<Chunk> chunk;
        uint32_t version = 0; // unique per change applied, so a stale mesh result is recognised
        bool dirty = false; // needs (re)meshing; also listed in the dirty list
        bool inFlight = false; // a mesh job holds a snapshot of it
        uint32_t gpu = NO_GPU_CHUNK; // ChunkRenderer handle
        // Section 9: the quads of each damaged block, rebuilt only when that block's mask changes.
        FlatMap<uint16_t, MicroQuads> microCache;
        uint32_t microStamp = 0; // bumped whenever any mask in this chunk changes; guards cache stores
        uint8_t meshLod = 0; // micro LOD the current or in-flight mesh was built at
        // Sky cover, computed on demand: bit x of row z is set when the column holds an opaque whole block.
        mutable std::array<uint32_t, CHUNK_SIZE> cover{};
        mutable bool coverValid = false;
    };

    class ClientWorld {
        std::unordered_map<ChunkPos, ClientChunk, ChunkPosHash> chunks;
        std::vector<ChunkPos> dirtyList;
        size_t bytes = 0;
        uint32_t nextVersion = 1;
        BlockId belowWorldFill; // what the mesher sees under the lowest chunk row

        void MarkChanged(ClientChunk &cc, ChunkPos pos, int localX, int localY, int localZ, bool allNeighbours);

        [[nodiscard]] const std::array<uint32_t, CHUNK_SIZE> &CoverOf(const ClientChunk &cc, const MeshTables &tables) const;


    public:
        explicit ClientWorld(BlockId belowWorldFill) : belowWorldFill(belowWorldFill) {
        }

        [[nodiscard]] ClientChunk *Get(ChunkPos pos);

        [[nodiscard]] const ClientChunk *Get(ChunkPos pos) const;

        // Inserts or replaces the chunk and marks it and its 26 neighbours dirty. False on a bad record.
        bool ApplyChunkData(const ChunkDataMsg &msg);

        // Applies block and mask changes; neighbours are re-marked when a change touches the chunk border.
        bool ApplyBlockDelta(const BlockDeltaMsg &msg);

        // Removes the chunk and returns its GPU handle for the caller to free (NO_GPU_CHUNK if none).
        uint32_t ApplyUnload(ChunkPos pos);

        // Section 9: meshable only when all six face neighbours are present or outside the world's height.
        [[nodiscard]] bool Meshable(ChunkPos pos, const ClientChunk &cc) const;

        // True when the mesh is known to be empty without running a job: uniform air, or uniform opaque
        // enclosed by uniform opaque neighbours.
        [[nodiscard]] bool KnownEmpty(ChunkPos pos, const ClientChunk &cc, const MeshTables &tables) const;

        // Copies the chunk, its one-block border and its damaged blocks (cached quads where still valid) into
        // the snapshot (hard rule 5).
        void BuildMeshInput(ChunkPos pos, const ClientChunk &cc, const MeshTables &tables, int lod,
                            MeshInput &input) const;

        // Marks every chunk dirty.
        void MarkAllDirty();

        // Marks chunks with damaged blocks whose micro LOD no longer matches their distance to the camera.
        void RefreshLod(ChunkPos cameraChunk);

        // Keeps the quads a mesh job built for damaged blocks, unless the chunk's masks changed meanwhile.
        void StoreMicroCache(const MeshOutput &output);

        void MarkDirty(ChunkPos pos);

        std::vector<ChunkPos> &DirtyList() { return dirtyList; }

        // World queries for the shared raycast.
        [[nodiscard]] BlockId Block(BlockPos pos) const;

        [[nodiscard]] const MicroBlock *Micro(BlockPos pos) const;

        [[nodiscard]] size_t Count() const { return chunks.size(); }

        [[nodiscard]] size_t MemoryBytes() const { return bytes; }
    };

    // Adapter for shared/Raycast.h over the replica and the registry's solid table.
    struct ReplicaQuery {
        const ClientWorld &world;
        const BlockRegistry &blocks;

        [[nodiscard]] BlockId Block(BlockPos pos) const { return world.Block(pos); }

        [[nodiscard]] const MicroBlock *Micro(BlockPos pos) const { return world.Micro(pos); }

        [[nodiscard]] bool Solid(BlockId id) const { return blocks.Solid()[id] != 0; }
    };
}
