#pragma once
#include <cstdint>
#include <glm/vec3.hpp>

#include "BlockRegistry.h"
#include "Config.h"
#include "Coords.h"
#include "Faces.h"
#include "Protocol.h"

namespace vrtc {
    // The edit test tool (section 13 dig/place). Changed from the debug panel, keys and the command line.
    struct ToolSettings {
        EditMode mode = EditMode::Micro;
        double reach = TOOL_REACH; // metres, up to TOOL_MAX_REACH
        double radius = TOOL_RADIUS; // sphere radius in metres, up to TOOL_MAX_RADIUS (whole-block mode: 0 = one block)
        int strength = TOOL_STRENGTH; // hardness tiers it cuts, 0 to 15
        BlockId block = 1; // what Place adds
    };

    // The light source: a sun whose direction, strength and colour follow its elevation. Changed from the panel.
    struct LightSettings {
        float sunElevation = SUN_ELEVATION_DEGREES; // degrees above the horizon; below zero is night
        float sunAzimuth = SUN_AZIMUTH_DEGREES; // degrees clockwise from north
        bool cycle = false; // sweep the elevation through a day and night
        bool fog = true; // fade terrain into the sky colour toward the load radius
    };

    // Client-side counters for the debug UI (hard rule 10). Updated every frame by Engine.
    struct ClientStats {
        bool welcomed = false;
        uint64_t seed = 0;
        uint32_t messagesThisFrame = 0;
        uint32_t messagesPending = 0; // left in the connection after the drain budget ran out
        double bytesPerSecond = 0;
        uint64_t bytesReceived = 0;
        uint32_t chunksInReplica = 0;
        size_t replicaBytes = 0;
        uint32_t chunksDirty = 0; // need a mesh; most are edge chunks still missing a neighbour
        uint32_t chunksMeshable = 0; // of those, how many could be meshed right now
        uint32_t meshJobsInFlight = 0;
        uint32_t pendingUploads = 0;
        uint32_t uploadsThisFrame = 0;
        uint64_t meshesBuilt = 0;
        double lastMeshMs = 0; // worker time of the most recent mesh job
        uint32_t lastWholeQuads = 0; // greedy whole-block quads in the most recent mesh job
        uint32_t lastMicroQuads = 0; // micro-voxel quads in the most recent mesh job
        uint32_t lastMicroRebuilt = 0, lastMicroReused = 0; // damaged blocks rebuilt vs taken from the cache
        double lastEditMeshMs = 0; // most recent remesh of a chunk that has damaged blocks
        uint32_t lastEditMeshQuads = 0, lastEditMeshRebuilt = 0, lastEditMeshReused = 0;
        double worstMeshMs = 0; // slowest mesh job in the last second
        double meshMsAccum = 0; // worker time spent meshing in the last second
        uint32_t chunksDrawn = 0;
        uint32_t chunksCulled = 0;
        uint64_t quadsDrawn = 0;
        uint64_t quadsSkipped = 0; // in visible chunks but facing away from the camera
        uint64_t quadsStored = 0; // on the GPU across all chunks
        uint32_t terrainDrawCalls = 0;
        uint32_t gpuChunks = 0;
        size_t gpuMeshBytes = 0;
        uint64_t deltasApplied = 0;
        uint64_t editsSent = 0;

        // The player as the server last reported it, and what else is around.
        bool fly = false;
        glm::dvec3 playerVelocity{};
        bool playerOnGround = false;
        uint32_t entities = 0;

        // What the camera is looking at within reach.
        bool targetHit = false;
        BlockPos targetBlock{};
        BlockId targetId = BLOCK_AIR;
        int targetFace = -1;
        bool targetDamaged = false;
        IVec3 targetMicro{};
        double targetDistance = 0;
    };
}
