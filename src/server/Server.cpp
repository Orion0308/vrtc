#include "Server.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/geometric.hpp>

#include "ChunkSerial.h"
#include "Config.h"
#include "Log.h"
#include "Systems.h"
#include "Timing.h"

namespace {
    constexpr int UNLOAD_H = vrtc::LOAD_RADIUS_H + vrtc::UNLOAD_RADIUS_EXTRA;
    constexpr int UNLOAD_V = vrtc::LOAD_RADIUS_V + vrtc::UNLOAD_RADIUS_EXTRA;

    // Nearer chunks get a higher job priority so a moving player's new neighbourhood jumps the queue.
    int32_t LoadPriority(int64_t distanceSq) {
        return -static_cast<int32_t>(std::min<int64_t>(distanceSq, 1 << 20));
    }
}

vrtc::Server::Server(JobSystem &jobs, const BlockRegistry &blocks, uint64_t seed, ServerStats &stats, int enemies)
    : jobs(jobs), blocks(blocks), stats(stats), seed(seed),
      generator(seed, WorldGenBlocks{
                    blocks.Require("stone"), blocks.Require("dirt"), blocks.Require("grass"),
                    blocks.Require("bedrock")
                }),
      edit(world, blocks), enemyCount(enemies) {
}

vrtc::Server::~Server() {
    Stop();
}

void vrtc::Server::AddConnection(IConnection &connection) {
    Session session;
    session.connection = &connection;
    sessions.push_back(std::move(session));
    stats.clients.store(static_cast<uint32_t>(sessions.size()), std::memory_order_relaxed);
}

void vrtc::Server::Start() {
    if (thread.joinable()) return;
    thread = std::jthread([this](const std::stop_token &stop) { Run(stop); });
    log::Info("server: started, seed {}, {} worker(s)", seed, jobs.WorkerCount());
}

void vrtc::Server::Stop() {
    if (!thread.joinable()) return;
    thread.request_stop();
    thread.join();
    // Generation jobs capture this object; let them finish before anything is destroyed.
    jobs.WaitIdle();
    log::Info("server: stopped after {} ticks, {} chunks loaded, {} damaged blocks", tick, world.Count(), damagedBlocks);
}

void vrtc::Server::Run(const std::stop_token &stop) {
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(TICK_SECONDS));
    auto next = clock::now();
    while (!stop.stop_requested()) {
        const double start = Time::Now();
        Tick();
        const double ms = (Time::Now() - start) * 1000.0;
        stats.tickMs.store(ms, std::memory_order_relaxed);
        if (tick - worstTickWindowStart >= static_cast<uint32_t>(TICK_RATE)) {
            stats.tickMsMax.store(worstTickMs, std::memory_order_relaxed);
            worstTickMs = 0;
            worstTickWindowStart = tick;
        }
        worstTickMs = std::max(worstTickMs, ms);

        next += period;
        const auto now = clock::now();
        if (now > next + 3 * period) next = now; // far behind: drop the backlog rather than spin
        std::this_thread::sleep_until(next);
    }
}

void vrtc::Server::Tick() {
    ++tick;
    DrainMessages(); // 1. incoming messages, 2. player inputs and actions
    RunSystems(); // 3. systems in the section 12 order: player movement, mob AI, physics
    SyncSessions();
    FlushEdits(); // 4. world edit flush and change events
    UpdateStreaming(); // 5.
    SendReplication(); // 6.
    SendEntityState();
    stats.ticks.store(tick, std::memory_order_relaxed);
    stats.chunksLoaded.store(static_cast<uint32_t>(world.Count()), std::memory_order_relaxed);
    stats.chunksLoading.store(static_cast<uint32_t>(loading.size()), std::memory_order_relaxed);
    stats.loadQueue.store(static_cast<uint32_t>(loadQueue.size()), std::memory_order_relaxed);
    stats.worldBytes.store(world.MemoryBytes(), std::memory_order_relaxed);
}

void vrtc::Server::DrainMessages() {
    for (Session &session: sessions) {
        while (session.connection->Poll(messageScratch)) {
            stats.messagesReceived.fetch_add(1, std::memory_order_relaxed);
            HandleMessage(session, messageScratch);
        }
    }
}

void vrtc::Server::HandleMessage(Session &session, std::span<const uint8_t> message) {
    const auto type = PeekType(message);
    if (!type) return;
    ByteReader reader(message.subspan(1));
    switch (*type) {
        case MsgType::Join: {
            JoinMsg join;
            if (!Decode(reader, join)) return;
            if (join.protocolVersion != PROTOCOL_VERSION) {
                log::Error("server: client '{}' uses protocol {}, expected {}", join.playerName,
                           join.protocolVersion, PROTOCOL_VERSION);
                return;
            }
            session.joined = true;
            WelcomeMsg welcome;
            welcome.playerEntity = 0; // the entity is created on the first PlayerInput
            welcome.seed = seed;
            welcome.chunkBits = CHUNK_BITS;
            welcome.loadRadiusH = LOAD_RADIUS_H;
            welcome.loadRadiusV = LOAD_RADIUS_V;
            writer.Clear();
            Encode(writer, welcome);
            session.connection->Send(writer.Bytes(), Channel::Reliable);
            log::Info("server: '{}' joined", join.playerName);
            break;
        }
        case MsgType::PlayerInput: {
            PlayerInputMsg input;
            if (!Decode(reader, input)) return;
            HandlePlayerInput(session, input);
            break;
        }
        case MsgType::Dig: {
            DigMsg dig;
            if (Decode(reader, dig)) HandleDig(session, dig);
            break;
        }
        case MsgType::Place: {
            PlaceMsg place;
            if (Decode(reader, place)) HandlePlace(session, place);
            break;
        }
        default:
            break; // later milestones
    }
}

bool vrtc::Server::ValidateEdit(Session &session, glm::dvec3 point, float radius) {
    bool ok = session.joined && session.hasPosition;
    ok = ok && std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) && std::isfinite(radius);
    ok = ok && radius >= 0.f && radius <= TOOL_MAX_RADIUS;
    ok = ok && glm::length(point - session.position) <= TOOL_MAX_REACH + radius;
    ok = ok && tick - session.lastEditTick >= static_cast<uint32_t>(TOOL_COOLDOWN_TICKS);
    if (!ok) {
        stats.editsRejected.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    session.lastEditTick = tick;
    stats.edits.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void vrtc::Server::HandleDig(Session &session, const DigMsg &dig) {
    if (!ValidateEdit(session, dig.point, dig.radius)) return;
    const double start = Time::Now();
    const Sphere sphere{dig.point, static_cast<double>(dig.radius)};
    const uint8_t strength = std::min<uint8_t>(dig.strength, 15);
    if (dig.mode == EditMode::WholeBlocks) {
        // Radius 0 still removes the block the point is in: use a sphere that reaches that block's centre.
        const uint32_t removed = edit.RemoveBlocks(Sphere{sphere.centre, std::max(sphere.radius, 0.01)}, strength);
        stats.bitsCarved.fetch_add(static_cast<uint64_t>(removed) * MicroBlock::BITS, std::memory_order_relaxed);
    } else {
        const CarveResult result = edit.Carve(sphere, strength);
        stats.bitsCarved.fetch_add(result.totalBits, std::memory_order_relaxed);
        // CarveResult drives drops once inventories exist (M7): 4096 bits of a material make one block item.
    }
    stats.lastEditMs.store((Time::Now() - start) * 1000.0, std::memory_order_relaxed);
}

void vrtc::Server::HandlePlace(Session &session, const PlaceMsg &place) {
    if (place.block == BLOCK_AIR || place.block >= blocks.Count()) {
        stats.editsRejected.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!ValidateEdit(session, place.point, place.radius)) return;
    const double start = Time::Now();
    const Sphere sphere{place.point, static_cast<double>(place.radius)};
    if (place.mode == EditMode::WholeBlocks) {
        const uint32_t placed = edit.PlaceBlocks(Sphere{sphere.centre, std::max(sphere.radius, 0.01)}, place.block);
        stats.bitsFilled.fetch_add(static_cast<uint64_t>(placed) * MicroBlock::BITS, std::memory_order_relaxed);
    } else {
        const FillResult result = edit.Fill(sphere, place.block);
        stats.bitsFilled.fetch_add(result.bitsAdded, std::memory_order_relaxed);
    }
    stats.lastEditMs.store((Time::Now() - start) * 1000.0, std::memory_order_relaxed);
}

void vrtc::Server::FlushEdits() {
    if (!edit.HasChanges()) return;
    damagedBlocks += edit.TakeDamagedDelta();
    stats.damagedBlocks.store(static_cast<uint32_t>(std::max<int64_t>(damagedBlocks, 0)), std::memory_order_relaxed);
    edit.Flush([&](ChunkPos pos, Chunk &chunk, std::span<const uint16_t> indices) {
        // Replication listener: one BlockDelta per edited chunk to every client that holds it.
        // (Navigation, persistence and block-entity listeners attach here in later milestones.)
        BlockDeltaMsg delta;
        delta.pos = pos;
        delta.version = chunk.version;
        delta.entries.reserve(indices.size());
        for (const uint16_t index: indices) {
            BlockDeltaEntry &entry = delta.entries.emplace_back();
            entry.index = index;
            entry.id = chunk.GetIndex(index);
            if (const MicroBlock *mask = chunk.Micro(index)) {
                entry.hasMask = true;
                entry.mask = *mask;
            }
        }
        writer.Clear();
        Encode(writer, delta);
        for (Session &session: sessions) {
            if (!session.held.contains(pos)) continue;
            session.connection->Send(writer.Bytes(), Channel::Reliable);
            stats.bytesSent.fetch_add(writer.Size(), std::memory_order_relaxed);
        }
        stats.blocksChanged.fetch_add(indices.size(), std::memory_order_relaxed);
    });
}

bool vrtc::Server::CoveredByTicket(ChunkPos pos, int extra) const {
    for (const Session &s: sessions) {
        if (s.hasPosition && WithinRadius(pos, s.chunk, LOAD_RADIUS_H + extra, LOAD_RADIUS_V + extra)) return true;
    }
    return false;
}

void vrtc::Server::UpdateStreaming() {
    FinishLoads();
    if (ticketsDirty || tick % TICKET_REFRESH_TICKS == 0) {
        RefreshTickets();
        UnloadStale();
        ticketsDirty = false;
    }
    DispatchLoads();
    for (Session &session: sessions) {
        if (session.viewDirty) {
            RefreshView(session);
            session.viewDirty = false;
        }
    }
}

void vrtc::Server::RefreshTickets() {
    // Rebuild the wanted-but-missing list from every player's ticket, nearest first.
    loadQueue.clear();
    std::unordered_set<ChunkPos, ChunkPosHash> queued;
    for (const Session &s: sessions) {
        if (!s.hasPosition) continue;
        for (int dy = -LOAD_RADIUS_V; dy <= LOAD_RADIUS_V; ++dy) {
            const int cy = s.chunk.y + dy;
            if (!ChunkYInWorld(cy)) continue;
            for (int dz = -LOAD_RADIUS_H; dz <= LOAD_RADIUS_H; ++dz) {
                for (int dx = -LOAD_RADIUS_H; dx <= LOAD_RADIUS_H; ++dx) {
                    const ChunkPos pos{s.chunk.x + dx, cy, s.chunk.z + dz};
                    if (LoadedChunk *loaded = world.GetLoaded(pos)) {
                        loaded->lastTicketTick = tick;
                    } else if (!loading.contains(pos) && queued.insert(pos).second) {
                        loadQueue.push_back({pos, DistanceSq(pos, s.chunk)});
                    }
                }
            }
        }
    }
    // Chunks in the wider unload ring keep their ticket too, so the edge does not thrash.
    for (auto &[pos, loaded]: world.Chunks()) {
        if (loaded.lastTicketTick != tick && CoveredByTicket(pos, UNLOAD_RADIUS_EXTRA)) loaded.lastTicketTick = tick;
    }
    std::ranges::sort(loadQueue, [](const LoadRequest &a, const LoadRequest &b) {
        return a.distanceSq > b.distanceSq; // farthest first, so pop_back yields the nearest
    });
}

void vrtc::Server::DispatchLoads() {
    const size_t maxInFlight = static_cast<size_t>(jobs.WorkerCount()) * LOADS_IN_FLIGHT_PER_WORKER;
    while (!loadQueue.empty() && loading.size() < maxInFlight) {
        const LoadRequest request = loadQueue.back();
        loadQueue.pop_back();
        if (world.GetChunk(request.pos) || !loading.insert(request.pos).second) continue;
        const ChunkPos pos = request.pos;
        jobs.Submit(LoadPriority(request.distanceSq), [this, pos] {
            generated.Push(GenResult{pos, generator.Generate(pos)});
        });
    }
}

void vrtc::Server::FinishLoads() {
    generatedScratch.clear();
    generated.Drain(generatedScratch);
    for (GenResult &result: generatedScratch) {
        loading.erase(result.pos);
        stats.chunksGenerated.fetch_add(1, std::memory_order_relaxed);
        // The player may have moved on while this generated; keep only what a ticket still covers.
        if (!CoveredByTicket(result.pos, UNLOAD_RADIUS_EXTRA) || world.GetChunk(result.pos)) continue;
        world.Insert(result.pos, std::move(result.chunk), tick);
        for (Session &session: sessions) {
            if (session.hasPosition && WithinRadius(result.pos, session.chunk, LOAD_RADIUS_H, LOAD_RADIUS_V)) {
                session.pendingSend.push_back(result.pos);
            }
        }
    }
}

void vrtc::Server::UnloadStale() {
    std::vector<ChunkPos> stale;
    for (const auto &[pos, loaded]: world.Chunks()) {
        if (tick - loaded.lastTicketTick > static_cast<uint32_t>(UNLOAD_GRACE_TICKS)) stale.push_back(pos);
    }
    for (const ChunkPos pos: stale) {
        // Edited chunks are dropped with their changes until persistence arrives (M6).
        if (const Chunk *chunk = world.GetChunk(pos)) damagedBlocks -= static_cast<int64_t>(chunk->MicroCount());
        world.Remove(pos);
        stats.chunksUnloaded.fetch_add(1, std::memory_order_relaxed);
        for (Session &session: sessions) {
            if (session.held.erase(pos)) SendUnload(session, pos);
        }
    }
}

void vrtc::Server::RefreshView(Session &session) {
    if (!session.hasPosition) return;
    // Drop chunks that left the client's view.
    std::vector<ChunkPos> gone;
    for (const ChunkPos pos: session.held) {
        if (!WithinRadius(pos, session.chunk, UNLOAD_H, UNLOAD_V)) gone.push_back(pos);
    }
    for (const ChunkPos pos: gone) {
        session.held.erase(pos);
        SendUnload(session, pos);
    }
    // Everything loaded and in view that the client does not hold yet, nearest last.
    session.pendingSend.clear();
    for (int dy = -LOAD_RADIUS_V; dy <= LOAD_RADIUS_V; ++dy) {
        const int cy = session.chunk.y + dy;
        if (!ChunkYInWorld(cy)) continue;
        for (int dz = -LOAD_RADIUS_H; dz <= LOAD_RADIUS_H; ++dz) {
            for (int dx = -LOAD_RADIUS_H; dx <= LOAD_RADIUS_H; ++dx) {
                const ChunkPos pos{session.chunk.x + dx, cy, session.chunk.z + dz};
                if (!session.held.contains(pos) && world.GetChunk(pos)) session.pendingSend.push_back(pos);
            }
        }
    }
    const ChunkPos centre = session.chunk;
    std::ranges::sort(session.pendingSend, [centre](ChunkPos a, ChunkPos b) {
        return DistanceSq(a, centre) > DistanceSq(b, centre);
    });
}

void vrtc::Server::SendReplication() {
    for (Session &session: sessions) {
        if (!session.joined) continue;
        int sent = 0;
        while (!session.pendingSend.empty() && sent < CHUNK_SENDS_PER_TICK) {
            const ChunkPos pos = session.pendingSend.back();
            session.pendingSend.pop_back();
            if (session.held.contains(pos)) continue;
            const Chunk *chunk = world.GetChunk(pos);
            if (!chunk) continue;
            SendChunk(session, pos, *chunk);
            session.held.insert(pos);
            ++sent;
        }
    }
}

void vrtc::Server::SendChunk(Session &session, ChunkPos pos, const Chunk &chunk) {
    static thread_local ByteWriter record;
    record.Clear();
    WriteChunkRecord(record, chunk);
    ChunkDataMsg msg;
    msg.pos = pos;
    msg.version = chunk.version;
    msg.record = record.Bytes();
    writer.Clear();
    Encode(writer, msg);
    session.connection->Send(writer.Bytes(), Channel::Reliable);
    stats.chunksSent.fetch_add(1, std::memory_order_relaxed);
    stats.bytesSent.fetch_add(writer.Size(), std::memory_order_relaxed);
}

void vrtc::Server::SendUnload(Session &session, ChunkPos pos) {
    writer.Clear();
    Encode(writer, ChunkUnloadMsg{pos});
    session.connection->Send(writer.Bytes(), Channel::Reliable);
    stats.bytesSent.fetch_add(writer.Size(), std::memory_order_relaxed);
}

namespace {
    // Entity handles travel as one integer: 20 bits of index, 12 of generation.
    uint32_t WireId(vrtc::Entity e) { return e.id | (static_cast<uint32_t>(e.gen) << 20); }
}

void vrtc::Server::HandlePlayerInput(Session &session, const PlayerInputMsg &input) {
    if (!session.joined) return;
    if (!session.player.IsNull() && input.sequence <= session.lastInputSequence) return; // stale
    session.lastInputSequence = input.sequence;

    if (session.player.IsNull()) {
        // First input: the player appears where the client says it is, with a body the physics system owns
        // from here on.
        const Entity player = registry.Create();
        registry.Add(player, Transform{input.position, input.yaw, input.pitch});
        registry.Add(player, Collider{glm::dvec3(PLAYER_HALF_WIDTH, PLAYER_HALF_HEIGHT, PLAYER_HALF_WIDTH)});
        RigidBody body;
        body.mass = PLAYER_MASS;
        body.friction = 0.0; // the movement system already steers velocity toward the input
        registry.Add(player, body);
        registry.Add(player, Health{});
        registry.Add(player, PlayerControl{static_cast<uint32_t>(&session - sessions.data())});
        registry.Add(player, Replicated{VISUAL_PLAYER});
        session.player = player;
        log::Info("server: player entity {} spawned at {:.1f}, {:.1f}, {:.1f}", WireId(player), input.position.x,
                  input.position.y, input.position.z);

        // Company: chasers in a ring a few metres out.
        for (int i = 0; i < enemyCount; ++i) {
            const double angle = 6.283185307179586 * i / std::max(enemyCount, 1);
            SpawnEnemy(input.position + glm::dvec3(std::cos(angle) * 8.0, 2.0, std::sin(angle) * 8.0));
        }
    }

    PlayerControl &control = registry.Get<PlayerControl>(session.player);
    Transform &transform = registry.Get<Transform>(session.player);
    control.moveForward = std::clamp(input.moveForward, -1.f, 1.f);
    control.moveRight = std::clamp(input.moveRight, -1.f, 1.f);
    control.jump = (input.buttons & INPUT_JUMP) != 0;
    control.sprint = (input.buttons & INPUT_SPRINT) != 0;
    control.fly = (input.buttons & INPUT_FLY) != 0;
    control.lastSequence = input.sequence;
    transform.yaw = input.yaw;
    transform.pitch = input.pitch;
    // Fly mode is a debug camera: the client's position is taken as is and physics leaves the body alone.
    if (control.fly) transform.position = input.position;
}

vrtc::Entity vrtc::Server::SpawnEnemy(glm::dvec3 position) {
    const Entity enemy = registry.Create();
    registry.Add(enemy, Transform{position, 0.f, 0.f});
    registry.Add(enemy, Collider{glm::dvec3(ENEMY_HALF_SIZE)});
    RigidBody body;
    body.mass = ENEMY_MASS;
    body.friction = 0.0; // steered by its AI
    body.restitution = 0.2;
    body.hardness = static_cast<uint8_t>(ENEMY_HARDNESS);
    registry.Add(enemy, body);
    registry.Add(enemy, Health{});
    registry.Add(enemy, MobAI{});
    registry.Add(enemy, Replicated{VISUAL_ENEMY});

    EntitySpawnMsg spawn;
    spawn.id = WireId(enemy);
    spawn.visual = VISUAL_ENEMY;
    spawn.position = position;
    spawn.halfExtents = glm::vec3(static_cast<float>(ENEMY_HALF_SIZE));
    writer.Clear();
    Encode(writer, spawn);
    for (Session &session: sessions) {
        if (session.joined) session.connection->Send(writer.Bytes(), Channel::Reliable);
    }
    return enemy;
}

void vrtc::Server::RunSystems() {
    const double start = Time::Now();
    SystemContext ctx{registry, world, edit, blocks, TICK_SECONDS};
    PlayerMovementSystem(ctx); // 1. player input and movement intent
    MobAISystem(ctx); // 2. mob AI
    PhysicsSystem(ctx); // 3. movement and collision for every body
    stats.systemsMs.store((Time::Now() - start) * 1000.0, std::memory_order_relaxed);
    stats.entities.store(registry.AliveCount(), std::memory_order_relaxed);
}

void vrtc::Server::SyncSessions() {
    for (Session &session: sessions) {
        if (session.player.IsNull()) continue;
        session.position = registry.Get<Transform>(session.player).position;
        const ChunkPos chunk = ToChunkPos(ToBlockPos(session.position));
        if (!session.hasPosition || chunk != session.chunk) {
            session.chunk = chunk;
            session.hasPosition = true;
            session.viewDirty = true;
            ticketsDirty = true;
        }
    }
}

void vrtc::Server::SendEntityState() {
    // One snapshot of every replicated entity; each client skips its own player, which it gets as PlayerState.
    EntitySnapshotMsg snapshot;
    snapshot.tick = tick;
    registry.ForEach<Replicated, Transform>([&](Entity entity, Replicated &, Transform &transform) {
        snapshot.entities.push_back({WireId(entity), transform.position, transform.yaw});
    });

    for (Session &session: sessions) {
        if (!session.joined || session.player.IsNull()) continue;
        const RigidBody &body = registry.Get<RigidBody>(session.player);
        PlayerStateMsg state;
        state.lastSequence = session.lastInputSequence;
        state.position = session.position;
        state.velocity = body.velocity;
        state.onGround = body.onGround;
        writer.Clear();
        Encode(writer, state);
        session.connection->Send(writer.Bytes(), Channel::Unreliable);

        const uint32_t own = WireId(session.player);
        EntitySnapshotMsg mine;
        mine.tick = tick;
        for (const EntityState &e: snapshot.entities) {
            if (e.id != own) mine.entities.push_back(e);
        }
        writer.Clear();
        Encode(writer, mine);
        session.connection->Send(writer.Bytes(), Channel::Unreliable);
    }
}
