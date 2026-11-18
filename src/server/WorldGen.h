#pragma once
// Terrain generation (section 8): a pure function of (seed, ChunkPos). Runs on workers and never touches World.
#include <cstdint>
#include <memory>

#include "Chunk.h"
#include "Coords.h"

class FastNoiseLite;

namespace vrtc {
    // Block IDs the generator places, resolved from the registry by name (hard rule 9).
    struct WorldGenBlocks {
        BlockId stone, dirt, grass, bedrock;
    };

    class WorldGen {
        struct Noise;
        std::unique_ptr<const Noise> noise; // configured once; GetNoise is const and safe to share across workers
        WorldGenBlocks blocks;

    public:
        WorldGen(uint64_t seed, WorldGenBlocks blocks);

        ~WorldGen();

        // Returns a uniform chunk without filling an array when the chunk is all air or all stone.
        [[nodiscard]] std::unique_ptr<Chunk> Generate(ChunkPos pos) const;
    };
}
