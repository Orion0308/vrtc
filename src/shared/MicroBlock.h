#pragma once
// The micro-voxel mask of a damaged block (section 6): 16x16x16 solid bits, 512 bytes.
// One 16-bit row per (y, z); bit x within the row. Kept uncompressed in memory so collision can read it directly.
#include <bit>
#include <cstdint>
#include <cstring>

#include "Config.h"

namespace vrtc {
    struct MicroBlock {
        uint16_t rows[MICRO_RES][MICRO_RES]; // rows[y][z], bit x

        static MicroBlock Full() {
            MicroBlock m;
            std::memset(m.rows, 0xFF, sizeof m.rows);
            return m;
        }

        static MicroBlock Empty() {
            MicroBlock m;
            std::memset(m.rows, 0, sizeof m.rows);
            return m;
        }

        [[nodiscard]] bool Get(int x, int y, int z) const { return (rows[y][z] >> x) & 1; }

        void Set(int x, int y, int z, bool solid) {
            if (solid) rows[y][z] |= static_cast<uint16_t>(1u << x);
            else rows[y][z] &= static_cast<uint16_t>(~(1u << x));
        }

        [[nodiscard]] uint32_t Count() const {
            uint32_t n = 0;
            for (const auto &plane: rows) {
                for (const uint16_t row: plane) n += static_cast<uint32_t>(std::popcount(row));
            }
            return n;
        }

        [[nodiscard]] bool IsFull() const {
            for (const auto &plane: rows) {
                for (const uint16_t row: plane) {
                    if (row != 0xFFFF) return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool IsEmpty() const {
            for (const auto &plane: rows) {
                for (const uint16_t row: plane) {
                    if (row != 0) return false;
                }
            }
            return true;
        }

        [[nodiscard]] const uint16_t *Words() const { return &rows[0][0]; }

        [[nodiscard]] uint16_t *Words() { return &rows[0][0]; }

        static constexpr size_t WORDS = MICRO_RES * MICRO_RES;
        static constexpr uint32_t BITS = MICRO_RES * MICRO_RES * MICRO_RES; // 4096
    };

    static_assert(sizeof(MicroBlock) == 512);
}

namespace vrtc {
    // A block the generated surface cuts through, stored as its eight corner density values instead of a
    // 512-byte mask (18 bytes). The mask is a pure function of the corners and is expanded on demand for
    // meshing, ray casts and edits; an edit turns the block into an ordinary MicroBlock.
    // Corner index: bit 0 = x, bit 1 = z, bit 2 = y. Values are density * 256; positive is solid.
    struct SlopeBlock {
        uint16_t index; // block index inside the chunk
        int16_t corner[8];
    };

    inline int16_t QuantiseSlope(float density) {
        const float scaled = density * 256.f;
        return static_cast<int16_t>(scaled > 32767.f ? 32767.f : scaled < -32767.f ? -32767.f : scaled);
    }

    // Number of positive corners, 0 to 8.
    inline int SlopePositiveCorners(const int16_t c[8]) {
        int n = 0;
        for (int k = 0; k < 8; ++k) n += c[k] > 0;
        return n;
    }

    // Fills `mask`: a micro-voxel is solid when the trilinear interpolation of the corners is positive at its
    // centre. Returns the number of solid micro-voxels. Server and client run this same code.
    inline uint32_t ExpandSlope(const int16_t c[8], MicroBlock &mask) {
        uint32_t count = 0;
        constexpr float STEP = 1.f / MICRO_RES;
        const float f[8] = {float(c[0]), float(c[1]), float(c[2]), float(c[3]), float(c[4]), float(c[5]), float(c[6]), float(c[7])};
        for (int my = 0; my < MICRO_RES; ++my) {
            const float fy = (static_cast<float>(my) + 0.5f) * STEP;
            // Along y first: four values on the (x, z) corners of this horizontal slice.
            const float y00 = f[0] + (f[4] - f[0]) * fy, y01 = f[1] + (f[5] - f[1]) * fy;
            const float y10 = f[2] + (f[6] - f[2]) * fy, y11 = f[3] + (f[7] - f[3]) * fy;
            for (int mz = 0; mz < MICRO_RES; ++mz) {
                const float fz = (static_cast<float>(mz) + 0.5f) * STEP;
                // Along x the value is linear: start at the first micro centre and step.
                const float x0 = y00 + (y10 - y00) * fz, x1 = y01 + (y11 - y01) * fz;
                const float slope = (x1 - x0) * STEP;
                float value = x0 + slope * 0.5f;
                uint32_t bits = 0;
                for (int mx = 0; mx < MICRO_RES; ++mx, value += slope) bits |= static_cast<uint32_t>(value > 0.f) << mx;
                mask.rows[my][mz] = static_cast<uint16_t>(bits);
                count += static_cast<uint32_t>(std::popcount(bits));
            }
        }
        return count;
    }
}
