#pragma once
// The zero-width ray query (section 13): a 3D DDA over the block grid that, on reaching a damaged block, runs a
// second DDA over that block's 16^3 grid and continues if it passes through a gap.
// `Query` provides: BlockId Block(BlockPos) const; const MicroBlock *Micro(BlockPos) const; bool Solid(BlockId) const.
// It is a template rather than an interface so no virtual call sits in the per-cell loop (hard rule 7).
#include <cmath>
#include <limits>
#include <glm/vec3.hpp>

#include "Coords.h"
#include "Faces.h"
#include "MicroBlock.h"

namespace vrtc {
    struct RayHit {
        bool hit = false;
        BlockPos block{};
        BlockId id = BLOCK_AIR;
        int face = -1; // face of the block that was hit; its normal points back toward the ray. -1 if the ray started inside.
        bool damaged = false; // true when the hit is on a micro-voxel of a damaged block
        IVec3 micro{}; // micro cell hit (valid when damaged)
        double distance = 0;
        glm::dvec3 point{};
    };

    namespace detail {
        // The face a cell is entered through when stepping along `axis` in direction `step`.
        constexpr int EnteredFace(int axis, int step) {
            if (axis == 0) return step > 0 ? FACE_NX : FACE_PX;
            if (axis == 1) return step > 0 ? FACE_NY : FACE_PY;
            return step > 0 ? FACE_NZ : FACE_PZ;
        }

        struct Dda {
            int cell[3];
            int step[3];
            double tMax[3];
            double tDelta[3];

            // Cell grid of size `cellSize` with its origin at `gridOrigin`; `start` is the ray position at tStart.
            void Init(glm::dvec3 origin, glm::dvec3 dir, double tStart, glm::dvec3 gridOrigin, double cellSize,
                      glm::dvec3 start) {
                for (int a = 0; a < 3; ++a) {
                    const double local = (start[a] - gridOrigin[a]) / cellSize;
                    cell[a] = static_cast<int>(std::floor(local));
                    if (dir[a] > 0) {
                        step[a] = 1;
                        tDelta[a] = cellSize / dir[a];
                        tMax[a] = (gridOrigin[a] + (cell[a] + 1) * cellSize - origin[a]) / dir[a];
                    } else if (dir[a] < 0) {
                        step[a] = -1;
                        tDelta[a] = -cellSize / dir[a];
                        tMax[a] = (gridOrigin[a] + cell[a] * cellSize - origin[a]) / dir[a];
                    } else {
                        step[a] = 0;
                        tDelta[a] = std::numeric_limits<double>::infinity();
                        tMax[a] = std::numeric_limits<double>::infinity();
                    }
                    if (tMax[a] < tStart) tMax[a] = tStart; // guard against rounding when starting on a boundary
                }
            }

            // Steps to the next cell; returns the axis stepped and sets t to the entry parameter.
            int Step(double &t) {
                int axis = 0;
                if (tMax[1] < tMax[axis]) axis = 1;
                if (tMax[2] < tMax[axis]) axis = 2;
                t = tMax[axis];
                cell[axis] += step[axis];
                tMax[axis] += tDelta[axis];
                return axis;
            }
        };
    }

    template<class Query>
    RayHit Raycast(const Query &query, glm::dvec3 origin, glm::dvec3 dir, double maxDistance) {
        RayHit result;
        const double length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (length <= 0 || maxDistance <= 0) return result;
        dir /= length;

        detail::Dda blocks;
        blocks.Init(origin, dir, 0.0, glm::dvec3(0), 1.0, origin);
        double t = 0;
        int face = -1;

        for (int guard = 0; guard < 4096; ++guard) {
            const BlockPos pos{blocks.cell[0], blocks.cell[1], blocks.cell[2]};
            const BlockId id = query.Block(pos);
            if (query.Solid(id)) {
                const MicroBlock *mask = query.Micro(pos);
                if (!mask) {
                    result.hit = true;
                    result.block = pos;
                    result.id = id;
                    result.face = face;
                    result.distance = t;
                    result.point = origin + dir * t;
                    return result;
                }
                // Damaged block: walk its micro grid from the entry point.
                const glm::dvec3 blockOrigin(pos.x, pos.y, pos.z);
                const double cellSize = 1.0 / MICRO_RES;
                glm::dvec3 start = origin + dir * t;
                // Keep the start strictly inside the block so floor() lands in a valid cell.
                for (int a = 0; a < 3; ++a) {
                    start[a] = std::min(std::max(start[a], blockOrigin[a]), blockOrigin[a] + 1.0 - 1e-9);
                }
                detail::Dda cells;
                cells.Init(origin, dir, t, blockOrigin, cellSize, start);
                double tm = t;
                int microFace = face;
                while (cells.cell[0] >= 0 && cells.cell[0] < MICRO_RES && cells.cell[1] >= 0 && cells.cell[1] < MICRO_RES
                       && cells.cell[2] >= 0 && cells.cell[2] < MICRO_RES) {
                    if (mask->Get(cells.cell[0], cells.cell[1], cells.cell[2])) {
                        result.hit = true;
                        result.block = pos;
                        result.id = id;
                        result.face = microFace;
                        result.damaged = true;
                        result.micro = {cells.cell[0], cells.cell[1], cells.cell[2]};
                        result.distance = tm;
                        result.point = origin + dir * tm;
                        return result;
                    }
                    const int axis = cells.Step(tm);
                    microFace = detail::EnteredFace(axis, cells.step[axis]);
                    if (tm > maxDistance) return result;
                }
            }
            const int axis = blocks.Step(t);
            if (t > maxDistance) return result;
            face = detail::EnteredFace(axis, blocks.step[axis]);
        }
        return result;
    }
}
