#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>

#include "Log.h"
#include "TerrainTextures.h"

namespace {
    constexpr int64_t OUT_OF_VIEW_PENALTY = 1 << 20; // sorts out-of-frustum chunks after every visible one

    glm::dvec3 FaceNormal(int face) {
        if (face < 0) return {0, 0, 0};
        const vrtc::IVec3 n = vrtc::FACE_NORMAL[face];
        return {n.x, n.y, n.z};
    }

    glm::dvec3 BlockCentre(vrtc::BlockPos b) { return {b.x + 0.5, b.y + 0.5, b.z + 0.5}; }
}

vrtc::Engine::Engine(JobSystem &jobs, IConnection &connection, const BlockRegistry &blocks,
                     const ServerStats *serverStats, ClientConfig config)
    : window(WindowDesc{}), jobs(jobs), connection(connection), blocks(blocks), serverStats(serverStats),
      config(std::move(config)),
      entityShader(VRTC_ASSET_DIR "/shaders/entity.vert", VRTC_ASSET_DIR "/shaders/entity.frag"),
      world(blocks.Require("bedrock")), debugUI(window.Handle()), tool(this->config.tool) {
    terrainTexture = BuildTerrainTextures(textures, blocks, meshTables);
    camera.SetLook(this->config.yaw, this->config.pitch);
    if (this->config.hasSpawn) camera.SetPosition(this->config.spawn);

    // A unit cube drawn as 12 lines, for the target block and the edit shape.
    std::vector<EntityVertex> corners;
    for (int i = 0; i < 8; ++i) {
        corners.push_back({glm::vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1), glm::vec3(0), glm::vec2(0), 0.f});
    }
    const std::vector<uint32_t> edges = {0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 1, 3, 4, 6, 5, 7, 0, 4, 1, 5, 2, 6, 3, 7};
    outlineMesh = entityRenderer.CreateMesh(corners, edges, GL_LINES);
    outlineTarget = materials.Create(entityShader, NO_TEXTURE, glm::vec4(1.f, 1.f, 1.f, 1.f));
    outlineShape = materials.Create(entityShader, NO_TEXTURE, glm::vec4(1.f, .6f, .1f, 1.f));

    // A unit cube centred on the origin with face normals, scaled per entity to its box.
    std::vector<EntityVertex> cubeVertices;
    std::vector<uint32_t> cubeIndices;
    for (int f = 0; f < FACE_COUNT; ++f) {
        const auto base = static_cast<uint32_t>(cubeVertices.size());
        const glm::vec3 normal(FACE_NORMAL[f].x, FACE_NORMAL[f].y, FACE_NORMAL[f].z);
        for (const IVec3 &c: FACE_CORNERS[f]) {
            cubeVertices.push_back({glm::vec3(c.x, c.y, c.z) - 0.5f, normal, glm::vec2(0), 0.f});
        }
        cubeIndices.insert(cubeIndices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    cubeMesh = entityRenderer.CreateMesh(cubeVertices, cubeIndices);
    enemyMaterial = materials.Create(entityShader, NO_TEXTURE, glm::vec4(0.f, 0.9f, 0.9f, 1.f)); // cyan
    playerMaterial = materials.Create(entityShader, NO_TEXTURE, glm::vec4(0.9f, 0.75f, 0.3f, 1.f));
    fly = this->config.fly || this->config.autoFly > 0;

    log::Info("client: ready, {} worker(s), load radius {}x{}, tool reach {} radius {} mode {}", jobs.WorkerCount(),
              LOAD_RADIUS_H, LOAD_RADIUS_V, tool.reach, tool.radius, tool.mode == EditMode::Micro ? "micro" : "blocks");
}

vrtc::Engine::~Engine() {
    // Mesh jobs capture this object's tables and result queue; let them finish before members go away.
    jobs.WaitIdle();
}

void vrtc::Engine::SendJoin() {
    JoinMsg join;
    join.protocolVersion = PROTOCOL_VERSION;
    join.playerName = config.playerName;
    writer.Clear();
    Encode(writer, join);
    connection.Send(writer.Bytes(), Channel::Reliable);
}

void vrtc::Engine::SendInput() {
    PlayerInputMsg input;
    input.sequence = ++inputSequence;
    input.position = camera.Position(); // only used by the server in fly mode
    input.yaw = camera.Yaw();
    input.pitch = camera.Pitch();
    const Input &keys = window.GetInput();
    if (keys.Captured()) {
        input.moveForward = static_cast<float>(keys.Down(GLFW_KEY_W)) - static_cast<float>(keys.Down(GLFW_KEY_S));
        input.moveRight = static_cast<float>(keys.Down(GLFW_KEY_D)) - static_cast<float>(keys.Down(GLFW_KEY_A));
        if (keys.Down(GLFW_KEY_SPACE)) input.buttons |= INPUT_JUMP;
        if (keys.Down(GLFW_KEY_LEFT_CONTROL)) input.buttons |= INPUT_SPRINT;
    }
    if (fly) input.buttons |= INPUT_FLY;
    writer.Clear();
    Encode(writer, input);
    connection.Send(writer.Bytes(), Channel::Unreliable);
}

void vrtc::Engine::SendDig(glm::dvec3 centre) {
    DigMsg dig;
    dig.point = centre;
    dig.radius = static_cast<float>(tool.radius);
    dig.strength = static_cast<uint8_t>(std::clamp(tool.strength, 0, 15));
    dig.mode = tool.mode;
    writer.Clear();
    Encode(writer, dig);
    connection.Send(writer.Bytes(), Channel::Reliable);
    ++stats.editsSent;
}

void vrtc::Engine::SendPlace(glm::dvec3 centre) {
    PlaceMsg place;
    place.point = centre;
    place.radius = static_cast<float>(tool.radius);
    place.mode = tool.mode;
    place.block = tool.block;
    writer.Clear();
    Encode(writer, place);
    connection.Send(writer.Bytes(), Channel::Reliable);
    ++stats.editsSent;
}

void vrtc::Engine::DrainMessages() {
    const double start = Time::Now();
    stats.messagesThisFrame = 0;
    // A time budget, so a burst of chunks cannot stall a frame (section 11).
    while ((Time::Now() - start) * 1000.0 < MESSAGE_DRAIN_BUDGET_MS && connection.Poll(messageBuffer)) {
        ++stats.messagesThisFrame;
        stats.bytesReceived += messageBuffer.size();
        bytesInWindow += messageBuffer.size();
        HandleMessage(messageBuffer);
    }
}

void vrtc::Engine::HandleMessage(std::span<const uint8_t> message) {
    const auto type = PeekType(message);
    if (!type) return;
    ByteReader reader(message.subspan(1));
    switch (*type) {
        case MsgType::Welcome: {
            WelcomeMsg welcome;
            if (!Decode(reader, welcome)) return;
            if (welcome.chunkBits != CHUNK_BITS) {
                log::Fatal("client: server chunk size 2^{} does not match ours 2^{}", welcome.chunkBits, CHUNK_BITS);
            }
            stats.welcomed = true;
            stats.seed = welcome.seed;
            log::Info("client: welcomed, seed {}", welcome.seed);
            break;
        }
        case MsgType::ChunkData: {
            ChunkDataMsg data;
            if (!Decode(reader, data)) return;
            if (!world.ApplyChunkData(data)) log::Warn("client: bad chunk record at {},{},{}", data.pos.x, data.pos.y, data.pos.z);
            break;
        }
        case MsgType::ChunkUnload: {
            ChunkUnloadMsg unload;
            if (!Decode(reader, unload)) return;
            chunkRenderer.Free(world.ApplyUnload(unload.pos));
            break;
        }
        case MsgType::BlockDelta: {
            BlockDeltaMsg delta;
            if (!Decode(reader, delta)) return;
            if (world.ApplyBlockDelta(delta)) ++stats.deltasApplied;
            break;
        }
        case MsgType::PlayerState: {
            PlayerStateMsg state;
            if (!Decode(reader, state)) return;
            // Two states are kept so the camera moves smoothly between 30 Hz server ticks.
            playerPosition.previous = hasPlayerState ? playerPosition.current : state.position;
            playerPosition.current = state.position;
            playerPosition.receivedAt = Time::Now();
            hasPlayerState = true;
            stats.playerVelocity = state.velocity;
            stats.playerOnGround = state.onGround;
            break;
        }
        case MsgType::EntitySpawn: {
            EntitySpawnMsg spawn;
            if (!Decode(reader, spawn)) return;
            ClientEntity &entity = entities[spawn.id];
            entity.visual = spawn.visual;
            entity.halfExtents = spawn.halfExtents;
            entity.position = {spawn.position, spawn.position, Time::Now()};
            break;
        }
        case MsgType::EntityDespawn: {
            EntityDespawnMsg despawn;
            if (!Decode(reader, despawn)) return;
            entities.erase(despawn.id);
            break;
        }
        case MsgType::EntitySnapshot: {
            EntitySnapshotMsg snapshot;
            if (!Decode(reader, snapshot)) return;
            const double now = Time::Now();
            for (const EntityState &state: snapshot.entities) {
                const auto it = entities.find(state.id);
                if (it == entities.end()) continue; // not spawned for us (yet)
                it->second.position.previous = it->second.position.current;
                it->second.position.current = state.position;
                it->second.position.receivedAt = now;
            }
            break;
        }
        default:
            break;
    }
}

void vrtc::Engine::UpdateTarget() {
    const ReplicaQuery query{world, blocks};
    target = Raycast(query, camera.Position(), glm::dvec3(camera.Forward()), tool.reach);
    stats.targetHit = target.hit;
    stats.targetBlock = target.block;
    stats.targetId = target.id;
    stats.targetFace = target.face;
    stats.targetDamaged = target.damaged;
    stats.targetMicro = target.micro;
    stats.targetDistance = target.distance;
}

void vrtc::Engine::HandleToolInput(Input &input) {
    // Quick adjustments without leaving mouse capture; the debug panel has the same controls as sliders.
    if (input.Pressed(GLFW_KEY_TAB)) tool.mode = tool.mode == EditMode::Micro ? EditMode::WholeBlocks : EditMode::Micro;
    if (input.Pressed(GLFW_KEY_RIGHT_BRACKET)) tool.radius = std::min(tool.radius * 1.25 + 0.01, TOOL_MAX_RADIUS);
    if (input.Pressed(GLFW_KEY_LEFT_BRACKET)) tool.radius = std::max(tool.radius / 1.25 - 0.01, 0.0);
    if (input.Pressed(GLFW_KEY_EQUAL)) tool.reach = std::min(tool.reach * 1.25, TOOL_MAX_REACH);
    if (input.Pressed(GLFW_KEY_MINUS)) tool.reach = std::max(tool.reach / 1.25, 0.5);
    if (input.Pressed(GLFW_KEY_R)) {
        tool.block = static_cast<BlockId>(static_cast<size_t>(tool.block) + 1 >= blocks.Count() ? 1 : tool.block + 1);
    }

    const bool autoDig = config.autoDig > 0, autoPlace = config.autoPlace > 0;
    if (!target.hit || !(input.Captured() || autoDig || autoPlace)) return;
    const double now = Time::Now();
    const bool repeat = now - lastEditTime >= TOOL_REPEAT_SECONDS;
    const bool digHeld = (input.Captured() && input.MouseDown(GLFW_MOUSE_BUTTON_LEFT)) || autoDig;
    const bool placeHeld = (input.Captured() && input.MouseDown(GLFW_MOUSE_BUTTON_RIGHT)) || autoPlace;
    const bool dig = (input.Captured() && input.MousePressed(GLFW_MOUSE_BUTTON_LEFT)) || (digHeld && repeat);
    const bool place = !dig && ((input.Captured() && input.MousePressed(GLFW_MOUSE_BUTTON_RIGHT)) || (placeHeld && repeat));
    if (!dig && !place) return;

    const glm::dvec3 normal = FaceNormal(target.face);
    if (tool.mode == EditMode::Micro) {
        // A sphere centred just inside the surface digs a bowl; just outside it adds a bump.
        if (dig) SendDig(target.point - normal * 1e-3);
        else SendPlace(target.point + normal * 1e-3);
    } else {
        if (dig) SendDig(BlockCentre(target.block));
        else if (target.face >= 0) SendPlace(BlockCentre(target.block) + normal);
    }
    lastEditTime = now;
    if (dig && autoDig) --config.autoDig;
    if (place && autoPlace) --config.autoPlace;
}

void vrtc::Engine::DrawOutlines() {
    if (!target.hit) return;
    const BlockPos origin = ChunkOrigin(camera.CameraChunk());
    const glm::dvec3 originD(origin.x, origin.y, origin.z);
    const auto boxTransform = [&](glm::dvec3 min, double size) {
        glm::mat4 m = glm::translate(glm::mat4(1.f), glm::vec3(min - originD));
        return glm::scale(m, glm::vec3(static_cast<float>(size)));
    };

    // The block (or micro-voxel) the camera looks at.
    const glm::dvec3 block(target.block.x, target.block.y, target.block.z);
    if (target.damaged) {
        const glm::dvec3 cell = block + glm::dvec3(target.micro.x, target.micro.y, target.micro.z) / double(MICRO_RES);
        entityRenderer.Submit({MakeSortKey(outlineTarget, outlineMesh), outlineMesh, outlineTarget,
                               boxTransform(cell - glm::dvec3(0.002), 1.0 / MICRO_RES + 0.004)});
    } else {
        entityRenderer.Submit({MakeSortKey(outlineTarget, outlineMesh), outlineMesh, outlineTarget,
                               boxTransform(block - glm::dvec3(0.003), 1.006)});
    }

    // The bounding box of the edit that a click would apply.
    const glm::dvec3 normal = FaceNormal(target.face);
    glm::dvec3 centre;
    double half;
    if (tool.mode == EditMode::Micro) {
        centre = target.point;
        half = std::max(tool.radius, 1.0 / MICRO_RES);
    } else {
        centre = BlockCentre(target.block);
        half = std::max(tool.radius, 0.5);
    }
    (void) normal;
    entityRenderer.Submit({MakeSortKey(outlineShape, outlineMesh), outlineMesh, outlineShape,
                           boxTransform(centre - glm::dvec3(half), half * 2.0)});
}

void vrtc::Engine::CollectMeshes() {
    meshScratch.clear();
    meshResults.Drain(meshScratch);
    for (MeshOutput &result: meshScratch) {
        --meshJobsInFlight;
        ++stats.meshesBuilt;
        stats.lastMeshMs = result.workerMs;
        stats.lastWholeQuads = result.wholeQuads;
        stats.lastMicroQuads = result.microQuads;
        stats.lastMicroRebuilt = result.microRebuilt;
        stats.lastMicroReused = result.microReused;
        if (result.microRebuilt + result.microReused > 0) {
            // The most recent remesh of a chunk with damaged blocks: what an edit costs.
            stats.lastEditMeshMs = result.workerMs;
            stats.lastEditMeshQuads = result.wholeQuads + result.microQuads;
            stats.lastEditMeshRebuilt = result.microRebuilt;
            stats.lastEditMeshReused = result.microReused;
        }
        worstMeshMs = std::max(worstMeshMs, result.workerMs);
        meshMsInWindow += result.workerMs;
        ClientChunk *cc = world.Get(result.pos);
        if (!cc) continue; // unloaded while meshing
        cc->inFlight = false;
        world.StoreMicroCache(result);
        if (cc->version != result.version) continue; // replaced while meshing; it is dirty again already
        pendingUploads.push_back(std::move(result));
    }
}

void vrtc::Engine::UploadMeshes() {
    const double start = Time::Now();
    stats.uploadsThisFrame = 0;
    while (!pendingUploads.empty() && stats.uploadsThisFrame < MESH_UPLOADS_PER_FRAME
           && (Time::Now() - start) * 1000.0 < MESH_UPLOAD_BUDGET_MS) {
        MeshOutput &mesh = pendingUploads.front();
        if (ClientChunk *cc = world.Get(mesh.pos); cc && cc->version == mesh.version) {
            cc->gpu = chunkRenderer.Upload(mesh, cc->gpu);
            ++stats.uploadsThisFrame;
        }
        pendingUploads.pop_front();
    }
}

void vrtc::Engine::ScheduleMeshing() {
    const uint32_t maxInFlight = jobs.WorkerCount() * MESH_JOBS_IN_FLIGHT_PER_WORKER;
    std::vector<ChunkPos> &dirty = world.DirtyList();
    const ChunkPos cameraChunk = camera.CameraChunk();

    // One pass: drop stale entries, resolve chunks that need no job, score the rest.
    candidates.clear();
    for (size_t i = 0; i < dirty.size();) {
        ClientChunk *cc = world.Get(dirty[i]);
        if (!cc || !cc->dirty) { // unloaded, or a duplicate entry already handled
            dirty[i] = dirty.back();
            dirty.pop_back();
            continue;
        }
        if (!cc->inFlight && world.KnownEmpty(dirty[i], *cc, meshTables)) {
            cc->dirty = false;
            if (cc->gpu != NO_GPU_CHUNK) {
                chunkRenderer.Free(cc->gpu);
                cc->gpu = NO_GPU_CHUNK;
            }
            dirty[i] = dirty.back();
            dirty.pop_back();
            continue;
        }
        if (!cc->inFlight && world.Meshable(dirty[i], *cc)) {
            const glm::vec3 origin(static_cast<float>((dirty[i].x - cameraChunk.x) * CHUNK_SIZE),
                                   static_cast<float>((dirty[i].y - cameraChunk.y) * CHUNK_SIZE),
                                   static_cast<float>((dirty[i].z - cameraChunk.z) * CHUNK_SIZE));
            const bool inView = frustum.IntersectsBox(origin, origin + glm::vec3(CHUNK_SIZE));
            candidates.push_back({DistanceSq(dirty[i], cameraChunk) + (inView ? 0 : OUT_OF_VIEW_PENALTY), i});
        }
        ++i;
    }
    stats.chunksDirty = static_cast<uint32_t>(dirty.size());
    stats.chunksMeshable = static_cast<uint32_t>(candidates.size());

    if (meshJobsInFlight >= maxInFlight || candidates.empty()) return;
    // Snapshots are built on the main thread, so dispatch is budgeted per frame like uploads are.
    const size_t slots = std::min<size_t>({maxInFlight - meshJobsInFlight, candidates.size(), MESH_DISPATCH_PER_FRAME});
    std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(slots), candidates.end(),
                      [](const Candidate &a, const Candidate &b) { return a.score < b.score; });

    // Nearest and in-view first (section 9). Dispatch, then remove the picked entries from the dirty list by
    // index in descending order so swap-and-pop never moves an entry we still have to remove.
    const double dispatchStart = Time::Now();
    std::vector<size_t> picked;
    for (size_t k = 0; k < slots; ++k) {
        if (k > 0 && (Time::Now() - dispatchStart) * 1000.0 > MESH_DISPATCH_BUDGET_MS) break;
        const Candidate &candidate = candidates[k];
        const ChunkPos pos = dirty[candidate.dirtyIndex];
        ClientChunk *cc = world.Get(pos);
        auto input = std::make_unique<MeshInput>();
        const int lod = MicroLodFor(pos, cameraChunk);
        world.BuildMeshInput(pos, *cc, meshTables, lod, *input);
        cc->meshLod = static_cast<uint8_t>(lod);
        if (lod != 0) cc->microCache.Clear(); // quads are cached only for the nearest chunks
        cc->dirty = false;
        cc->inFlight = true;
        ++meshJobsInFlight;
        const auto priority = -static_cast<int32_t>(std::min<int64_t>(candidate.score, OUT_OF_VIEW_PENALTY * 2));
        jobs.Submit(priority, [in = std::move(input), tables = &meshTables, results = &meshResults]() mutable {
            results->Push(MeshChunk(*in, *tables));
        });
        picked.push_back(candidate.dirtyIndex);
    }
    std::ranges::sort(picked, std::greater<>());
    for (const size_t index: picked) {
        dirty[index] = dirty.back();
        dirty.pop_back();
    }
}

void vrtc::Engine::RenderTerrain() {
    glViewport(0, 0, window.FramebufferWidth(), window.FramebufferHeight());
    glClearColor(lighting.fogColour.r, lighting.fogColour.g, lighting.fogColour.b, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_FRAMEBUFFER_SRGB);

    const glm::mat4 viewProj = camera.Projection(window.Aspect()) * camera.View();
    frustum = Frustum(viewProj);
    const ChunkRenderer::DrawResult drawn = chunkRenderer.Draw(viewProj, frustum, camera.CameraChunk(),
                                                               camera.RelativeEye(), textures, terrainTexture, lighting);
    stats.chunksDrawn = drawn.drawn;
    stats.chunksCulled = drawn.culled;
    stats.quadsDrawn = drawn.quads;
    stats.quadsSkipped = drawn.quadsSkipped;
    stats.terrainDrawCalls = drawn.drawCalls;
    stats.quadsStored = chunkRenderer.StoredQuads();

    DrawOutlines();
    DrawEntities();
    entityRenderer.Flush(viewProj);

    glDisable(GL_FRAMEBUFFER_SRGB); // ImGui authors its colours for a linear framebuffer
}

void vrtc::Engine::EndFrame(double frameStart) {
    const double now = Time::Now();
    frameStats.totalMs = (now - frameStart) * 1000.0;
    worstFrameMs = std::max(worstFrameMs, frameStats.totalMs);
    if (frameStats.totalMs > LONG_FRAME_MS) {
        ++frameStats.longFrames;
        if (frameStats.longFrames <= 10) {
            log::Warn("client: long frame {:.1f} ms (events {:.1f}, drain {:.1f}, mesh {:.1f}, draw {:.1f}, ui {:.1f}, swap {:.1f})",
                      frameStats.totalMs, frameStats.eventsMs, frameStats.drainMs, frameStats.meshMs, frameStats.renderMs,
                      frameStats.debugUIMs, frameStats.swapMs);
        }
    }
    if (now - worstFrameWindowStart >= 1.0) {
        frameStats.worstMs = worstFrameMs;
        worstFrameMs = 0;
        worstFrameWindowStart = now;
    }
}

void vrtc::Engine::UpdateStats() {
    const double now = Time::Now();
    if (now - bytesWindowStart >= 1.0) {
        stats.bytesPerSecond = static_cast<double>(bytesInWindow) / (now - bytesWindowStart);
        bytesInWindow = 0;
        stats.worstMeshMs = worstMeshMs;
        stats.meshMsAccum = meshMsInWindow;
        worstMeshMs = 0;
        meshMsInWindow = 0;
        bytesWindowStart = now;
    }
    stats.chunksInReplica = static_cast<uint32_t>(world.Count());
    stats.replicaBytes = world.MemoryBytes();
    stats.meshJobsInFlight = meshJobsInFlight;
    stats.pendingUploads = static_cast<uint32_t>(pendingUploads.size());
    stats.gpuChunks = chunkRenderer.Count();
    stats.gpuMeshBytes = chunkRenderer.GpuBytes();
}

void vrtc::Engine::Run() {
    SendJoin();
    SendInput();
    const double runStart = Time::Now();
    worstFrameWindowStart = bytesWindowStart = runStart;
    Input &input = window.GetInput();

    while (!window.ShouldClose()) {
        double lapStart = Time::Now();
        const double frameStart = lapStart;
        const auto lap = [&lapStart] {
            const double now = Time::Now();
            const double ms = (now - lapStart) * 1000.0;
            lapStart = now;
            return ms;
        };

        // 1. Input, camera, tool, PlayerInput at the tick rate.
        time.Tick();
        window.PollEvents();
        if (input.Pressed(GLFW_KEY_ESCAPE)) input.SetCaptured(false);
        if (!input.Captured() && input.MousePressed(GLFW_MOUSE_BUTTON_LEFT) && !DebugUI::WantsMouse()) input.SetCaptured(true);
        if (input.Pressed(GLFW_KEY_F1)) debugUI.Toggle();
        if (input.Pressed(GLFW_KEY_F3)) fly = !fly;
        camera.Update(input, time.Dt(), fly);
        // Walking: the server simulates the player; the camera rides at eye height on the interpolated position.
        if (!fly && hasPlayerState) {
            camera.SetPosition(playerPosition.At(Time::Now()) + glm::dvec3(0.0, PLAYER_EYE_ABOVE_CENTRE, 0.0));
        }
        UpdateLighting(time.Dt());
        if (config.autoFly > 0) camera.SetPosition(camera.Position() + glm::dvec3(camera.Forward()) * config.autoFly * time.Dt());
        while (time.ShouldStep()) SendInput();
        UpdateTarget();
        HandleToolInput(input);
        if (const ChunkPos chunk = camera.CameraChunk(); chunk != lodChunk) {
            world.RefreshLod(chunk); // damaged blocks get coarser with distance
            lodChunk = chunk;
        }
        frameStats.eventsMs = lap();

        // 2. Server messages within a budget.
        DrainMessages();
        frameStats.drainMs = lap();

        // 3. Prediction: none yet, the camera is local. 4. Meshing jobs.
        CollectMeshes();
        UploadMeshes();
        ScheduleMeshing();
        frameStats.meshMs = lap();

        // 5. Render.
        if (window.IsMinimised()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            frameStats.renderMs = frameStats.debugUIMs = frameStats.swapMs = 0;
        } else {
            gpuProfiler.BeginFrame();
            gpuProfiler.BeginSceneStats();
            RenderTerrain();
            gpuProfiler.EndSceneStats();
            gpuProfiler.Mark(GpuSection::Terrain);
            frameStats.renderMs = lap();

            UpdateStats();
            const DebugContext ctx{
                frameStats, gpuProfiler, stats, serverStats, jobs, camera, entityRenderer.Stats(), blocks, tool, light,
                window.FramebufferWidth(), window.FramebufferHeight(), textures.GpuBytes(), entityRenderer.MeshBytes()
            };
            debugUI.Render(ctx);
            gpuProfiler.Mark(GpuSection::DebugUI);
            frameStats.debugUIMs = lap();

            window.SwapBuffers();
            frameStats.swapMs = lap();
        }
        UpdateStats();
        EndFrame(frameStart);

        if (config.runSeconds > 0 && Time::Now() - runStart >= config.runSeconds) break;
    }
    const glm::dvec3 p = camera.Position();
    log::Info("client: at {:.0f},{:.0f},{:.0f}: {} chunks in replica ({:.1f} MiB), {} on GPU ({:.1f} MiB), "
              "{} meshes built, {} edits sent, {} deltas applied, {} long frames",
              p.x, p.y, p.z, world.Count(), world.MemoryBytes() / (1024.0 * 1024.0), chunkRenderer.Count(),
              chunkRenderer.GpuBytes() / (1024.0 * 1024.0), stats.meshesBuilt, stats.editsSent, stats.deltasApplied,
              frameStats.longFrames);
}

void vrtc::Engine::UpdateLighting(double dt) {
    if (light.cycle) {
        // Sweep the sun from below the horizon to its peak and back: one day per SUN_CYCLE_SECONDS.
        sunCyclePhase += dt * 2.0 * 3.14159265358979 / SUN_CYCLE_SECONDS;
        light.sunElevation = static_cast<float>(35.0 + 55.0 * std::sin(sunCyclePhase));
    }
    const float elevation = glm::radians(light.sunElevation), azimuth = glm::radians(light.sunAzimuth);
    // Azimuth is clockwise from north (-Z), matching the camera's yaw.
    lighting.sunDir = glm::vec3(std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                                -std::cos(elevation) * std::cos(azimuth));

    // Everything follows how high the sun is: full day above about 15 degrees, night below the horizon,
    // with a warm tint while it is low.
    const float height = std::sin(elevation);
    const float day = glm::smoothstep(-0.10f, 0.25f, height);
    const float warm = 1.f - glm::smoothstep(0.0f, 0.45f, height);
    const glm::vec3 sunTint = glm::mix(glm::vec3(1.00f, 0.96f, 0.88f), glm::vec3(1.00f, 0.62f, 0.35f), warm);
    lighting.sunColour = sunTint * (0.75f * day);
    lighting.ambient = glm::mix(glm::vec3(0.035f, 0.045f, 0.08f), glm::vec3(0.42f, 0.47f, 0.55f), day);
    const glm::vec3 sky = glm::mix(glm::vec3(0.004f, 0.007f, 0.02f), glm::vec3(0.25f, 0.50f, 0.90f), day);
    lighting.fogColour = glm::mix(sky, glm::vec3(0.85f, 0.45f, 0.25f) * day, warm * 0.35f);

    if (light.fog) {
        // Fade out just inside the load radius, which hides chunks appearing (section 10).
        lighting.fogEnd = static_cast<float>(LOAD_RADIUS_H * CHUNK_SIZE - CHUNK_SIZE);
        lighting.fogStart = lighting.fogEnd * FOG_START_FRACTION;
    } else {
        lighting.fogStart = 1e9f;
        lighting.fogEnd = 2e9f;
    }
}

void vrtc::Engine::DrawEntities() {
    const BlockPos origin = ChunkOrigin(camera.CameraChunk());
    const glm::dvec3 originD(origin.x, origin.y, origin.z);
    const double now = Time::Now();
    for (const auto &[id, entity]: entities) {
        const MaterialHandle material = entity.visual == VISUAL_ENEMY ? enemyMaterial : playerMaterial;
        glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(entity.position.At(now) - originD));
        model = glm::scale(model, entity.halfExtents * 2.f);
        entityRenderer.Submit({MakeSortKey(material, cubeMesh), cubeMesh, material, model});
    }
    stats.entities = static_cast<uint32_t>(entities.size());
    stats.fly = fly;
}
