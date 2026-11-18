#pragma once
// Exact box-against-world overlap (section 13): a box collides with whole solid blocks and with the individual
// micro-voxels of partial blocks. Touching is not overlapping, so a body fits any gap it is no wider than.
// `Query` provides: BlockId Block(BlockPos); bool Solid(BlockId); const MicroBlock *Mask(BlockPos) (nullptr when
// the block is whole). It is a template so no virtual call sits in the per-block loop (hard rule 7).
#include <algorithm>
#include <cmath>
#include <glm/vec3.hpp>

#include "Coords.h"
#include "MicroBlock.h"

namespace vrtc {
    template<class Query>
    bool BoxOverlapsSolid(Query &query, glm::dvec3 min, glm::dvec3 max) {
        constexpr double EPS = 1e-7; // shrink so that faces in contact do not count
        min += glm::dvec3(EPS);
        max -= glm::dvec3(EPS);
        const BlockPos lo = ToBlockPos(min), hi = ToBlockPos(max);
        for (int y = lo.y; y <= hi.y; ++y) {
            for (int z = lo.z; z <= hi.z; ++z) {
                for (int x = lo.x; x <= hi.x; ++x) {
                    const BlockPos pos{x, y, z};
                    if (!query.Solid(query.Block(pos))) continue;
                    const MicroBlock *mask = query.Mask(pos);
                    if (!mask) return true; // whole block
                    // Partial block: test the micro-voxel range the box covers, a row of bits at a time.
                    const auto range = [](double boxMin, double boxMax, int block, int &first, int &last) {
                        first = std::clamp(static_cast<int>(std::floor((boxMin - block) * MICRO_RES)), 0, MICRO_RES - 1);
                        last = std::clamp(static_cast<int>(std::floor((boxMax - block) * MICRO_RES)), 0, MICRO_RES - 1);
                    };
                    int x0, x1, y0, y1, z0, z1;
                    range(min.x, max.x, x, x0, x1);
                    range(min.y, max.y, y, y0, y1);
                    range(min.z, max.z, z, z0, z1);
                    const auto bits = static_cast<uint16_t>(((1u << (x1 - x0 + 1)) - 1u) << x0);
                    for (int my = y0; my <= y1; ++my) {
                        for (int mz = z0; mz <= z1; ++mz) {
                            if (mask->rows[my][mz] & bits) return true;
                        }
                    }
                }
            }
        }
        return false;
    }
}
