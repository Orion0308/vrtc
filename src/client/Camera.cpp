#include "Camera.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "Config.h"

glm::vec3 vrtc::Camera::Forward() const {
    const float cy = std::cos(glm::radians(yaw)), sy = std::sin(glm::radians(yaw));
    const float cp = std::cos(glm::radians(pitch)), sp = std::sin(glm::radians(pitch));
    return {cp * sy, sp, -cp * cy};
}

double vrtc::Camera::Speed() const { return FLY_SPEED * speedScale; }

void vrtc::Camera::Update(const Input &input, double dt, bool flying) {
    const glm::vec2 look = input.MouseDelta();
    yaw += look.x * static_cast<float>(MOUSE_SENSITIVITY);
    pitch -= look.y * static_cast<float>(MOUSE_SENSITIVITY);
    pitch = std::clamp(pitch, -89.f, 89.f);
    if (yaw > 180.f) yaw -= 360.f;
    if (yaw < -180.f) yaw += 360.f;

    if (const double scroll = input.ScrollDelta(); scroll != 0.0) {
        speedScale = std::clamp(speedScale * std::pow(1.25, scroll), 0.05, 50.0);
    }

    if (!flying) return;

    const glm::dvec3 forward = Forward();
    const glm::dvec3 right = glm::normalize(glm::cross(forward, glm::dvec3(0, 1, 0)));
    glm::dvec3 move{0};
    if (input.Down(GLFW_KEY_W)) move += forward;
    if (input.Down(GLFW_KEY_S)) move -= forward;
    if (input.Down(GLFW_KEY_D)) move += right;
    if (input.Down(GLFW_KEY_A)) move -= right;
    if (input.Down(GLFW_KEY_SPACE)) move.y += 1;
    if (input.Down(GLFW_KEY_LEFT_SHIFT)) move.y -= 1;
    if (glm::dot(move, move) > 0) {
        double speed = Speed();
        if (input.Down(GLFW_KEY_LEFT_CONTROL)) speed *= FLY_SPRINT_MULTIPLIER;
        position += glm::normalize(move) * speed * dt;
    }
    position.y = std::clamp(position.y, static_cast<double>(WORLD_MIN_BLOCK_Y) - 64.0,
                            static_cast<double>(WORLD_MAX_BLOCK_Y) + 256.0);
}

glm::vec3 vrtc::Camera::RelativeEye() const {
    const BlockPos origin = ChunkOrigin(CameraChunk());
    return glm::vec3(position - glm::dvec3(origin.x, origin.y, origin.z));
}

glm::mat4 vrtc::Camera::View() const {
    const glm::vec3 eye = RelativeEye();
    return glm::lookAt(eye, eye + Forward(), glm::vec3(0, 1, 0));
}

glm::mat4 vrtc::Camera::Projection(float aspect) const {
    return glm::perspective(glm::radians(FOV_DEGREES), aspect, NEAR_PLANE, FAR_PLANE);
}
