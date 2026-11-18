#pragma once
// The client frame loop (section 11). Owns the window, the replica, the renderers and the debug UI. It talks to
// the server only through IConnection and never includes a server header (hard rule 1).
#include <algorithm>
#include <deque>
#include <unordered_map>
#include <string>
#include <vector>

#include "BlockRegistry.h"
#include "Camera.h"
#include "ChunkRenderer.h"
#include "ClientStats.h"
#include "ClientWorld.h"
#include "Config.h"
#include "Connection.h"
#include "DebugCounters.h"
#include "DebugUI.h"
#include "FrameStats.h"
#include "Frustum.h"
#include "GpuProfiler.h"
#include "Jobs.h"
#include "Material.h"
#include "Mesher.h"
#include "Queue.h"
#include "Raycast.h"
#include "Renderer.h"
#include "Serialise.h"
#include "Shader.h"
#include "Texture.h"
#include "Timing.h"
#include "Window.h"

namespace vrtc {
    struct ClientConfig {
        std::string playerName = "player";
        double runSeconds = 0; // > 0: quit after this long (smoke tests)
        double autoFly = 0; // > 0: fly forward at this speed without input (streaming and memory tests)
        int autoDig = 0; // > 0: dig this many times at whatever the camera looks at, as if LMB were held (edit tests)
        int autoPlace = 0; // > 0: place this many times, as if RMB were held
        bool fly = false; // start as a free camera instead of walking as the player
        float yaw = 0.f, pitch = 0.f; // initial look direction in degrees
        bool hasSpawn = false;
        glm::dvec3 spawn{}; // initial camera position when hasSpawn
        ToolSettings tool; // initial edit tool settings; the debug panel and keys change them at run time
    };

    class Engine {
        // Declaration order matters: everything below the window needs its GL context.
        Window window;
        Time time{TICK_SECONDS};
        JobSystem &jobs;
        IConnection &connection;
        const BlockRegistry &blocks;
        const ServerStats *serverStats;
        ClientConfig config;

        TextureManager textures;
        MaterialManager materials;
        Shader entityShader;
        Renderer entityRenderer{materials, textures};
        MeshTables meshTables;
        TextureHandle terrainTexture = NO_TEXTURE;
        ChunkRenderer chunkRenderer;
        ClientWorld world;
        Camera camera;
        GpuProfiler gpuProfiler;
        DebugUI debugUI;

        // Edit tool
        ToolSettings tool;
        RayHit target;
        double lastEditTime = -1;
        LightSettings light;
        double sunCyclePhase = 0;
        TerrainLighting lighting;
        ChunkPos lodChunk{INT32_MAX, 0, 0}; // camera chunk the micro LODs were last evaluated for
        MeshHandle outlineMesh = 0;
        MaterialHandle outlineTarget = 0, outlineShape = 0;

        // Player and entities (server-simulated; the client only interpolates what it is told)
        struct Interpolated {
            glm::dvec3 previous{}, current{};
            double receivedAt = 0; // when `current` arrived
            [[nodiscard]] glm::dvec3 At(double now) const {
                const double t = std::clamp((now - receivedAt) / TICK_SECONDS, 0.0, 1.0);
                return previous + (current - previous) * t;
            }
        };
        struct ClientEntity {
            uint16_t visual = 0;
            glm::vec3 halfExtents{0.5f};
            Interpolated position;
        };
        bool fly = false; // F3: free camera instead of walking as the player
        bool hasPlayerState = false;
        Interpolated playerPosition; // centre of the player box
        std::unordered_map<uint32_t, ClientEntity> entities;
        MeshHandle cubeMesh = 0;
        MaterialHandle enemyMaterial = 0, playerMaterial = 0;

        ResultQueue<MeshOutput> meshResults;
        std::vector<MeshOutput> meshScratch;
        std::deque<MeshOutput> pendingUploads;
        uint32_t meshJobsInFlight = 0;
        Frustum frustum;

        struct Candidate {
            int64_t score;
            size_t dirtyIndex;
        };

        std::vector<Candidate> candidates;

        FrameStats frameStats;
        ClientStats stats;
        double worstFrameMs = 0, worstFrameWindowStart = 0;
        uint64_t bytesInWindow = 0;
        double bytesWindowStart = 0;
        double worstMeshMs = 0, meshMsInWindow = 0;

        ByteWriter writer;
        std::vector<uint8_t> messageBuffer;
        uint32_t inputSequence = 0;

        void SendJoin();

        void SendInput();

        void SendDig(glm::dvec3 centre);

        void SendPlace(glm::dvec3 centre);

        void DrainMessages();

        void HandleMessage(std::span<const uint8_t> message);

        void UpdateTarget();

        void HandleToolInput(Input &input);

        void DrawOutlines();

        void DrawEntities();

        void CollectMeshes();

        void UploadMeshes();

        void ScheduleMeshing();

        void RenderTerrain();

        void UpdateLighting(double dt);

        void UpdateStats(); // counters shown by the debug panel

        void EndFrame(double frameStart); // frame timing, once per frame after the swap

    public:
        Engine(JobSystem &jobs, IConnection &connection, const BlockRegistry &blocks, const ServerStats *serverStats,
               ClientConfig config);

        ~Engine();

        Engine(const Engine &) = delete;

        Engine &operator=(const Engine &) = delete;

        void Run();
    };
}
