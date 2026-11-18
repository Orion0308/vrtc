#pragma once
// The world edit API (section 7): the one way to change block or micro-voxel data in a loaded world (hard rule 4).
// Every change bumps the chunk's version, sets its dirty flags and records the block for the end-of-tick
// ChunkChanged event, which the server turns into BlockDelta messages.
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "BlockRegistry.h"
#include "Coords.h"
#include "Shapes.h"
#include "World.h"

namespace vrtc {
    struct CarveResult {
        std::vector<uint32_t> bitsRemovedById; // indexed by BlockId; 4096 bits make one block item
        uint32_t blocksTouched = 0;
        uint32_t totalBits = 0;
    };

    struct FillResult {
        uint32_t bitsAdded = 0;
        uint32_t blocksTouched = 0;
    };

    class WorldEdit {
        World &world;
        const BlockRegistry &blocks;
        std::unordered_map<ChunkPos, std::vector<uint16_t>, ChunkPosHash> changed; // per edited chunk this tick
        int64_t damagedDelta = 0; // change in the number of masks since the last Flush

        // Looks the chunk up once per chunk while walking a shape's bounding box.
        Chunk *ChunkFor(ChunkPos pos, ChunkPos &cachedPos, Chunk *&cached);

        void Record(ChunkPos pos, Chunk &chunk, uint16_t index);

    public:
        WorldEdit(World &world, const BlockRegistry &blocks) : world(world), blocks(blocks) {
        }

        // Places or removes a whole block. No effect on unloaded chunks.
        void SetBlock(BlockPos pos, BlockId id);

        // Removes the micro-voxels inside the shape from every breakable block with hardness <= strength.
        CarveResult Carve(const Shape &shape, uint8_t strength);

        // Adds micro-voxels of `id` inside the shape: fills air, repairs damaged blocks of the same material.
        FillResult Fill(const Shape &shape, BlockId id);

        // Whole-block variants for the test tool: every block whose centre is inside the shape.
        uint32_t RemoveBlocks(const Shape &shape, uint8_t strength);

        uint32_t PlaceBlocks(const Shape &shape, BlockId id);

        [[nodiscard]] bool HasChanges() const { return !changed.empty(); }

        // Emits one ChunkChanged per edited chunk: callback(ChunkPos, Chunk&, sorted unique block indices).
        template<class Callback>
        void Flush(Callback &&callback) {
            for (auto &[pos, indices]: changed) {
                Chunk *chunk = world.GetChunk(pos);
                if (!chunk) continue; // unloaded during the tick
                std::sort(indices.begin(), indices.end());
                indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
                callback(pos, *chunk, std::span<const uint16_t>(indices));
            }
            changed.clear();
        }

        // Net change in damaged-block count since the last call; resets to zero.
        int64_t TakeDamagedDelta() {
            const int64_t d = damagedDelta;
            damagedDelta = 0;
            return d;
        }
    };
}
