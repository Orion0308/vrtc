#pragma once
// Fly camera (M1). Position is a double in world metres; rendering is relative to the camera's chunk origin
// (section 5), so the view matrix is built from a small float offset and never loses precision far from 0.
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "Coords.h"
#include "Input.h"

namespace vrtc {
    class Camera {
        glm::dvec3 position{SPAWN_X, SPAWN_Y, SPAWN_Z};
        float yaw = 0.f; // degrees; 0 looks north (-Z), positive turns toward +X
        float pitch = 0.f; // degrees, clamped to +-89
        double speedScale = 1.0; // scroll wheel multiplier

    public:
        // Mouse look always; WASD flight only when `flying`. When walking, the server moves the player
        // and Engine sets the position.
        void Update(const Input &input, double dt, bool flying = true);

        [[nodiscard]] glm::dvec3 Position() const { return position; }

        void SetPosition(glm::dvec3 p) { position = p; }

        void SetLook(float yawDegrees, float pitchDegrees) {
            yaw = yawDegrees;
            pitch = pitchDegrees;
        }

        [[nodiscard]] float Yaw() const { return yaw; }

        [[nodiscard]] float Pitch() const { return pitch; }

        [[nodiscard]] glm::vec3 Forward() const;

        [[nodiscard]] ChunkPos CameraChunk() const { return ToChunkPos(ToBlockPos(position)); }

        // Eye position relative to the origin of CameraChunk().
        [[nodiscard]] glm::vec3 RelativeEye() const;

        [[nodiscard]] glm::mat4 View() const;

        [[nodiscard]] glm::mat4 Projection(float aspect) const;

        [[nodiscard]] double Speed() const;
    };
}
