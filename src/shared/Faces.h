#pragma once
// The fixed face order used everywhere (section 5): index 0 to 5 is +Z, -Z, +X, -X, +Y, -Y.
// Carried over from the old data/VoxelData.h; its winding order becomes the standard.
#include <array>

namespace vrtc {
    inline constexpr int FACE_PZ = 0, FACE_NZ = 1, FACE_PX = 2, FACE_NX = 3, FACE_PY = 4, FACE_NY = 5;
    inline constexpr int FACE_COUNT = 6;

    struct IVec3 {
        int x, y, z;
    };

    inline constexpr std::array<IVec3, FACE_COUNT> FACE_NORMAL = {{
        {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0},
    }};

    inline constexpr std::array<int, FACE_COUNT> FACE_OPPOSITE = {1, 0, 3, 2, 5, 4};

    // The four corners of each face as unit-cube offsets, counter-clockwise seen from outside.
    // A quad is drawn as triangles (0,1,2) and (0,2,3).
    inline constexpr std::array<std::array<IVec3, 4>, FACE_COUNT> FACE_CORNERS = {{
        {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}, // +Z
        {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}}, // -Z
        {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}, // +X
        {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}, // -X
        {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}}, // +Y
        {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}}, // -Y
    }};
}
