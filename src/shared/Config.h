#pragma once
// Every tunable number lives here (spec section 1). Values marked "initial" in the spec are starting points.
#include <cstdint>

namespace vrtc {
    // ---- World units (section 5) ----
    inline constexpr int CHUNK_BITS = 5; // chunk side is 32 blocks
    inline constexpr int CHUNK_SIZE = 1 << CHUNK_BITS;
    inline constexpr int CHUNK_MASK = CHUNK_SIZE - 1;
    inline constexpr int CHUNK_AREA = CHUNK_SIZE * CHUNK_SIZE;
    inline constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_AREA;
    inline constexpr int MICRO_RES = 16; // micro-voxels per block side (6.25 cm)
    inline constexpr int WORLD_MIN_CHUNK_Y = -4; // inclusive: lowest chunk row
    inline constexpr int WORLD_MAX_CHUNK_Y = 8; // exclusive: world spans blocks -128 to 255
    inline constexpr int WORLD_MIN_BLOCK_Y = WORLD_MIN_CHUNK_Y * CHUNK_SIZE;
    inline constexpr int WORLD_MAX_BLOCK_Y = WORLD_MAX_CHUNK_Y * CHUNK_SIZE - 1;    
    inline constexpr int VERTEX_STEPS = 16; // vertex position quantisation per block

    // ---- Simulation (section 5, 8) ----
    inline constexpr int TICK_RATE = 30;
    inline constexpr double TICK_SECONDS = 1.0 / TICK_RATE;
    // Section 5 lists 12 x 4 as the eventual radius; M2 is accepted at 8 and M5 raises it (DECISIONS.md).
    inline constexpr int LOAD_RADIUS_H = 12; // chunks kept loaded around a player, horizontally
    inline constexpr int LOAD_RADIUS_V = 3; // and vertically
    inline constexpr int UNLOAD_RADIUS_EXTRA = 2; // unload radius is this much larger, so edges do not thrash
    inline constexpr int UNLOAD_GRACE_TICKS = 3 * TICK_RATE; // a chunk with no ticket for this long is unloaded
    inline constexpr int SIM_RADIUS = 6; // entities and block entities tick inside this
    inline constexpr int TICKET_REFRESH_TICKS = TICK_RATE / 2; // how often tickets are re-scanned when nobody moved
    inline constexpr int CHUNK_SENDS_PER_TICK = 64; // replication rate limit per client
    inline constexpr int LOADS_IN_FLIGHT_PER_WORKER = 4; // generation jobs queued per worker thread

    // ---- Client (section 9, 11) ----
    inline constexpr int MESH_UPLOADS_PER_FRAME = 4;
    inline constexpr double MESH_UPLOAD_BUDGET_MS = 2.0;
    inline constexpr double MESSAGE_DRAIN_BUDGET_MS = 1.0;
    inline constexpr int MESH_JOBS_IN_FLIGHT_PER_WORKER = 2;
    inline constexpr int MICRO_LOD0_CHUNKS = 2; // damaged blocks render at 16^3 within this many chunks
    inline constexpr int MICRO_LOD1_CHUNKS = 4; // at 8^3 within this many
    inline constexpr int MICRO_LOD2_CHUNKS = 6; // at 4^3 within this many; beyond, as a whole block if at least half remains
    inline constexpr int MESH_DISPATCH_PER_FRAME = 8; // snapshots built on the main thread per frame
    inline constexpr double MESH_DISPATCH_BUDGET_MS = 1.5;
    inline constexpr float SUN_ELEVATION_DEGREES = 50.f; // initial sun height above the horizon
    inline constexpr float SUN_AZIMUTH_DEGREES = 215.f; // initial sun bearing, clockwise from north
    inline constexpr float SUN_CYCLE_SECONDS = 90.f; // length of a full day when the panel cycle is on
    inline constexpr float FOG_START_FRACTION = 0.55f; // fog begins at this fraction of the load radius
    inline constexpr double LONG_FRAME_MS = 20.0; // M2 acceptance: no frame longer than this while streaming

    // ---- Protocol (section 11) ----
    inline constexpr uint32_t PROTOCOL_VERSION = 5;

    // ---- Physics and bodies (section 13). All bodies are axis-aligned boxes simulated on the server. ----
    inline constexpr double GRAVITY = 28.0; // m/s^2, downward
    inline constexpr double PHYSICS_SUBSTEP = 0.05; // metres a body may move between collision tests (< 1 micro-voxel)
    inline constexpr int PHYSICS_MAX_SUBSTEPS = 96;
    inline constexpr double STEP_HEIGHT = 0.5; // bodies climb ledges up to this high without jumping
    inline constexpr double BOUNCE_MIN_SPEED = 2.0; // impacts slower than this do not bounce
    inline constexpr double IMPACT_CARVE_SPEED = 12.0; // a hard body hitting terrain faster than this chips it
    inline constexpr double PLAYER_HALF_WIDTH = 0.3, PLAYER_HALF_HEIGHT = 0.9; // 0.6 x 1.8 x 0.6 m
    inline constexpr double PLAYER_EYE_ABOVE_CENTRE = 0.72; // eye height 1.62 m
    inline constexpr double PLAYER_MASS = 80.0; // kg
    inline constexpr double WALK_SPEED = 4.3, SPRINT_SPEED = 5.6; // m/s
    inline constexpr double JUMP_HEIGHT = 1.25; // metres
    inline constexpr double GROUND_ACCEL = 12.0, AIR_ACCEL = 2.5; // how fast velocity follows input, 1/s
    inline constexpr double ENEMY_HALF_SIZE = 0.5; // a 1 m cube
    inline constexpr double ENEMY_MASS = 120.0;
    inline constexpr double ENEMY_SPEED = 3.4;
    inline constexpr double ENEMY_STOP_DISTANCE = 2.5; // metres from its target at which a chaser holds position
    inline constexpr int ENEMY_HARDNESS = 2; // what it can chip on a hard impact

    // ---- Edit tool (section 13 dig/place; test tool defaults until tools come from item data in M7) ----
    inline constexpr double TOOL_REACH = 5.0; // metres, initial
    inline constexpr double TOOL_RADIUS = 0.2; // metres, a bare hand's carve sphere
    inline constexpr int TOOL_STRENGTH = 15; // hardness tiers the test tool cuts (15 = everything breakable)
    inline constexpr double TOOL_MAX_REACH = 512.0; // the server rejects edits farther than this from the player
    inline constexpr double TOOL_MAX_RADIUS = 20.0; // and spheres larger than this
    inline constexpr int TOOL_COOLDOWN_TICKS = 3; // minimum ticks between two edits from one player
    inline constexpr double TOOL_REPEAT_SECONDS = 0.12; // client-side repeat while a button is held

    // ---- Camera (M1/M2 fly camera) ----
    inline constexpr float FOV_DEGREES = 70.f;
    inline constexpr float NEAR_PLANE = 0.05f;
    inline constexpr float FAR_PLANE = 12000.f;
    inline constexpr double FLY_SPEED = 20.0; // m/s
    inline constexpr double FLY_SPRINT_MULTIPLIER = 4.0;
    inline constexpr double MOUSE_SENSITIVITY = 0.1; // degrees per pixel
    inline constexpr double SPAWN_X = 0.5, SPAWN_Y = 96.0, SPAWN_Z = 0.5;

    // ---- Terrain generation (section 8) ----
    inline constexpr int WORLDGEN_LATTICE = 4; // density is sampled every 4 blocks and interpolated
    inline constexpr float WORLDGEN_BASE_HEIGHT = 40.f;
    inline constexpr float WORLDGEN_HEIGHT_AMPLITUDE = 75.f;
    inline constexpr float WORLDGEN_HEIGHT_FREQUENCY = 1.f / 1500.f;
    inline constexpr float WORLDGEN_DENSITY_FREQUENCY = 1.f / 2400.f;
    inline constexpr float WORLDGEN_DENSITY_AMPLITUDE = 28.f; // blocks of overhang the 3D noise can add
    inline constexpr float WORLDGEN_TUNNEL_FREQUENCY = 1.f / 250.f;
    inline constexpr float WORLDGEN_TUNNEL_RADIUS = 0.13f; // carve where a^2 + b^2 of two noise fields is below r^2
    inline constexpr float WORLDGEN_CHAMBER_FREQUENCY = 1.f / 64.f;
    inline constexpr float WORLDGEN_CHAMBER_THRESHOLD = 0.7f;
    inline constexpr int WORLDGEN_DIRT_DEPTH = 3;
    // Shape surface blocks with micro-voxel masks from the density field, so slopes step by 1/16 block instead
    // of whole blocks. Costs one 512-byte mask per surface block that the surface cuts through.
    inline constexpr bool WORLDGEN_SMOOTH_SLOPES = true;
    inline constexpr int WORLDGEN_FLOOR_START_Y = -100; // density ramps up to solid below here
    inline constexpr int WORLDGEN_CAVE_MIN_Y = -120; // no caves below here
    // Chunks that start above this are uniform air without sampling anything.
    inline constexpr int WORLDGEN_MAX_SURFACE_Y =
            static_cast<int>(WORLDGEN_BASE_HEIGHT + WORLDGEN_HEIGHT_AMPLITUDE + WORLDGEN_DENSITY_AMPLITUDE) + 2;
}
