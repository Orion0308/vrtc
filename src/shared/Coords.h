#pragma once
// Coordinate types and conversions (section 5). Hard rule 8: floor, never truncate; shift and mask, never divide.
#include <cmath>
#include <cstdint>
#include <glm/vec3.hpp>

#include "Config.h"

namespace vrtc {
    struct BlockPos {
        int32_t x, y, z;

        bool operator==(const BlockPos &) const = default;
    };

    struct ChunkPos {
        int32_t x, y, z;

        bool operator==(const ChunkPos &) const = default;
    };

    inline int32_t FloorToInt(double v) { return static_cast<int32_t>(std::floor(v)); }

    inline BlockPos ToBlockPos(glm::dvec3 p) { return {FloorToInt(p.x), FloorToInt(p.y), FloorToInt(p.z)}; }

    // Arithmetic shift is well defined for negative values in C++20, so this is exact for all coordinates.
    constexpr ChunkPos ToChunkPos(BlockPos b) { return {b.x >> CHUNK_BITS, b.y >> CHUNK_BITS, b.z >> CHUNK_BITS}; }

    constexpr BlockPos ChunkOrigin(ChunkPos c) {
        return {c.x << CHUNK_BITS, c.y << CHUNK_BITS, c.z << CHUNK_BITS};
    }

    constexpr int LocalCoord(int32_t v) { return v & CHUNK_MASK; }

    // Block index inside a chunk: x | z << 5 | y << 10, so x-runs are contiguous.
    constexpr uint16_t BlockIndex(int x, int y, int z) {
        return static_cast<uint16_t>(x | (z << CHUNK_BITS) | (y << (2 * CHUNK_BITS)));
    }

    constexpr bool ChunkYInWorld(int32_t chunkY) {
        return chunkY >= WORLD_MIN_CHUNK_Y && chunkY < WORLD_MAX_CHUNK_Y;
    }

    constexpr ChunkPos Offset(ChunkPos c, int dx, int dy, int dz) { return {c.x + dx, c.y + dy, c.z + dz}; }

    constexpr int64_t DistanceSq(ChunkPos a, ChunkPos b) {
        const int64_t dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // True when `c` lies in the box of `rh` chunks horizontally and `rv` vertically around `centre`.
    constexpr bool WithinRadius(ChunkPos c, ChunkPos centre, int rh, int rv) {
        const int dx = c.x - centre.x, dy = c.y - centre.y, dz = c.z - centre.z;
        return dx >= -rh && dx <= rh && dz >= -rh && dz <= rh && dy >= -rv && dy <= rv;
    }

    struct ChunkPosHash {
        size_t operator()(ChunkPos p) const noexcept {
            uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(p.x)) * 0x9E3779B97F4A7C15ull;
            h ^= static_cast<uint64_t>(static_cast<uint32_t>(p.y)) * 0xC2B2AE3D27D4EB4Full + (h >> 29);
            h ^= static_cast<uint64_t>(static_cast<uint32_t>(p.z)) * 0x165667B19E3779F9ull + (h >> 31);
            h ^= h >> 33;
            h *= 0xFF51AFD7ED558CCDull;
            h ^= h >> 33;
            return static_cast<size_t>(h);
        }
    };
}
