#pragma once
// Chunk meshing (section 9). Runs on worker threads from a snapshot built on the main thread.
//
//   Pass 1 (whole blocks): binary greedy meshing keyed by texture layer and the four corner AO values.
//   Pass 2 (damaged blocks): each block is meshed on its own grid straight from the 16-bit mask rows. Distant
//   chunks use a coarser grid (micro LOD: 16^3, 8^3 or 4^3). Each block's quads are cached on the main thread and
//   reused until its mask, its boundary neighbours or the LOD change.
//   Quads come out grouped by face direction so the renderer can skip directions that face away.
//
//   Naive surface nets over a density field: per-block fill (1 solid, 0 air, the mask's fill fraction for a
//   damaged block) blurred with a 3x3x3 box, iso-surface at one half. Vertices carry gradient normals.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "BlockRegistry.h"
#include "ChunkVertex.h"
#include "Coords.h"
#include "MicroBlock.h"

namespace vrtc {
    inline constexpr int MESH_PAD = CHUNK_SIZE + 2; // 34: the chunk plus a one-block border
    inline constexpr int MESH_PAD_AREA = MESH_PAD * MESH_PAD;
    inline constexpr int MESH_PAD_VOLUME = MESH_PAD * MESH_PAD_AREA;

    // x fastest, then z, then y: the same axis order as BlockIndex, so x-runs copy with memcpy.
    constexpr int PadIndex(int x, int y, int z) { return x + MESH_PAD * (z + MESH_PAD * y); }


    // Micro LOD of a chunk: 0 = 16^3 per damaged block, 1 = 8^3, 2 = 4^3, 3 = whole block or nothing.
    inline int MicroLodFor(ChunkPos chunk, ChunkPos camera) {
        const int d = std::max({std::abs(chunk.x - camera.x), std::abs(chunk.y - camera.y), std::abs(chunk.z - camera.z)});
        return d <= MICRO_LOD0_CHUNKS ? 0 : d <= MICRO_LOD1_CHUNKS ? 1 : d <= MICRO_LOD2_CHUNKS ? 2 : 3;
    }

    // Packed per-block-ID tables the hot loop reads (hard rule 7). Built once from the registry.
    struct MeshTables {
        std::vector<uint8_t> opaque; // hides neighbours' faces (when whole)
        std::vector<uint8_t> visible; // gets faces of its own
        std::vector<uint16_t> faceLayer; // texture array layer, indexed id * 6 + face
    };

    // Quads of one damaged block, grouped by face: face f is quads[faceStart[f] .. faceStart[f + 1]).
    struct MicroMesh {
        std::vector<ChunkQuad> quads;
        std::array<uint32_t, 7> faceStart{};
    };

    // A cache entry. It is valid only for the key it was built with (boundary-culling bits in the low byte, the
    // sky light of its cell in the high byte) and for its LOD.
    struct MicroQuads {
        uint16_t blocked = 0;
        uint8_t lod = 0;
        std::shared_ptr<const MicroMesh> mesh;
    };

    // One partial block handed to the worker. Kept small: a chunk on a slope has thousands of them.
    struct MicroEntry {
        uint16_t index = 0; // block index inside the chunk
        uint16_t cachedBlocked = 0; // key `cached` was built with; the worker compares it with its own
        int32_t maskIndex = -1; // into MeshInput::masks for an edited block; -1 for a generated slope
        int16_t corner[8]{}; // the slope's corner values when maskIndex is -1
        std::shared_ptr<const MicroMesh> cached; // previous quads, reused if the boundary bits still match
    };

    struct MeshInput {
        ChunkPos pos{};
        uint32_t version = 0;
        uint32_t microStamp = 0; // the chunk's damaged-block generation the entries were taken from
        uint8_t lod = 0;
        std::vector<BlockId> blocks; // MESH_PAD_VOLUME
        std::vector<uint8_t> whole; // MESH_PAD_VOLUME: 0 where the block has a mask (not opaque-whole)
        std::vector<MicroEntry> micro; // partial blocks inside the chunk: edited masks and generated slopes
        std::vector<MicroBlock> masks; // the masks of edited blocks, referenced by MicroEntry::maskIndex
        // Sky cover: bit x of row z is set when some opaque block lies above the snapshot in that padded column.
        std::array<uint64_t, MESH_PAD> coveredAbove{};
    };

    struct MeshOutput {
        ChunkPos pos{};
        uint32_t version = 0;
        uint32_t microStamp = 0;
        uint8_t lod = 0;
        std::vector<ChunkQuad> quads; // grouped by face
        std::array<uint32_t, 7> faceStart{};
        std::vector<std::pair<uint16_t, MicroQuads>> builtMicro; // freshly built blocks, for the cache
        uint32_t wholeQuads = 0;
        uint32_t microQuads = 0;
        uint32_t microRebuilt = 0;
        uint32_t microReused = 0;
        double workerMs = 0;
    };

    MeshOutput MeshChunk(const MeshInput &input, const MeshTables &tables);
}
