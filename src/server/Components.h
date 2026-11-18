#pragma once
// Server components (section 12). Plain data; systems in Systems.cpp act on them. Nothing here refers to meshes,
// textures or materials (hard rule 1): a visual ID is the only link to rendering.
#include <cstdint>
#include <glm/vec3.hpp>

#include "Config.h"
#include "ecs/Entity.h"

namespace vrtc {
    struct Transform {
        glm::dvec3 position{}; // centre of the body's box, world metres
        float yaw = 0, pitch = 0; // degrees
    };

    // Axis-aligned box around Transform::position. Bodies do not rotate (section 13).
    struct Collider {
        glm::dvec3 halfExtents{0.5};
    };

    // Everything the physics system integrates. SI units: metres, seconds, kilograms, newtons.
    struct RigidBody {
        glm::dvec3 velocity{}; // m/s
        glm::dvec3 acceleration{}; // m/s^2 requested for this tick by a controller; cleared after integration
        glm::dvec3 force{}; // N accumulated this tick (impulses, pushes); cleared after integration
        double mass = 1.0; // kg; heavier bodies are pushed less in body-body contacts
        double gravityScale = 1.0; // 0 floats
        double drag = 0.05; // 1/s linear air drag
        double friction = 8.0; // 1/s horizontal slowdown while on the ground
        double restitution = 0.0; // 0 = no bounce, 1 = perfectly elastic
        double stepHeight = STEP_HEIGHT; // ledges it climbs without jumping; 0 disables
        uint8_t hardness = 0; // hardness tier it can chip out of terrain on a hard impact; 0 = never
        bool onGround = false; // set by physics
        bool blockedHorizontally = false; // set by physics: a wall stopped it this tick
        bool frozen = false; // no physics (fly mode, or its chunk is not loaded yet)
    };

    struct Health {
        float current = 100, maximum = 100;
    };

    // A player's latest input. The connection is identified by session index.
    struct PlayerControl {
        uint32_t session = 0;
        float moveForward = 0, moveRight = 0;
        bool jump = false, sprint = false, fly = false;
        uint32_t lastSequence = 0;
    };

    enum class MobType : uint8_t { Chaser };

    struct MobAI {
        MobType type = MobType::Chaser;
        Entity target{}; // null until it picks one
    };

    // Present on everything clients should see.
    struct Replicated {
        uint16_t visual = 0;
    };
}
