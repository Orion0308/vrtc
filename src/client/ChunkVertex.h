#pragma once
// The terrain GPU format.
//
// ChunkQuad: one greedy quad per instance, 12 bytes (was four 8-byte vertices, 32 bytes). The vertex shader
// expands it to four corners from gl_VertexID. With millions of micro-voxel faces this is the dominant buffer.
//   a: x:10 | y:10 | z:10           min corner on the face's plane, 1/16 block steps, chunk-local (0..512)
//   b: w:10 | h:10 | face:3 | ao:8  extent along the face's u and v axes; 2 bits of AO per corner
//   c: layer:12 | sky:4 | blockLight:4
// u/v axes per face normal axis: X -> (z, y), Y -> (x, z), Z -> (x, y). Corner order is Faces.h FACE_CORNERS.
//
#include <cstdint>

namespace vrtc {
    struct ChunkQuad {
        uint32_t a, b, c;
    };


    inline constexpr uint32_t MAX_TEXTURE_LAYERS = 4096; // 12 bits of layer

    inline ChunkQuad PackChunkQuad(uint32_t x, uint32_t y, uint32_t z, uint32_t w, uint32_t h, uint32_t face,
                                   const uint32_t ao[4], uint32_t layer, uint32_t sky, uint32_t blockLight) {
        const uint32_t aoBits = (ao[0] & 3) | ((ao[1] & 3) << 2) | ((ao[2] & 3) << 4) | ((ao[3] & 3) << 6);
        return {
            (x & 0x3FF) | ((y & 0x3FF) << 10) | ((z & 0x3FF) << 20),
            (w & 0x3FF) | ((h & 0x3FF) << 10) | ((face & 0x7) << 20) | (aoBits << 23),
            (layer & 0xFFF) | ((sky & 0xF) << 12) | ((blockLight & 0xF) << 16),
        };
    }
}
