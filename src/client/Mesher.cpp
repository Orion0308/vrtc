#include "Mesher.h"

#include <array>
#include <bit>
#include <chrono>
#include <cstring>

#include "Faces.h"

namespace {
    using namespace vrtc;

    struct CornerOffsets {
        int side1, side2, corner; // offsets from the block's pad index to the three AO neighbours
    };

    struct FaceInfo {
        int normalOffset; // offset to the neighbour the face looks at
        std::array<CornerOffsets, 4> corners;
    };

    constexpr int Stride(int axis) { return axis == 0 ? 1 : axis == 1 ? MESH_PAD_AREA : MESH_PAD; }

    constexpr std::array<FaceInfo, FACE_COUNT> BuildFaces() {
        std::array<FaceInfo, FACE_COUNT> faces{};
        for (int f = 0; f < FACE_COUNT; ++f) {
            const IVec3 n = FACE_NORMAL[f];
            const int normalOffset = n.x * Stride(0) + n.y * Stride(1) + n.z * Stride(2);
            const int normalAxis = n.x != 0 ? 0 : n.y != 0 ? 1 : 2;
            const int a1 = (normalAxis + 1) % 3, a2 = (normalAxis + 2) % 3;
            faces[f].normalOffset = normalOffset;
            for (int k = 0; k < 4; ++k) {
                const IVec3 c = FACE_CORNERS[f][k];
                const int component[3] = {c.x, c.y, c.z};
                // A corner at 1 along a tangent axis looks toward +1 on that axis, at 0 toward -1.
                const int o1 = (component[a1] ? 1 : -1) * Stride(a1);
                const int o2 = (component[a2] ? 1 : -1) * Stride(a2);
                faces[f].corners[k] = {normalOffset + o1, normalOffset + o2, normalOffset + o1 + o2};
            }
        }
        return faces;
    }

    constexpr std::array<FaceInfo, FACE_COUNT> FACES = BuildFaces();
    constexpr int FACE_AXIS[FACE_COUNT] = {2, 2, 0, 0, 1, 1}; // normal axis of each face
    constexpr bool FACE_POSITIVE[FACE_COUNT] = {true, false, true, false, true, false};
    // Tangent axes of a slice for each normal axis: u runs along rows, v across them. Must match chunk.vert.
    constexpr int AXIS_U[3] = {2, 0, 0};
    constexpr int AXIS_V[3] = {1, 2, 1};
    constexpr uint32_t NO_AO[4] = {3, 3, 3, 3};

    // ---------------------------------------------------------------- damaged blocks

    // Binary greedy rectangles over `res` rows of visibility bits: a run of set bits grows downward while the
    // rows below contain the same run. emit(u0, v0, width, height).
    template<class Emit>
    void BinaryGreedy(uint16_t (&rows)[MICRO_RES], int res, Emit &&emit) {
        for (int v = 0; v < res; ++v) {
            uint32_t bits = rows[v];
            while (bits) {
                const int lo = std::countr_zero(bits);
                const int len = std::countr_one(bits >> lo);
                const uint32_t run = ((1u << len) - 1u) << lo;
                int h = 1;
                while (v + h < res && (rows[v + h] & run) == run) {
                    rows[v + h] &= static_cast<uint16_t>(~run);
                    ++h;
                }
                bits &= ~run;
                emit(lo, v, len, h);
            }
        }
    }

    // Reduces the 16^3 mask to (16 >> lod)^3: a coarse cell is solid when at least half its micro-voxels are.
    void Downsample(const MicroBlock &m, int lod, uint16_t (&out)[MICRO_RES][MICRO_RES]) {
        if (lod == 0) {
            std::memcpy(out, m.rows, sizeof m.rows);
            return;
        }
        const int s = 1 << lod, res = MICRO_RES >> lod;
        const uint32_t cellMask = (1u << s) - 1u;
        const int half = (s * s * s) / 2;
        for (int cy = 0; cy < res; ++cy) {
            for (int cz = 0; cz < res; ++cz) {
                uint32_t bits = 0;
                for (int cx = 0; cx < res; ++cx) {
                    int count = 0;
                    for (int dy = 0; dy < s; ++dy) {
                        for (int dz = 0; dz < s; ++dz) {
                            count += std::popcount((static_cast<uint32_t>(m.rows[cy * s + dy][cz * s + dz]) >> (cx * s)) & cellMask);
                        }
                    }
                    if (count >= half) bits |= 1u << cx;
                }
                out[cy][cz] = static_cast<uint16_t>(bits);
            }
        }
    }

    // Greedy quads of one damaged block at the given LOD, grouped by face, in 1/16 steps from the chunk origin.
    MicroMesh BuildMicroMesh(uint16_t index, const MicroBlock &mask, uint8_t blocked, BlockId id, const uint16_t *layers,
                             int lod, uint32_t sky) {
        MicroMesh mesh;
        const int res = MICRO_RES >> lod, scale = 1 << lod;
        const uint32_t all = (1u << res) - 1u;
        const int bx = index & CHUNK_MASK, bz = (index >> CHUNK_BITS) & CHUNK_MASK, by = index >> (2 * CHUNK_BITS);
        const int base[3] = {bx * VERTEX_STEPS, by * VERTEX_STEPS, bz * VERTEX_STEPS};
        const auto isBlocked = [blocked](int f) { return (blocked >> f) & 1; };

        static thread_local uint16_t cells[MICRO_RES][MICRO_RES]; // [y][z], bits over x
        static thread_local uint16_t vis[FACE_COUNT][MICRO_RES][MICRO_RES];
        Downsample(mask, lod, cells);

        // Visible faces per row: inside the block a face is hidden by the adjacent cell; on the block boundary
        // only by an opaque-whole neighbour block.
        for (int y = 0; y < res; ++y) {
            for (int z = 0; z < res; ++z) {
                const uint32_t row = cells[y][z];
                uint32_t px = row & ~(row >> 1), nx = row & ~((row << 1) & all);
                if (isBlocked(FACE_PX)) px &= ~(1u << (res - 1));
                if (isBlocked(FACE_NX)) nx &= ~1u;
                const uint32_t pz = z + 1 < res ? cells[y][z + 1] : (isBlocked(FACE_PZ) ? all : 0);
                const uint32_t nz = z > 0 ? cells[y][z - 1] : (isBlocked(FACE_NZ) ? all : 0);
                const uint32_t py = y + 1 < res ? cells[y + 1][z] : (isBlocked(FACE_PY) ? all : 0);
                const uint32_t ny = y > 0 ? cells[y - 1][z] : (isBlocked(FACE_NY) ? all : 0);
                vis[FACE_PX][y][z] = static_cast<uint16_t>(px);
                vis[FACE_NX][y][z] = static_cast<uint16_t>(nx);
                vis[FACE_PZ][y][z] = static_cast<uint16_t>(row & ~pz);
                vis[FACE_NZ][y][z] = static_cast<uint16_t>(row & ~nz);
                vis[FACE_PY][y][z] = static_cast<uint16_t>(row & ~py);
                vis[FACE_NY][y][z] = static_cast<uint16_t>(row & ~ny);
            }
        }

        uint16_t rows[MICRO_RES];
        for (int f = 0; f < FACE_COUNT; ++f) {
            mesh.faceStart[f] = static_cast<uint32_t>(mesh.quads.size());
            const uint32_t layer = layers[id * 6 + f];
            const int axis = FACE_AXIS[f];
            const int plane = FACE_POSITIVE[f] ? 1 : 0;
            for (int d = 0; d < res; ++d) {
                // Gather the slice at depth d as rows over the face's u axis.
                bool any = false;
                if (axis == 1) { // +-Y: rows indexed by z, bits over x
                    for (int v = 0; v < res; ++v) any |= (rows[v] = vis[f][d][v]) != 0;
                } else if (axis == 2) { // +-Z: rows indexed by y, bits over x
                    for (int v = 0; v < res; ++v) any |= (rows[v] = vis[f][v][d]) != 0;
                } else { // +-X: rows indexed by y, bits over z
                    for (int v = 0; v < res; ++v) {
                        uint32_t bits = 0;
                        for (int z = 0; z < res; ++z) bits |= ((vis[f][v][z] >> d) & 1u) << z;
                        any |= (rows[v] = static_cast<uint16_t>(bits)) != 0;
                    }
                }
                if (!any) continue;
                BinaryGreedy(rows, res, [&](int u0, int v0, int w, int h) {
                    int min[3];
                    if (axis == 1) { // u = x, v = z
                        min[0] = u0; min[1] = d + plane; min[2] = v0;
                    } else if (axis == 2) { // u = x, v = y
                        min[0] = u0; min[1] = v0; min[2] = d + plane;
                    } else { // u = z, v = y
                        min[0] = d + plane; min[1] = v0; min[2] = u0;
                    }
                    mesh.quads.push_back(PackChunkQuad(
                        static_cast<uint32_t>(base[0] + min[0] * scale), static_cast<uint32_t>(base[1] + min[1] * scale),
                        static_cast<uint32_t>(base[2] + min[2] * scale), static_cast<uint32_t>(w * scale),
                        static_cast<uint32_t>(h * scale), static_cast<uint32_t>(f), NO_AO, layer, sky, 0));
                });
            }
        }
        mesh.faceStart[FACE_COUNT] = static_cast<uint32_t>(mesh.quads.size());
        return mesh;
    }

    // ---------------------------------------------------------------- blocky chunk

    void MeshBlocky(const MeshInput &input, const MeshTables &tables, MeshOutput &out) {
        const BlockId *in = input.blocks.data();
        const uint8_t *opaque = tables.opaque.data();
        const uint8_t *visible = tables.visible.data();
        const uint16_t *layers = tables.faceLayer.data();

        static thread_local std::vector<ChunkQuad> byFace[FACE_COUNT];
        for (auto &quads: byFace) quads.clear();

        // Only opaque-whole blocks hide faces: a damaged block in the chunk or its border is see-through.
        static thread_local std::vector<uint8_t> occluder;
        occluder.resize(MESH_PAD_VOLUME);
        for (int i = 0; i < MESH_PAD_VOLUME; ++i) occluder[i] = opaque[in[i]] & input.whole[i];

        // Sky light (spec section 10, simplified): 15 in every cell with open sky above, flooding into covered
        // cells and losing one level per step. Opaque-whole blocks stop it; partial blocks let it through.
        // The flood is confined to this snapshot, so light that would detour through a neighbouring chunk and
        // come back is missed; in practice that only dims a few cells near chunk borders.
        static thread_local std::vector<uint8_t> light;
        static thread_local std::vector<int> frontier, next;
        light.assign(MESH_PAD_VOLUME, 0);
        bool anyDark = false;
        for (int z = 0; z < MESH_PAD; ++z) {
            for (int x = 0; x < MESH_PAD; ++x) {
                bool exposed = !((input.coveredAbove[z] >> x) & 1);
                for (int y = MESH_PAD - 1; y >= 0; --y) {
                    const int index = PadIndex(x, y, z);
                    if (occluder[index]) {
                        exposed = false;
                    } else if (exposed) {
                        light[index] = 15;
                    } else {
                        anyDark = true;
                    }
                }
            }
        }
        if (anyDark) {
            static constexpr int STEP[6] = {1, -1, MESH_PAD, -MESH_PAD, MESH_PAD_AREA, -MESH_PAD_AREA};
            const auto spread = [&](int index, int level, std::vector<int> &out) {
                const int x = index % MESH_PAD, z = (index / MESH_PAD) % MESH_PAD, y = index / MESH_PAD_AREA;
                const bool ok[6] = {x + 1 < MESH_PAD, x > 0, z + 1 < MESH_PAD, z > 0, y + 1 < MESH_PAD, y > 0};
                for (int d = 0; d < 6; ++d) {
                    if (!ok[d]) continue;
                    const int n = index + STEP[d];
                    if (occluder[n] || light[n] >= level) continue;
                    light[n] = static_cast<uint8_t>(level);
                    out.push_back(n);
                }
            };
            frontier.clear();
            for (int i = 0; i < MESH_PAD_VOLUME; ++i) {
                if (light[i] == 15) spread(i, 14, frontier);
            }
            for (int level = 13; level >= 1 && !frontier.empty(); --level) {
                next.clear();
                for (const int index: frontier) {
                    if (light[index] == level + 1) spread(index, level, next);
                }
                frontier.swap(next);
            }
        }

        // Pass 1: whole blocks, binary greedy. Column bitmasks along each axis give every face's visibility with
        // one shift and mask per column, so the work scales with visible faces rather than with chunk volume.
        struct Columns {
            uint64_t occ[MESH_PAD][MESH_PAD]; // opaque-whole, [v][u], bits along the axis
            uint64_t vis[MESH_PAD][MESH_PAD]; // visible and whole
        };
        static thread_local Columns cols[3];
        for (auto &c: cols) {
            std::memset(c.occ, 0, sizeof c.occ);
            std::memset(c.vis, 0, sizeof c.vis);
        }
        for (int y = 0; y < MESH_PAD; ++y) {
            for (int z = 0; z < MESH_PAD; ++z) {
                const int rowIndex = PadIndex(0, y, z);
                for (int x = 0; x < MESH_PAD; ++x) {
                    const int index = rowIndex + x;
                    if (occluder[index]) {
                        cols[0].occ[y][z] |= 1ull << x;
                        cols[1].occ[z][x] |= 1ull << y;
                        cols[2].occ[y][x] |= 1ull << z;
                    }
                    if (visible[in[index]] && input.whole[index]) {
                        cols[0].vis[y][z] |= 1ull << x;
                        cols[1].vis[z][x] |= 1ull << y;
                        cols[2].vis[y][x] |= 1ull << z;
                    }
                }
            }
        }

        constexpr uint64_t INTERIOR = ((1ull << CHUNK_SIZE) - 1ull) << 1; // pad depths 1..32
        static thread_local uint32_t sliceRows[CHUNK_SIZE][CHUNK_SIZE]; // [depth][v], bits over u
        static thread_local uint32_t keyCache[CHUNK_SIZE][CHUNK_SIZE]; // [v][u] of the current slice, 0 = unknown
        for (int f = 0; f < FACE_COUNT; ++f) {
            const FaceInfo &face = FACES[f];
            const int n = FACE_AXIS[f], ua = AXIS_U[n], va = AXIS_V[n];
            const bool positive = FACE_POSITIVE[f];
            const Columns &c = cols[n];

            std::memset(sliceRows, 0, sizeof sliceRows);
            bool anyFace = false;
            for (int v = 1; v <= CHUNK_SIZE; ++v) {
                for (int u = 1; u <= CHUNK_SIZE; ++u) {
                    const uint64_t occ = c.occ[v][u];
                    uint64_t faces = c.vis[v][u] & ~(positive ? occ >> 1 : occ << 1) & INTERIOR;
                    while (faces) {
                        const int depth = std::countr_zero(faces);
                        faces &= faces - 1;
                        sliceRows[depth - 1][v - 1] |= 1u << (u - 1);
                        anyFace = true;
                    }
                }
            }
            if (!anyFace) continue;

            for (int d = 0; d < CHUNK_SIZE; ++d) {
                uint32_t (&rows)[CHUNK_SIZE] = sliceRows[d];
                bool any = false;
                for (int v = 0; v < CHUNK_SIZE && !any; ++v) any = rows[v] != 0;
                if (!any) continue;
                std::memset(keyCache, 0, sizeof keyCache);

                // Layer and the four corner AO values of a visible cell, computed once per cell.
                const auto keyOf = [&](int u, int v) -> uint32_t {
                    uint32_t &cached = keyCache[v][u];
                    if (cached) return cached;
                    int cc[3];
                    cc[n] = d + 1;
                    cc[ua] = u + 1;
                    cc[va] = v + 1;
                    const int index = PadIndex(cc[0], cc[1], cc[2]);
                    uint32_t k = 1u | (static_cast<uint32_t>(layers[in[index] * 6 + f]) << 1);
                    for (int i = 0; i < 4; ++i) {
                        const CornerOffsets &co = face.corners[i];
                        const uint32_t s1 = occluder[index + co.side1];
                        const uint32_t s2 = occluder[index + co.side2];
                        const uint32_t cr = occluder[index + co.corner];
                        const uint32_t ao = (s1 && s2) ? 0 : 3 - (s1 + s2 + cr);
                        k |= ao << (13 + 2 * i);
                    }
                    // Sky light of the air cell the face looks into; faces merge only when it matches too.
                    k |= static_cast<uint32_t>(light[index + face.normalOffset]) << 21;
                    cached = k;
                    return k;
                };

                for (int v = 0; v < CHUNK_SIZE; ++v) {
                    uint32_t bits = rows[v];
                    while (bits) {
                        const int u0 = std::countr_zero(bits);
                        const uint32_t k = keyOf(u0, v);
                        int w = 1;
                        while (u0 + w < CHUNK_SIZE && ((bits >> (u0 + w)) & 1u) && keyOf(u0 + w, v) == k) ++w;
                        const uint32_t run = w == CHUNK_SIZE ? ~0u : (((1u << w) - 1u) << u0);
                        int h = 1;
                        while (v + h < CHUNK_SIZE && (rows[v + h] & run) == run) {
                            bool same = true;
                            for (int i = 0; i < w && same; ++i) same = keyOf(u0 + i, v + h) == k;
                            if (!same) break;
                            rows[v + h] &= ~run;
                            ++h;
                        }
                        bits &= ~run;

                        int min[3];
                        min[n] = d + (positive ? 1 : 0); // the face's plane
                        min[ua] = u0;
                        min[va] = v;
                        const uint32_t ao[4] = {(k >> 13) & 3, (k >> 15) & 3, (k >> 17) & 3, (k >> 19) & 3};
                        byFace[f].push_back(PackChunkQuad(
                            static_cast<uint32_t>(min[0] * VERTEX_STEPS), static_cast<uint32_t>(min[1] * VERTEX_STEPS),
                            static_cast<uint32_t>(min[2] * VERTEX_STEPS), static_cast<uint32_t>(w * VERTEX_STEPS),
                            static_cast<uint32_t>(h * VERTEX_STEPS), static_cast<uint32_t>(f), ao, (k >> 1) & 0xFFF,
                            (k >> 21) & 0xF, 0));
                        ++out.wholeQuads;
                    }
                }
            }
        }

        // Pass 2: damaged blocks, reusing cached quads where mask, boundary and LOD are unchanged.
        for (const MicroEntry &entry: input.micro) {
            const int bx = entry.index & CHUNK_MASK, bz = (entry.index >> CHUNK_BITS) & CHUNK_MASK;
            const int by = entry.index >> (2 * CHUNK_BITS);
            const BlockId id = in[PadIndex(bx + 1, by + 1, bz + 1)];
            if (!visible[id]) continue;
            // Boundary faces are culled only against an opaque-whole neighbour block.
            const int pad = PadIndex(bx + 1, by + 1, bz + 1);
            uint8_t blocked = 0;
            for (int f = 0; f < FACE_COUNT; ++f) blocked |= static_cast<uint8_t>(occluder[pad + FACES[f].normalOffset] << f);

            std::shared_ptr<const MicroMesh> mesh;
            // A partial block is lit by the sky light of its own cell (it does not stop light).
            const uint32_t sky = light[pad];
            const auto key = static_cast<uint16_t>(blocked | (sky << 8));
            if (entry.cached && entry.cachedBlocked == key) {
                mesh = entry.cached;
                ++out.microReused;
            } else {
                // A generated slope is expanded from its corners here, on the worker.
                MicroBlock expanded;
                const MicroBlock *mask = &expanded;
                if (entry.maskIndex >= 0) mask = &input.masks[static_cast<size_t>(entry.maskIndex)];
                else ExpandSlope(entry.corner, expanded);
                mesh = std::make_shared<const MicroMesh>(BuildMicroMesh(entry.index, *mask, blocked, id, layers, input.lod, sky));
                // Only the nearest chunks keep their quads: that is where edits happen, and caching every
                // slope block in view would cost more memory than it saves time.
                if (input.lod == 0) out.builtMicro.emplace_back(entry.index, MicroQuads{key, input.lod, mesh});
                ++out.microRebuilt;
            }
            for (int f = 0; f < FACE_COUNT; ++f) {
                byFace[f].insert(byFace[f].end(), mesh->quads.begin() + mesh->faceStart[f],
                                 mesh->quads.begin() + mesh->faceStart[f + 1]);
            }
            out.microQuads += static_cast<uint32_t>(mesh->quads.size());
        }

        size_t total = 0;
        for (const auto &quads: byFace) total += quads.size();
        out.quads.reserve(total);
        for (int f = 0; f < FACE_COUNT; ++f) {
            out.faceStart[f] = static_cast<uint32_t>(out.quads.size());
            out.quads.insert(out.quads.end(), byFace[f].begin(), byFace[f].end());
        }
        out.faceStart[FACE_COUNT] = static_cast<uint32_t>(out.quads.size());
    }

}

vrtc::MeshOutput vrtc::MeshChunk(const MeshInput &input, const MeshTables &tables) {
    const auto start = std::chrono::steady_clock::now();
    MeshOutput out;
    out.pos = input.pos;
    out.version = input.version;
    out.microStamp = input.microStamp;
    out.lod = input.lod;
    MeshBlocky(input, tables, out);
    out.workerMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return out;
}
