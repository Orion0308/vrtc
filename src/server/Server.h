#pragma once
// The server (section 11): a fixed 30 Hz tick on its own thread. It owns the World and talks to clients only
// through IConnection. Scope so far: join, player tickets, chunk streaming, and Dig/Place through WorldEdit
// replicated as BlockDelta.
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <unordered_set>
#include <vector>
#include <glm/vec3.hpp>

#include "BlockRegistry.h"
#include "Components.h"
#include "Connection.h"
#include "DebugCounters.h"
#include "Jobs.h"
#include "Queue.h"
#include "Serialise.h"
#include "World.h"
#include "WorldEdit.h"
#include "WorldGen.h"
#include "ecs/Registry.h"

namespace vrtc {
    class Server {
        struct Session {
            IConnection *connection = nullptr;
            bool joined = false;
            bool hasPosition = false;
            glm::dvec3 position{};
            ChunkPos chunk{};
            uint32_t lastInputSequence = 0;
            Entity player{}; // created on the first PlayerInput
            uint32_t lastEditTick = 0;
            std::unordered_set<ChunkPos, ChunkPosHash> held; // chunks this client has been sent
            std::vector<ChunkPos> pendingSend; // loaded, in view, not yet sent; farthest first so back() is nearest
            bool viewDirty = true;
        };

        struct GenResult {
            ChunkPos pos;
            std::unique_ptr<Chunk> chunk;
        };

        struct LoadRequest {
            ChunkPos pos;
            int64_t distanceSq;
        };

        JobSystem &jobs;
        const BlockRegistry &blocks;
        ServerStats &stats;
        const uint64_t seed;
        WorldGen generator;
        World world;
        WorldEdit edit;
        Registry registry; // entities: players, mobs (section 12)
        int enemyCount; // chasers spawned beside each player when they appear
        std::vector<Session> sessions;
        std::unordered_set<ChunkPos, ChunkPosHash> loading; // generation jobs in flight
        std::vector<LoadRequest> loadQueue; // nearest last, so back() is next
        ResultQueue<GenResult> generated;
        std::vector<GenResult> generatedScratch;
        std::vector<uint8_t> messageScratch;
        ByteWriter writer;
        uint32_t tick = 0;
        bool ticketsDirty = true;
        double worstTickMs = 0;
        uint32_t worstTickWindowStart = 0;
        int64_t damagedBlocks = 0;
        std::jthread thread;

        void Run(const std::stop_token &stop);

        void Tick();

        void DrainMessages();

        void HandleMessage(Session &session, std::span<const uint8_t> message);

        void HandleDig(Session &session, const DigMsg &dig);

        void HandlePlace(Session &session, const PlaceMsg &place);

        // Reach, size and cooldown checks shared by Dig and Place. False (and counted) when rejected.
        bool ValidateEdit(Session &session, glm::dvec3 point, float radius);

        void FlushEdits();

        void HandlePlayerInput(Session &session, const PlayerInputMsg &input);

        Entity SpawnEnemy(glm::dvec3 position);

        void RunSystems();

        // Copies each player entity position into its session, which drives tickets and streaming.
        void SyncSessions();

        void SendEntityState();

        void UpdateStreaming();

        void RefreshTickets();

        void DispatchLoads();

        void FinishLoads();

        void UnloadStale();

        void RefreshView(Session &session);

        void SendReplication();

        void SendChunk(Session &session, ChunkPos pos, const Chunk &chunk);

        void SendUnload(Session &session, ChunkPos pos);

        [[nodiscard]] bool CoveredByTicket(ChunkPos pos, int extra) const;

    public:
        Server(JobSystem &jobs, const BlockRegistry &blocks, uint64_t seed, ServerStats &stats, int enemies = 1);

        ~Server();

        Server(const Server &) = delete;

        Server &operator=(const Server &) = delete;

        // Call before Start. Each connection is one player.
        void AddConnection(IConnection &connection);

        void Start();

        // Joins the tick thread and waits for outstanding generation jobs. Safe to call twice.
        void Stop();

        [[nodiscard]] uint64_t Seed() const { return seed; }
    };
}
