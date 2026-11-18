#include "ClientWorld.h"

#include <cstring>

#include "ChunkSerial.h"
#include "Faces.h"

vrtc::ClientChunk *vrtc::ClientWorld::Get(ChunkPos pos) {
    const auto it = chunks.find(pos);
    return it == chunks.end() ? nullptr : &it->second;
}

const vrtc::ClientChunk *vrtc::ClientWorld::Get(ChunkPos pos) const {
    const auto it = chunks.find(pos);
    return it == chunks.end() ? nullptr : &it->second;
}

void vrtc::ClientWorld::MarkChanged(ClientChunk &cc, ChunkPos pos, int localX, int localY, int localZ,
                                    bool allNeighbours) {
    cc.version = nextVersion++;
    cc.coverValid = false;
    // A change on local coordinate 0 or 31 alters the neighbour's border faces too (section 7).
    const int xs[3] = {0, allNeighbours || localX == 0 ? -1 : 0, allNeighbours || localX == CHUNK_MASK ? 1 : 0};
    const int ys[3] = {0, allNeighbours || localY == 0 ? -1 : 0, allNeighbours || localY == CHUNK_MASK ? 1 : 0};
    const int zs[3] = {0, allNeighbours || localZ == 0 ? -1 : 0, allNeighbours || localZ == CHUNK_MASK ? 1 : 0};
    for (const int dy: ys) {
        for (const int dz: zs) {
            for (const int dx: xs) MarkDirty(Offset(pos, dx, dy, dz));
        }
    }
}

bool vrtc::ClientWorld::ApplyChunkData(const ChunkDataMsg &msg) {
    auto [it, isNew] = chunks.try_emplace(msg.pos);
    ClientChunk &cc = it->second;
    if (isNew) {
        cc.chunk = std::make_unique<Chunk>();
    } else {
        bytes -= cc.chunk->MemoryBytes();
    }

    ByteReader reader(msg.record);
    if (!ReadChunkRecord(reader, *cc.chunk)) {
        if (isNew) chunks.erase(it);
        return false;
    }
    cc.chunk->version = msg.version;
    cc.microCache.Clear();
    ++cc.microStamp;
    bytes += cc.chunk->MemoryBytes();

    if (isNew) {
        for (int f = 0; f < FACE_COUNT; ++f) {
            const IVec3 n = FACE_NORMAL[f];
            if (ClientChunk *neighbour = Get(Offset(msg.pos, n.x, n.y, n.z))) {
                cc.chunk->neighbours[f] = neighbour->chunk.get();
                neighbour->chunk->neighbours[FACE_OPPOSITE[f]] = cc.chunk.get();
            }
        }
    }
    // New data changes this chunk's faces and the border faces and ambient occlusion of everything around it.
    MarkChanged(cc, msg.pos, 0, 0, 0, true);
    // Anything but air can shade what lies beneath: chunks further down in these columns need new light.
    if (!(cc.chunk->IsUniform() && cc.chunk->UniformId() == BLOCK_AIR)) {
        for (int cy = msg.pos.y - 2; cy >= WORLD_MIN_CHUNK_Y; --cy) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) MarkDirty(ChunkPos{msg.pos.x + dx, cy, msg.pos.z + dz});
            }
        }
    }
    return true;
}

bool vrtc::ClientWorld::ApplyBlockDelta(const BlockDeltaMsg &msg) {
    ClientChunk *cc = Get(msg.pos);
    if (!cc) return false; // a delta for a chunk we do not hold: the server's ChunkData will carry the change
    Chunk &chunk = *cc->chunk;
    bytes -= chunk.MemoryBytes();
    for (const BlockDeltaEntry &e: msg.entries) {
        chunk.SetIndex(e.index, e.id); // drops any old mask
        if (e.hasMask) chunk.GetOrCreateMicro(e.index) = e.mask;
        cc->microCache.Erase(e.index);
        MarkChanged(*cc, msg.pos, e.index & CHUNK_MASK, e.index >> (2 * CHUNK_BITS), (e.index >> CHUNK_BITS) & CHUNK_MASK,
                    false);
    }
    ++cc->microStamp;
    chunk.version = msg.version;
    bytes += chunk.MemoryBytes();
    return true;
}

uint32_t vrtc::ClientWorld::ApplyUnload(ChunkPos pos) {
    const auto it = chunks.find(pos);
    if (it == chunks.end()) return NO_GPU_CHUNK;
    ClientChunk &cc = it->second;
    bytes -= cc.chunk->MemoryBytes();
    for (int f = 0; f < FACE_COUNT; ++f) {
        if (Chunk *neighbour = cc.chunk->neighbours[f]) neighbour->neighbours[FACE_OPPOSITE[f]] = nullptr;
    }
    const uint32_t gpu = cc.gpu;
    chunks.erase(it); // any dirty-list entry is dropped when next scanned
    return gpu;
}

bool vrtc::ClientWorld::Meshable(ChunkPos pos, const ClientChunk &cc) const {
    for (int f = 0; f < FACE_COUNT; ++f) {
        if (cc.chunk->neighbours[f]) continue;
        const IVec3 n = FACE_NORMAL[f];
        if (n.y == 0 || ChunkYInWorld(pos.y + n.y)) return false; // a real neighbour is missing
    }
    return true;
}

bool vrtc::ClientWorld::KnownEmpty(ChunkPos, const ClientChunk &cc, const MeshTables &tables) const {
    const Chunk &chunk = *cc.chunk;
    if (!chunk.IsUniform()) return false; // a damaged chunk is never uniform
    if (!tables.visible[chunk.UniformId()]) return true; // uniform air (or other faceless block)
    if (!tables.opaque[chunk.UniformId()]) return false;
    for (const Chunk *neighbour: chunk.neighbours) {
        // A missing neighbour here is outside the world: air above (faces would show), solid below.
        if (!neighbour) return false;
        if (!neighbour->IsUniform() || !tables.opaque[neighbour->UniformId()]) return false;
    }
    return true;
}

void vrtc::ClientWorld::MarkAllDirty() {
    for (auto &[pos, cc]: chunks) {
        if (cc.dirty) continue;
        cc.dirty = true;
        dirtyList.push_back(pos);
    }
}

void vrtc::ClientWorld::RefreshLod(ChunkPos cameraChunk) {
    for (auto &[pos, cc]: chunks) {
        if (cc.dirty || cc.chunk->DamagedCount() == 0) continue;
        if (MicroLodFor(pos, cameraChunk) != cc.meshLod) {
            cc.dirty = true;
            dirtyList.push_back(pos);
        }
    }
}

void vrtc::ClientWorld::BuildMeshInput(ChunkPos pos, const ClientChunk &cc, const MeshTables &tables, int lod,
                                       MeshInput &input) const {
    input.pos = pos;
    input.version = cc.version;
    input.microStamp = cc.microStamp;
    input.lod = static_cast<uint8_t>(lod);
    input.blocks.resize(MESH_PAD_VOLUME);
    input.whole.assign(MESH_PAD_VOLUME, 1);
    input.micro.clear();
    input.masks.clear();
    BlockId *out = input.blocks.data();
    const bool useCache = lod == 0 && !cc.microCache.Empty();

    // Sky cover for lighting: which padded columns have an opaque block somewhere above this snapshot.
    input.coveredAbove.fill(0);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int cy = pos.y + 1; cy < WORLD_MAX_CHUNK_Y; ++cy) {
                const auto it = chunks.find(ChunkPos{pos.x + dx, cy, pos.z + dz});
                if (it == chunks.end()) continue;
                const std::array<uint32_t, CHUNK_SIZE> &cover = CoverOf(it->second, tables);
                // Map the neighbour column onto the padded grid: one border column on each side.
                for (int z = 0; z < CHUNK_SIZE; ++z) {
                    const int pz = dz < 0 ? (z == CHUNK_SIZE - 1 ? 0 : -1) : dz > 0 ? (z == 0 ? CHUNK_SIZE + 1 : -1) : z + 1;
                    if (pz < 0) continue;
                    uint64_t bits;
                    if (dx < 0) bits = (cover[z] >> (CHUNK_SIZE - 1)) & 1ull;
                    else if (dx > 0) bits = static_cast<uint64_t>(cover[z] & 1u) << (CHUNK_SIZE + 1);
                    else bits = static_cast<uint64_t>(cover[z]) << 1;
                    input.coveredAbove[pz] |= bits;
                }
            }
        }
    }

    // For an offset of -1, 0 or +1 along one axis: where the copy lands in the padded array, where it starts in
    // the source chunk and how many blocks it covers.
    struct Range {
        int padStart, localStart, count;
    };
    const auto range = [](int d) -> Range {
        if (d < 0) return {0, CHUNK_SIZE - 1, 1};
        if (d > 0) return {CHUNK_SIZE + 1, 0, 1};
        return {1, 0, CHUNK_SIZE};
    };

    for (int dy = -1; dy <= 1; ++dy) {
        const Range ry = range(dy);
        for (int dz = -1; dz <= 1; ++dz) {
            const Range rz = range(dz);
            for (int dx = -1; dx <= 1; ++dx) {
                const Range rx = range(dx);
                const bool centre = dx == 0 && dy == 0 && dz == 0;
                const Chunk *source = cc.chunk.get();
                if (!centre) {
                    const auto it = chunks.find(Offset(pos, dx, dy, dz));
                    source = it == chunks.end() ? nullptr : it->second.chunk.get();
                }

                if (!source || source->IsUniform()) {
                    BlockId fill = BLOCK_AIR;
                    if (source) fill = source->UniformId();
                    else if (pos.y + dy < WORLD_MIN_CHUNK_Y) fill = belowWorldFill;
                    for (int py = 0; py < ry.count; ++py) {
                        for (int pz = 0; pz < rz.count; ++pz) {
                            BlockId *row = out + PadIndex(rx.padStart, ry.padStart + py, rz.padStart + pz);
                            for (int px = 0; px < rx.count; ++px) row[px] = fill;
                        }
                    }
                    continue;
                }

                const BlockId *data = source->Data();
                for (int py = 0; py < ry.count; ++py) {
                    for (int pz = 0; pz < rz.count; ++pz) {
                        std::memcpy(out + PadIndex(rx.padStart, ry.padStart + py, rz.padStart + pz),
                                    data + BlockIndex(rx.localStart, ry.localStart + py, rz.localStart + pz),
                                    static_cast<size_t>(rx.count) * sizeof(BlockId));
                    }
                }

                // Partial blocks (edited masks and generated slopes) are not opaque-whole; the centre chunk's
                // also go to the worker. `mostlySolid` decides the far-LOD stand-in.
                const auto partial = [&](uint16_t index, bool mostlySolid) -> MicroEntry * {
                    const int lx = index & CHUNK_MASK, lz = (index >> CHUNK_BITS) & CHUNK_MASK, ly = index >> (2 * CHUNK_BITS);
                    if (lx < rx.localStart || lx >= rx.localStart + rx.count) return nullptr;
                    if (ly < ry.localStart || ly >= ry.localStart + ry.count) return nullptr;
                    if (lz < rz.localStart || lz >= rz.localStart + rz.count) return nullptr;
                    const int padIndex = PadIndex(rx.padStart + lx - rx.localStart, ry.padStart + ly - ry.localStart,
                                                  rz.padStart + lz - rz.localStart);
                    if (lod >= 3) {
                        // Far away: a partial block draws as a whole block if at least half of it remains,
                        // otherwise not at all, and joins the greedy whole-block pass.
                        if (!mostlySolid) out[padIndex] = BLOCK_AIR;
                        return nullptr;
                    }
                    input.whole[padIndex] = 0;
                    if (!centre) return nullptr;
                    MicroEntry &entry = input.micro.emplace_back();
                    entry.index = index;
                    if (useCache) {
                        if (const MicroQuads *cached = cc.microCache.Find(index); cached && cached->lod == lod) {
                            entry.cached = cached->mesh;
                            entry.cachedBlocked = cached->blocked;
                        }
                    }
                    return &entry;
                };

                const std::span<const uint16_t> owners = source->MicroOwners();
                for (size_t slot = 0; slot < owners.size(); ++slot) {
                    const MicroBlock &mask = source->MicroAt(slot);
                    const bool mostlySolid = lod >= 3 && mask.Count() * 2 >= MicroBlock::BITS;
                    if (MicroEntry *entry = partial(owners[slot], mostlySolid)) {
                        entry->maskIndex = static_cast<int32_t>(input.masks.size());
                        input.masks.push_back(mask);
                    }
                }
                for (const SlopeBlock &slope: source->Slopes()) {
                    if (MicroEntry *entry = partial(slope.index, SlopePositiveCorners(slope.corner) >= 4)) {
                        std::memcpy(entry->corner, slope.corner, sizeof slope.corner);
                    }
                }
            }
        }
    }
}

void vrtc::ClientWorld::StoreMicroCache(const MeshOutput &output) {
    ClientChunk *cc = Get(output.pos);
    if (!cc || cc->microStamp != output.microStamp) return; // masks changed while the job ran
    for (const auto &[index, quads]: output.builtMicro) cc->microCache.Set(index, quads);
}

void vrtc::ClientWorld::MarkDirty(ChunkPos pos) {
    ClientChunk *cc = Get(pos);
    if (!cc || cc->dirty) return;
    cc->dirty = true;
    dirtyList.push_back(pos);
}

vrtc::BlockId vrtc::ClientWorld::Block(BlockPos pos) const {
    const ClientChunk *cc = Get(ToChunkPos(pos));
    if (!cc) return BLOCK_AIR;
    return cc->chunk->Get(LocalCoord(pos.x), LocalCoord(pos.y), LocalCoord(pos.z));
}

const vrtc::MicroBlock *vrtc::ClientWorld::Micro(BlockPos pos) const {
    const ClientChunk *cc = Get(ToChunkPos(pos));
    if (!cc) return nullptr;
    // Expanded into a per-thread scratch mask: valid until the next call, which is all the raycast needs.
    static thread_local MicroBlock scratch;
    const uint16_t index = BlockIndex(LocalCoord(pos.x), LocalCoord(pos.y), LocalCoord(pos.z));
    return cc->chunk->GetMask(index, scratch) ? &scratch : nullptr;
}

const std::array<uint32_t, vrtc::CHUNK_SIZE> &vrtc::ClientWorld::CoverOf(const ClientChunk &cc,
                                                                         const MeshTables &tables) const {
    if (cc.coverValid) return cc.cover;
    const Chunk &chunk = *cc.chunk;
    if (chunk.IsUniform()) {
        cc.cover.fill(tables.opaque[chunk.UniformId()] ? ~0u : 0u);
    } else {
        cc.cover.fill(0);
        const BlockId *data = chunk.Data();
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                const BlockId *row = data + BlockIndex(0, y, z);
                uint32_t bits = 0;
                for (int x = 0; x < CHUNK_SIZE; ++x) bits |= static_cast<uint32_t>(tables.opaque[row[x]]) << x;
                cc.cover[z] |= bits;
            }
        }
        // Partial blocks let light through, so they are not cover.
        const auto clear = [&](uint16_t index) {
            // Only clears the column if nothing else in it is opaque; cheap enough to recheck that column.
            const int x = index & CHUNK_MASK, z = (index >> CHUNK_BITS) & CHUNK_MASK;
            bool covered = false;
            for (int y = 0; y < CHUNK_SIZE && !covered; ++y) {
                const uint16_t i = BlockIndex(x, y, z);
                covered = tables.opaque[data[i]] && !chunk.IsDamaged(i);
            }
            if (!covered) cc.cover[z] &= ~(1u << x);
        };
        for (const uint16_t index: chunk.MicroOwners()) clear(index);
        for (const SlopeBlock &slope: chunk.Slopes()) clear(slope.index);
    }
    cc.coverValid = true;
    return cc.cover;
}
