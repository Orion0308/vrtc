#pragma once
// Server systems (section 12): plain functions over the registry, run by Server::Tick in a fixed order.
#include "BlockRegistry.h"
#include "World.h"
#include "WorldEdit.h"
#include "ecs/Registry.h"

namespace vrtc {
    struct SystemContext {
        Registry &registry;
        World &world;
        WorldEdit &edit;
        const BlockRegistry &blocks;
        double dt; // seconds per tick
    };

    // Turns each player's latest input into a requested acceleration (and a jump).
    void PlayerMovementSystem(SystemContext &ctx);

    // Chasers steer toward the nearest player and hop when a wall stops them.
    void MobAISystem(SystemContext &ctx);

    // Integrates every RigidBody and resolves it against the exact micro-voxel world, then against other bodies.
    void PhysicsSystem(SystemContext &ctx);
}
