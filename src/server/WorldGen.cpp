#include "WorldGen.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

#include "Config.h"
#include "FastNoiseLite.h"
#include "MicroBlock.h"

namespace {
    using namespace vrtc;

    constexpr int L = WORLDGEN_LATTICE; // lattice spacing in blocks
    constexpr int LN = CHUNK_SIZE / L + 1; // lattice samples across the chunk (0, 4, ..., 32)
    constexpr int LNY = LN + 1; // one extra layer above, so the top rows know what is above them
    constexpr int ROWS = CHUNK_SIZE + L; // block rows classified per column: the chunk plus 4 above
    constexpr int GX = CHUNK_SIZE + 1; // block-corner grid: 33 corners across
    constexpr int GY = ROWS + 1; // and 37 up
    constexpr float TUNNEL_R2 = WORLDGEN_TUNNEL_RADIUS * WORLDGEN_TUNNEL_RADIUS;
    // The cave fields are scaled to roughly one unit per block so that min() with the density blends cleanly.
    constexpr float TUNNEL_SCALE = 50.f, CHAMBER_SCALE = 25.f;

    static_assert(CHUNK_SIZE % L == 0);
    static_assert(ROWS <= 64, "column solidity is kept in a 64-bit mask");

    inline int LatticeIndex(int lx, int ly, int lz) { return (ly * LN + lz) * LN + lx; }

    inline int GridIndex(int x, int y, int z) { return (y * GX + z) * GX + x; }

}

struct vrtc::WorldGen::Noise {
    FastNoiseLite height, density, tunnelA, tunnelB, chamber;
};

vrtc::WorldGen::WorldGen(uint64_t seed, WorldGenBlocks blocks) : blocks(blocks) {
    auto n = std::make_unique<Noise>();
    const int s = static_cast<int>(seed ^ (seed >> 32));

    n->height.SetSeed(s);
    n->height.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    n->height.SetFractalType(FastNoiseLite::FractalType_FBm);
    n->height.SetFractalOctaves(4);
    n->height.SetFrequency(WORLDGEN_HEIGHT_FREQUENCY);

    n->density.SetSeed(s + 1);
    n->density.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    n->density.SetFractalType(FastNoiseLite::FractalType_FBm);
    n->density.SetFractalOctaves(3);
    n->density.SetFrequency(WORLDGEN_DENSITY_FREQUENCY);

    n->tunnelA.SetSeed(s + 2);
    n->tunnelA.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    n->tunnelA.SetFrequency(WORLDGEN_TUNNEL_FREQUENCY);

    n->tunnelB.SetSeed(s + 3);
    n->tunnelB.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    n->tunnelB.SetFrequency(WORLDGEN_TUNNEL_FREQUENCY);

    n->chamber.SetSeed(s + 4);
    n->chamber.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    n->chamber.SetFractalType(FastNoiseLite::FractalType_FBm);
    n->chamber.SetFractalOctaves(1);
    n->chamber.SetFrequency(WORLDGEN_CHAMBER_FREQUENCY);

    noise = std::move(n);
}

vrtc::WorldGen::~WorldGen() = default;

std::unique_ptr<vrtc::Chunk> vrtc::WorldGen::Generate(ChunkPos pos) const {
    const BlockPos origin = ChunkOrigin(pos);
    const bool bottomChunk = pos.y == WORLD_MIN_CHUNK_Y;

    // 1. Nothing can be solid this high: uniform air without sampling.
    if (origin.y > WORLDGEN_MAX_SURFACE_Y) return std::make_unique<Chunk>(BLOCK_AIR);

    // 2. Sample one solidity field on the lattice: terrain density (3D noise plus a height bias), cut by the
    //    cave fields. Positive is solid.
    std::array<float, LN * LN> heights{};
    for (int lz = 0; lz < LN; ++lz) {
        for (int lx = 0; lx < LN; ++lx) {
            const auto wx = static_cast<float>(origin.x + lx * L);
            const auto wz = static_cast<float>(origin.z + lz * L);
            heights[lz * LN + lx] = WORLDGEN_BASE_HEIGHT + WORLDGEN_HEIGHT_AMPLITUDE * noise->height.GetNoise(wx, wz);
        }
    }

    std::array<float, LNY * LN * LN> lattice{};
    bool anySolid = false, allSolid = true;
    for (int ly = 0; ly < LNY; ++ly) {
        const int wyInt = origin.y + ly * L;
        const auto wy = static_cast<float>(wyInt);
        for (int lz = 0; lz < LN; ++lz) {
            const auto wz = static_cast<float>(origin.z + lz * L);
            for (int lx = 0; lx < LN; ++lx) {
                const auto wx = static_cast<float>(origin.x + lx * L);
                float value = noise->density.GetNoise(wx, wy, wz) * WORLDGEN_DENSITY_AMPLITUDE
                              + (heights[lz * LN + lx] - wy);
                if (wyInt < WORLDGEN_FLOOR_START_Y) value += static_cast<float>(WORLDGEN_FLOOR_START_Y - wyInt) * 2.f;
                if (wyInt >= WORLDGEN_CAVE_MIN_Y) {
                    const float a = noise->tunnelA.GetNoise(wx, wy, wz);
                    const float b = noise->tunnelB.GetNoise(wx, wy, wz);
                    value = std::min(value, (a * a + b * b - TUNNEL_R2) * TUNNEL_SCALE); // tunnels
                    value = std::min(value, (WORLDGEN_CHAMBER_THRESHOLD - noise->chamber.GetNoise(wx, wy, wz))
                                            * CHAMBER_SCALE); // chambers
                }
                lattice[LatticeIndex(lx, ly, lz)] = value;
                // Interpolated values stay inside the range of their corners, so a lattice that is uniformly
                // solid or uniformly air makes the whole chunk so.
                if (value > 0.f) anySolid = true;
                else allSolid = false;
            }
        }
    }

    if (!anySolid) return std::make_unique<Chunk>(BLOCK_AIR);
    // Everything solid including the rows above: every block is at least 4 deep, so all stone.
    if (allSolid && !bottomChunk) return std::make_unique<Chunk>(blocks.stone);

    // 3. Trilinear interpolation of the lattice onto every block corner of the chunk and the 4 rows above it.
    static thread_local std::vector<float> grid;
    grid.resize(static_cast<size_t>(GY) * GX * GX);
    for (int y = 0; y < GY; ++y) {
        const int cy = std::min(y / L, LNY - 2);
        const float fy = static_cast<float>(y - cy * L) / L;
        for (int z = 0; z < GX; ++z) {
            const int cz = std::min(z / L, LN - 2);
            const float fz = static_cast<float>(z - cz * L) / L;
            for (int x = 0; x < GX; ++x) {
                const int cx = std::min(x / L, LN - 2);
                const float fx = static_cast<float>(x - cx * L) / L;
                const auto at = [&](int dx, int dy, int dz) { return lattice[LatticeIndex(cx + dx, cy + dy, cz + dz)]; };
                const float x00 = at(0, 0, 0) + (at(1, 0, 0) - at(0, 0, 0)) * fx;
                const float x01 = at(0, 0, 1) + (at(1, 0, 1) - at(0, 0, 1)) * fx;
                const float x10 = at(0, 1, 0) + (at(1, 1, 0) - at(0, 1, 0)) * fx;
                const float x11 = at(0, 1, 1) + (at(1, 1, 1) - at(0, 1, 1)) * fx;
                const float z0 = x00 + (x01 - x00) * fz, z1 = x10 + (x11 - x10) * fz;
                grid[GridIndex(x, y, z)] = z0 + (z1 - z0) * fy;
            }
        }
    }

    // 4. Classify blocks from their eight corners: all positive is a whole block, none is air, a mix is a block
    //    the surface cuts through. With WORLDGEN_SMOOTH_SLOPES such a block becomes a SlopeBlock: its corner values
    //    are kept (18 bytes) and define which micro-voxels are solid, so slopes step by 1/16 block. Otherwise it
    //    is whole when most of its corners are solid.
    std::array<uint64_t, CHUNK_AREA> solidColumn{}; // per column, one bit per row: block is solid (whole or part)
    std::array<uint64_t, CHUNK_AREA> partialColumn{}; // and whether it is a slope block
    std::vector<SlopeBlock> slopes; // in ascending block index order, as the loops below visit them
    MicroBlock scratch;
    for (int y = 0; y < ROWS; ++y) {
        const bool inChunk = y < CHUNK_SIZE;
        const bool bedrockRow = bottomChunk && y == 0;
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                // Quantise first: the stored corners, not the floats, decide everything, so the server and
                // every client derive exactly the same micro-voxels.
                SlopeBlock slope;
                for (int k = 0; k < 8; ++k) { // bit 0 = x, bit 1 = z, bit 2 = y
                    slope.corner[k] = QuantiseSlope(grid[GridIndex(x + (k & 1), y + ((k >> 2) & 1), z + ((k >> 1) & 1))]);
                }
                const int positive = SlopePositiveCorners(slope.corner);
                bool solid = positive == 8 || bedrockRow;
                if (!solid && positive > 0) {
                    if (WORLDGEN_SMOOTH_SLOPES && inChunk) {
                        const uint32_t count = ExpandSlope(slope.corner, scratch);
                        solid = count > 0;
                        // All ones is a whole block and all zeros is air; neither is stored as partial.
                        if (count > 0 && count < MicroBlock::BITS) {
                            slope.index = BlockIndex(x, y, z);
                            slopes.push_back(slope);
                            partialColumn[z * CHUNK_SIZE + x] |= 1ull << y;
                        }
                    } else {
                        solid = positive >= 4;
                    }
                }
                if (solid) solidColumn[z * CHUNK_SIZE + x] |= 1ull << y;
            }
        }
    }

    // 5. Materials per column, top down: the first solid block under air is grass, the next few are dirt, the
    //    rest stone. The lowest row of the world is bedrock.
    auto chunk = std::make_unique<Chunk>();
    BlockId *out = chunk->ExpandForWrite();
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            const uint64_t column = solidColumn[z * CHUNK_SIZE + x];
            const uint64_t partialBits = partialColumn[z * CHUNK_SIZE + x];
            int depth = -1; // -1 while in air, otherwise blocks below the first air
            for (int y = ROWS - 1; y >= 0; --y) {
                const bool solid = (column >> y) & 1;
                depth = solid ? depth + 1 : -1;
                const int materialDepth = depth;
                // A slope block at the surface only partly covers the block beneath it, so that block is
                // surface too: do not count the slope block as cover.
                if (solid && depth == 0 && ((partialBits >> y) & 1)) depth = -1;
                if (y >= CHUNK_SIZE) continue; // rows above the chunk only seed the depth count
                BlockId id = BLOCK_AIR;
                if (bottomChunk && y == 0) {
                    id = blocks.bedrock;
                } else if (solid) {
                    id = materialDepth == 0 ? blocks.grass
                         : materialDepth <= WORLDGEN_DIRT_DEPTH ? blocks.dirt : blocks.stone;
                }
                out[BlockIndex(x, y, z)] = id;
            }
        }
    }

    // 6. Attach the slope blocks. Each keeps its BlockId; its corners say which sixteenths remain.
    chunk->SetSlopes(std::move(slopes));
    chunk->Compact();
    return chunk;
}
