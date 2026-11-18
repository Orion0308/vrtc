#include "World.h"

#include "Faces.h"

vrtc::Chunk *vrtc::World::GetChunk(ChunkPos pos) {
    const auto it = chunks.find(pos);
    return it == chunks.end() ? nullptr : it->second.chunk.get();
}

const vrtc::Chunk *vrtc::World::GetChunk(ChunkPos pos) const {
    const auto it = chunks.find(pos);
    return it == chunks.end() ? nullptr : it->second.chunk.get();
}

vrtc::LoadedChunk *vrtc::World::GetLoaded(ChunkPos pos) {
    const auto it = chunks.find(pos);
    return it == chunks.end() ? nullptr : &it->second;
}

vrtc::BlockId vrtc::World::GetBlock(BlockPos pos) const {
    const Chunk *chunk = GetChunk(ToChunkPos(pos));
    if (!chunk) return BLOCK_AIR;
    return chunk->Get(LocalCoord(pos.x), LocalCoord(pos.y), LocalCoord(pos.z));
}

vrtc::Chunk &vrtc::World::Insert(ChunkPos pos, std::unique_ptr<Chunk> chunk, uint32_t tick) {
    Chunk &c = *chunk;
    bytes += c.MemoryBytes();
    for (int f = 0; f < FACE_COUNT; ++f) {
        const IVec3 n = FACE_NORMAL[f];
        if (Chunk *neighbour = GetChunk(Offset(pos, n.x, n.y, n.z))) {
            c.neighbours[f] = neighbour;
            neighbour->neighbours[FACE_OPPOSITE[f]] = &c;
        }
    }
    chunks[pos] = LoadedChunk{std::move(chunk), tick};
    return c;
}

std::unique_ptr<vrtc::Chunk> vrtc::World::Remove(ChunkPos pos) {
    const auto it = chunks.find(pos);
    if (it == chunks.end()) return nullptr;
    std::unique_ptr<Chunk> chunk = std::move(it->second.chunk);
    chunks.erase(it);
    bytes -= chunk->MemoryBytes();
    for (int f = 0; f < FACE_COUNT; ++f) {
        if (Chunk *neighbour = chunk->neighbours[f]) {
            neighbour->neighbours[FACE_OPPOSITE[f]] = nullptr;
            chunk->neighbours[f] = nullptr;
        }
    }
    return chunk;
}
