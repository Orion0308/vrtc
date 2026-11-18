#include "WorldEdit.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace {
    using namespace vrtc;

    constexpr double MICRO_STEP = 1.0 / MICRO_RES;

    // Centre of micro-voxel 0 in the (y, z) row of block `b`.
    glm::dvec3 RowOrigin(BlockPos b, int my, int mz) {
        return {b.x + MICRO_STEP * 0.5, b.y + (my + 0.5) * MICRO_STEP, b.z + (mz + 0.5) * MICRO_STEP};
    }

    // Walks every block whose box intersects the shape's bounds, grouped by chunk so lookups are rare.
    template<class Fn>
    void ForEachBlock(const Shape &shape, Fn &&fn) {
        const ShapeBounds bounds = Bounds(shape);
        const BlockPos lo = ToBlockPos(bounds.min);
        const BlockPos hi = ToBlockPos(bounds.max);
        for (int y = std::max(lo.y, WORLD_MIN_BLOCK_Y); y <= std::min(hi.y, WORLD_MAX_BLOCK_Y); ++y) {
            for (int z = lo.z; z <= hi.z; ++z) {
                for (int x = lo.x; x <= hi.x; ++x) fn(BlockPos{x, y, z});
            }
        }
    }
}

vrtc::Chunk *vrtc::WorldEdit::ChunkFor(ChunkPos pos, ChunkPos &cachedPos, Chunk *&cached) {
    if (cached && cachedPos == pos) return cached;
    cachedPos = pos;
    cached = world.GetChunk(pos);
    return cached;
}

void vrtc::WorldEdit::Record(ChunkPos pos, Chunk &chunk, uint16_t index) {
    ++chunk.version;
    chunk.dirty |= Chunk::DIRTY_SAVE | Chunk::DIRTY_NAV | Chunk::DIRTY_MODIFIED_SINCE_GEN;
    changed[pos].push_back(index);
}

void vrtc::WorldEdit::SetBlock(BlockPos pos, BlockId id) {
    const ChunkPos cp = ToChunkPos(pos);
    Chunk *chunk = world.GetChunk(cp);
    if (!chunk) return;
    const uint16_t index = BlockIndex(LocalCoord(pos.x), LocalCoord(pos.y), LocalCoord(pos.z));
    if (chunk->Materialise(index)) ++damagedDelta; // a generated slope becomes an ordinary damaged block
    if (chunk->GetIndex(index) == id && !chunk->Micro(index)) return;
    if (chunk->Micro(index)) --damagedDelta;
    chunk->SetIndex(index, id);
    Record(cp, *chunk, index);
}

vrtc::CarveResult vrtc::WorldEdit::Carve(const Shape &shape, uint8_t strength) {
    CarveResult result;
    result.bitsRemovedById.assign(blocks.Count(), 0);
    ChunkPos cachedPos{};
    Chunk *cached = nullptr;

    ForEachBlock(shape, [&](BlockPos b) {
        const ChunkPos cp = ToChunkPos(b);
        Chunk *chunk = ChunkFor(cp, cachedPos, cached);
        if (!chunk) return;
        const uint16_t index = BlockIndex(LocalCoord(b.x), LocalCoord(b.y), LocalCoord(b.z));
        const BlockId id = chunk->GetIndex(index);
        if (id == BLOCK_AIR) return;
        const BlockDef &def = blocks.Get(id);
        if (def.unbreakable || def.hardness > strength) return;
        if (chunk->Materialise(index)) ++damagedDelta; // a generated slope becomes an ordinary damaged block

        const glm::dvec3 min(b.x, b.y, b.z);
        if (ContainsBox(shape, min, min + glm::dvec3(1))) {
            // 3. The whole block is inside: it becomes air.
            const MicroBlock *mask = chunk->Micro(index);
            const uint32_t removed = mask ? mask->Count() : MicroBlock::BITS;
            if (mask) --damagedDelta;
            chunk->SetIndex(index, BLOCK_AIR);
            Record(cp, *chunk, index);
            result.bitsRemovedById[id] += removed;
            result.totalBits += removed;
            ++result.blocksTouched;
            return;
        }

        // 4. Partial: get or create the mask and clear every bit whose centre is inside.
        const bool existed = chunk->Micro(index) != nullptr;
        MicroBlock &mask = chunk->GetOrCreateMicro(index);
        uint32_t removed = 0;
        for (int my = 0; my < MICRO_RES; ++my) {
            for (int mz = 0; mz < MICRO_RES; ++mz) {
                const uint16_t inside = RowBitsInside(shape, RowOrigin(b, my, mz), MICRO_STEP);
                if (!inside) continue;
                const uint16_t cleared = mask.rows[my][mz] & inside;
                if (!cleared) continue;
                removed += static_cast<uint32_t>(std::popcount(cleared));
                mask.rows[my][mz] &= static_cast<uint16_t>(~inside);
            }
        }
        if (removed == 0) {
            if (!existed) chunk->RemoveMicro(index); // an all-ones mask is never stored
            return;
        }
        // 5. Normalise.
        if (mask.IsEmpty()) {
            chunk->SetIndex(index, BLOCK_AIR); // drops the mask
            if (existed) --damagedDelta;
        } else if (!existed) {
            ++damagedDelta;
        }
        Record(cp, *chunk, index);
        result.bitsRemovedById[id] += removed;
        result.totalBits += removed;
        ++result.blocksTouched;
    });
    return result;
}

vrtc::FillResult vrtc::WorldEdit::Fill(const Shape &shape, BlockId fillId) {
    FillResult result;
    if (fillId == BLOCK_AIR || fillId >= blocks.Count()) return result;
    ChunkPos cachedPos{};
    Chunk *cached = nullptr;

    ForEachBlock(shape, [&](BlockPos b) {
        const ChunkPos cp = ToChunkPos(b);
        Chunk *chunk = ChunkFor(cp, cachedPos, cached);
        if (!chunk) return;
        const uint16_t index = BlockIndex(LocalCoord(b.x), LocalCoord(b.y), LocalCoord(b.z));
        const BlockId id = chunk->GetIndex(index);
        const bool air = id == BLOCK_AIR;
        if (!air && id == fillId && chunk->Materialise(index)) ++damagedDelta;
        // A damaged block cannot have a different block placed into its gaps; a whole block of anything is full.
        if (!air && (id != fillId || !chunk->Micro(index))) return;

        const glm::dvec3 min(b.x, b.y, b.z);
        if (air && ContainsBox(shape, min, min + glm::dvec3(1))) {
            chunk->SetIndex(index, fillId);
            Record(cp, *chunk, index);
            result.bitsAdded += MicroBlock::BITS;
            ++result.blocksTouched;
            return;
        }

        MicroBlock inside = MicroBlock::Empty();
        uint32_t insideBits = 0;
        for (int my = 0; my < MICRO_RES; ++my) {
            for (int mz = 0; mz < MICRO_RES; ++mz) {
                inside.rows[my][mz] = RowBitsInside(shape, RowOrigin(b, my, mz), MICRO_STEP);
                insideBits += static_cast<uint32_t>(std::popcount(inside.rows[my][mz]));
            }
        }
        if (insideBits == 0) return;

        if (air) {
            chunk->SetIndex(index, fillId); // whole for a moment
            if (insideBits < MicroBlock::BITS) {
                chunk->GetOrCreateMicro(index) = inside;
                ++damagedDelta;
            }
            Record(cp, *chunk, index);
            result.bitsAdded += insideBits;
            ++result.blocksTouched;
            return;
        }

        // Repair: set the bits inside, then normalise an all-ones mask back to a whole block.
        MicroBlock &mask = *chunk->MicroMutable(index);
        uint32_t added = 0;
        for (int my = 0; my < MICRO_RES; ++my) {
            for (int mz = 0; mz < MICRO_RES; ++mz) {
                const uint16_t gained = static_cast<uint16_t>(inside.rows[my][mz] & ~mask.rows[my][mz]);
                if (!gained) continue;
                added += static_cast<uint32_t>(std::popcount(gained));
                mask.rows[my][mz] |= gained;
            }
        }
        if (added == 0) return;
        if (mask.IsFull()) {
            chunk->RemoveMicro(index);
            --damagedDelta;
        }
        Record(cp, *chunk, index);
        result.bitsAdded += added;
        ++result.blocksTouched;
    });
    return result;
}

uint32_t vrtc::WorldEdit::RemoveBlocks(const Shape &shape, uint8_t strength) {
    uint32_t count = 0;
    ChunkPos cachedPos{};
    Chunk *cached = nullptr;
    ForEachBlock(shape, [&](BlockPos b) {
        if (!Contains(shape, glm::dvec3(b.x + 0.5, b.y + 0.5, b.z + 0.5))) return;
        const ChunkPos cp = ToChunkPos(b);
        Chunk *chunk = ChunkFor(cp, cachedPos, cached);
        if (!chunk) return;
        const uint16_t index = BlockIndex(LocalCoord(b.x), LocalCoord(b.y), LocalCoord(b.z));
        const BlockId id = chunk->GetIndex(index);
        if (id == BLOCK_AIR) return;
        const BlockDef &def = blocks.Get(id);
        if (def.unbreakable || def.hardness > strength) return;
        if (chunk->Micro(index)) --damagedDelta;
        chunk->SetIndex(index, BLOCK_AIR); // also drops a generated slope
        Record(cp, *chunk, index);
        ++count;
    });
    return count;
}

uint32_t vrtc::WorldEdit::PlaceBlocks(const Shape &shape, BlockId id) {
    uint32_t count = 0;
    if (id == BLOCK_AIR || id >= blocks.Count()) return count;
    ChunkPos cachedPos{};
    Chunk *cached = nullptr;
    ForEachBlock(shape, [&](BlockPos b) {
        if (!Contains(shape, glm::dvec3(b.x + 0.5, b.y + 0.5, b.z + 0.5))) return;
        const ChunkPos cp = ToChunkPos(b);
        Chunk *chunk = ChunkFor(cp, cachedPos, cached);
        if (!chunk) return;
        const uint16_t index = BlockIndex(LocalCoord(b.x), LocalCoord(b.y), LocalCoord(b.z));
        if (chunk->GetIndex(index) != BLOCK_AIR) return; // the target cell must be air
        chunk->SetIndex(index, id);
        Record(cp, *chunk, index);
        ++count;
    });
    return count;
}
