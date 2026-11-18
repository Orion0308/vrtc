#pragma once
// Edit shapes (section 7). Both sides use the same tests so a replayed edit clears the same micro-voxels.
#include <algorithm>
#include <cmath>
#include <variant>
#include <glm/vec3.hpp>

namespace vrtc {
    struct Sphere {
        glm::dvec3 centre;
        double radius;
    };

    struct Box {
        glm::dvec3 min, max;
    };

    using Shape = std::variant<Sphere, Box>;

    struct ShapeBounds {
        glm::dvec3 min, max;
    };

    inline ShapeBounds Bounds(const Shape &shape) {
        return std::visit([]<class S>(const S &s) -> ShapeBounds {
            if constexpr (std::is_same_v<S, Sphere>) {
                return {s.centre - glm::dvec3(s.radius), s.centre + glm::dvec3(s.radius)};
            } else {
                return {s.min, s.max};
            }
        }, shape);
    }

    inline bool Contains(const Shape &shape, glm::dvec3 p) {
        return std::visit([&]<class S>(const S &s) -> bool {
            if constexpr (std::is_same_v<S, Sphere>) {
                const glm::dvec3 d = p - s.centre;
                return d.x * d.x + d.y * d.y + d.z * d.z < s.radius * s.radius;
            } else {
                return p.x >= s.min.x && p.x < s.max.x && p.y >= s.min.y && p.y < s.max.y && p.z >= s.min.z && p.z < s.max.z;
            }
        }, shape);
    }

    // True when the whole axis-aligned box lies inside the shape.
    inline bool ContainsBox(const Shape &shape, glm::dvec3 min, glm::dvec3 max) {
        return std::visit([&]<class S>(const S &s) -> bool {
            if constexpr (std::is_same_v<S, Sphere>) {
                // The farthest corner from the centre decides.
                const double dx = std::max(std::abs(min.x - s.centre.x), std::abs(max.x - s.centre.x));
                const double dy = std::max(std::abs(min.y - s.centre.y), std::abs(max.y - s.centre.y));
                const double dz = std::max(std::abs(min.z - s.centre.z), std::abs(max.z - s.centre.z));
                return dx * dx + dy * dy + dz * dz <= s.radius * s.radius;
            } else {
                return min.x >= s.min.x && max.x <= s.max.x && min.y >= s.min.y && max.y <= s.max.y
                       && min.z >= s.min.z && max.z <= s.max.z;
            }
        }, shape);
    }

    // The 16 micro-voxel centres along x of one (y, z) row: returns the bits whose centre is inside the shape.
    // `rowOrigin` is the centre of micro-voxel 0 of the row; centres are `step` apart along x.
    inline uint16_t RowBitsInside(const Shape &shape, glm::dvec3 rowOrigin, double step) {
        return std::visit([&]<class S>(const S &s) -> uint16_t {
            double lo, hi; // real x range inside the shape at this y, z
            if constexpr (std::is_same_v<S, Sphere>) {
                const double dy = rowOrigin.y - s.centre.y, dz = rowOrigin.z - s.centre.z;
                const double remaining = s.radius * s.radius - dy * dy - dz * dz;
                if (remaining <= 0) return 0;
                const double half = std::sqrt(remaining);
                lo = s.centre.x - half;
                hi = s.centre.x + half;
            } else {
                if (rowOrigin.y < s.min.y || rowOrigin.y >= s.max.y || rowOrigin.z < s.min.z || rowOrigin.z >= s.max.z) return 0;
                lo = s.min.x;
                hi = s.max.x;
            }
            // Centres strictly inside (lo, hi): indices k with lo < rowOrigin.x + k*step < hi.
            const int first = std::max(0, static_cast<int>(std::floor((lo - rowOrigin.x) / step)) + 1);
            const int last = std::min(15, static_cast<int>(std::ceil((hi - rowOrigin.x) / step)) - 1);
            if (first > last) return 0;
            return static_cast<uint16_t>(((1u << (last - first + 1)) - 1u) << first);
        }, shape);
    }
}
