#pragma once
// View frustum planes extracted from a view-projection matrix, for culling chunk bounding boxes.
#include <array>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace vrtc {
    class Frustum {
        std::array<glm::vec4, 6> planes{}; // xyz normal pointing inward, w offset

    public:
        Frustum() = default;

        explicit Frustum(const glm::mat4 &viewProj) {
            // Gribb and Hartmann: rows of the matrix (glm is column-major, so row i is m[c][i]).
            const auto row = [&](int i) {
                return glm::vec4(viewProj[0][i], viewProj[1][i], viewProj[2][i], viewProj[3][i]);
            };
            const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
            planes = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
        }

        // True when the box may be visible (conservative).
        [[nodiscard]] bool IntersectsBox(glm::vec3 min, glm::vec3 max) const {
            for (const glm::vec4 &p: planes) {
                // The vertex farthest along the plane normal: if even that is behind, the box is outside.
                const glm::vec3 v(p.x >= 0 ? max.x : min.x, p.y >= 0 ? max.y : min.y, p.z >= 0 ? max.z : min.z);
                if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0) return false;
            }
            return true;
        }
    };
}
