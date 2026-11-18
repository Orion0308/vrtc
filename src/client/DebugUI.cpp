#include "DebugUI.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "Config.h"

namespace {
    // Not in our glad build; values from the extension specs. Both report kilobytes.
    constexpr GLenum GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX = 0x9048;
    constexpr GLenum GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX = 0x9049;
    constexpr GLenum TEXTURE_FREE_MEMORY_ATI = 0x87FC;
    constexpr double GPU_BOUND_THRESHOLD = .9;
    constexpr const char *FACE_NAMES[6] = {"+Z (south)", "-Z (north)", "+X (east)", "-X (west)", "+Y (top)", "-Y (bottom)"};

    std::string GlString(GLenum name) {
        const auto *s = reinterpret_cast<const char *>(glGetString(name));
        return s ? s : "?";
    }

    bool HasExtension(const char *name) {
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; ++i) {
            const auto *ext = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
            if (ext && std::strcmp(ext, name) == 0) return true;
        }
        return false;
    }

    double ToMiB(size_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

    void TimeBar(const char *label, double ms, double frameMs) {
        char overlay[32];
        std::snprintf(overlay, sizeof overlay, "%.2f ms", ms);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(120.f);
        ImGui::ProgressBar(frameMs > 0 ? static_cast<float>(ms / frameMs) : 0.f, ImVec2(-1.f, 0.f), overlay);
    }
}

vrtc::DebugUI::DebugUI(GLFWwindow *window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true); // chains onto the callbacks Window installed
    ImGui_ImplOpenGL3_Init("#version 460");
    glVendor = GlString(GL_VENDOR);
    glRenderer = GlString(GL_RENDERER);
    glVersion = GlString(GL_VERSION);
    hasNvMemInfo = HasExtension("GL_NVX_gpu_memory_info");
    hasAtiMemInfo = HasExtension("GL_ATI_meminfo");
}

vrtc::DebugUI::~DebugUI() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

bool vrtc::DebugUI::WantsMouse() {
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
}

void vrtc::DebugUI::DrawFrameSection(const DebugContext &ctx) {
    const FrameStats &frame = ctx.frame;
    const double gpuMs = ctx.gpu.TotalMs();
    cpuHistory[historyHead] = static_cast<float>(frame.totalMs);
    gpuHistory[historyHead] = static_cast<float>(gpuMs);
    historyHead = (historyHead + 1) % HISTORY;

    if (!ImGui::CollapsingHeader("Frame")) return;
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::Text("FPS (avg):        %.1f", io.Framerate);
    ImGui::Text("Frame time:       %.2f ms  (worst 1 s: %.2f ms)", frame.totalMs, frame.worstMs);
    ImGui::Text("Frames > %.0f ms:   %u", LONG_FRAME_MS, frame.longFrames);

    const double gpuShare = frame.totalMs > 0 ? gpuMs / frame.totalMs : 0;
    const bool gpuBound = gpuShare >= GPU_BOUND_THRESHOLD;
    ImGui::Text("GPU busy:         %.0f%% of frame", gpuShare * 100.0);
    ImGui::Text("Bottleneck:");
    ImGui::SameLine();
    ImGui::TextColored(gpuBound ? ImVec4(1.f, .45f, .35f, 1.f) : ImVec4(.45f, .85f, 1.f, 1.f), gpuBound ? "GPU" : "CPU");

    float maxMs = 1.f;
    for (int i = 0; i < HISTORY; ++i) maxMs = std::max({maxMs, cpuHistory[i], gpuHistory[i]});
    ImGui::PlotLines("CPU ms", cpuHistory.data(), HISTORY, historyHead, nullptr, 0.f, maxMs, ImVec2(0.f, 40.f));
    ImGui::PlotLines("GPU ms", gpuHistory.data(), HISTORY, historyHead, nullptr, 0.f, maxMs, ImVec2(0.f, 40.f));

    ImGui::SeparatorText("CPU (last frame)");
    TimeBar("Events/input", frame.eventsMs, frame.totalMs);
    TimeBar("Message drain", frame.drainMs, frame.totalMs);
    TimeBar("Mesh jobs/upload", frame.meshMs, frame.totalMs);
    TimeBar("Terrain draw", frame.renderMs, frame.totalMs);
    TimeBar("Debug UI", frame.debugUIMs, frame.totalMs);
    TimeBar("Swap", frame.swapMs, frame.totalMs);

    ImGui::SeparatorText("GPU (a few frames old)");
    TimeBar("Terrain", ctx.gpu.SectionMs(GpuSection::Terrain), frame.totalMs);
    TimeBar("Debug UI", ctx.gpu.SectionMs(GpuSection::DebugUI), frame.totalMs);
}

void vrtc::DebugUI::DrawToolSection(const DebugContext &ctx) const {
    if (!ImGui::CollapsingHeader("Edit tool", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ToolSettings &tool = ctx.tool;
    const ClientStats &c = ctx.client;

    ImGui::TextDisabled("LMB digs, RMB places. Tab mode, [ ] radius, - = reach, R block.");
    int mode = static_cast<int>(tool.mode);
    if (ImGui::Combo("Mode", &mode, "Whole blocks\0Micro-voxels (1/16 block)\0")) tool.mode = static_cast<EditMode>(mode);

    float reach = static_cast<float>(tool.reach);
    if (ImGui::SliderFloat("Reach (m)", &reach, 0.5f, static_cast<float>(TOOL_MAX_REACH), "%.1f")) tool.reach = reach;
    float radius = static_cast<float>(tool.radius);
    if (ImGui::SliderFloat("Radius (m)", &radius, 0.f, static_cast<float>(TOOL_MAX_RADIUS), "%.3f",
                           ImGuiSliderFlags_Logarithmic)) {
        tool.radius = radius;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("= %.1f micro-voxels", tool.radius * MICRO_RES);
    ImGui::SliderInt("Strength (hardness)", &tool.strength, 0, 15);

    // Block to place: every registered block except air.
    std::vector<const char *> names;
    for (size_t id = 1; id < ctx.blocks.Count(); ++id) names.push_back(ctx.blocks.Get(static_cast<BlockId>(id)).name.c_str());
    int blockIndex = std::clamp(static_cast<int>(tool.block) - 1, 0, static_cast<int>(names.size()) - 1);
    if (!names.empty() && ImGui::Combo("Place block", &blockIndex, names.data(), static_cast<int>(names.size()))) {
        tool.block = static_cast<BlockId>(blockIndex + 1);
    }
    if (ImGui::Button("Hand (0.2 m micro)")) {
        tool.mode = EditMode::Micro;
        tool.radius = 0.2;
    }
    ImGui::SameLine();
    if (ImGui::Button("Pick (0.5 m micro)")) {
        tool.mode = EditMode::Micro;
        tool.radius = 0.5;
    }
    ImGui::SameLine();
    if (ImGui::Button("Bomb (3 m micro)")) {
        tool.mode = EditMode::Micro;
        tool.radius = 3.0;
    }
    ImGui::SameLine();
    if (ImGui::Button("One block")) {
        tool.mode = EditMode::WholeBlocks;
        tool.radius = 0.0;
    }

    ImGui::SeparatorText("Light");
    LightSettings &light = ctx.light;
    ImGui::SliderFloat("Sun elevation", &light.sunElevation, -20.f, 90.f, "%.0f deg");
    ImGui::SliderFloat("Sun azimuth", &light.sunAzimuth, 0.f, 360.f, "%.0f deg");
    ImGui::Checkbox("Day/night cycle", &light.cycle);
    ImGui::SameLine();
    ImGui::Checkbox("Fog", &light.fog);

    ImGui::SeparatorText("Target");
    if (c.targetHit) {
        ImGui::Text("Block:            %s at %d, %d, %d   %.2f m away", ctx.blocks.Get(c.targetId).name.c_str(),
                    c.targetBlock.x, c.targetBlock.y, c.targetBlock.z, c.targetDistance);
        ImGui::Text("Face:             %s", c.targetFace >= 0 ? FACE_NAMES[c.targetFace] : "(inside)");
        if (c.targetDamaged) ImGui::Text("Micro-voxel:      %d, %d, %d of a damaged block", c.targetMicro.x, c.targetMicro.y, c.targetMicro.z);
        else ImGui::Text("Micro-voxel:      whole block");
    } else {
        ImGui::TextDisabled("nothing within reach");
    }
    ImGui::Text("Edits sent:       %llu   deltas applied: %llu", static_cast<unsigned long long>(c.editsSent),
                static_cast<unsigned long long>(c.deltasApplied));
    if (const ServerStats *s = ctx.server) {
        ImGui::Text("Server edits:     %llu applied, %llu rejected, last %.3f ms",
                    static_cast<unsigned long long>(s->edits.load()), static_cast<unsigned long long>(s->editsRejected.load()),
                    s->lastEditMs.load());
        ImGui::Text("Micro-voxels:     %.2f blocks carved, %.2f blocks filled, %u damaged blocks (%.1f MiB)",
                    static_cast<double>(s->bitsCarved.load()) / MicroBlock::BITS,
                    static_cast<double>(s->bitsFilled.load()) / MicroBlock::BITS, s->damagedBlocks.load(),
                    ToMiB(static_cast<size_t>(s->damagedBlocks.load()) * sizeof(MicroBlock)));
    }
    ImGui::Text("Last edit remesh: %.2f ms, %u quads, %u damaged blocks rebuilt / %u from cache", c.lastEditMeshMs,
                c.lastEditMeshQuads, c.lastEditMeshRebuilt, c.lastEditMeshReused);
    ImGui::Text("Last mesh job:    %.2f ms, %u block quads + %u micro quads", c.lastMeshMs, c.lastWholeQuads,
                c.lastMicroQuads);
}

void vrtc::DebugUI::DrawWorldSection(const DebugContext &ctx) const {
    if (!ImGui::CollapsingHeader("World and streaming")) return;
    const ClientStats &c = ctx.client;
    const glm::dvec3 p = ctx.camera.Position();
    const ChunkPos cp = ctx.camera.CameraChunk();
    ImGui::Text("Camera:           %.1f, %.1f, %.1f  (chunk %d, %d, %d)", p.x, p.y, p.z, cp.x, cp.y, cp.z);
    ImGui::Text("Yaw / pitch:      %.0f / %.0f   fly speed %.0f m/s", ctx.camera.Yaw(), ctx.camera.Pitch(), ctx.camera.Speed());
    ImGui::Text("Player:           %s (F3)   velocity %.1f, %.1f, %.1f m/s   %s", c.fly ? "flying" : "walking",
                c.playerVelocity.x, c.playerVelocity.y, c.playerVelocity.z, c.playerOnGround ? "on ground" : "in air");
    ImGui::Text("Entities:         %u seen%s", c.entities, "");
    ImGui::Text("Seed:             %llu%s", static_cast<unsigned long long>(c.seed), c.welcomed ? "" : " (not welcomed yet)");

    ImGui::SeparatorText("Client replica");
    ImGui::Text("Chunks held:      %u  (%.1f MiB)", c.chunksInReplica, ToMiB(c.replicaBytes));
    ImGui::Text("Need mesh:        %u  (%u ready, rest await neighbours)   in flight: %u   pending upload: %u",
                c.chunksDirty, c.chunksMeshable, c.meshJobsInFlight, c.pendingUploads);
    ImGui::Text("Uploads/frame:    %u   meshes built: %llu", c.uploadsThisFrame, static_cast<unsigned long long>(c.meshesBuilt));
    ImGui::Text("Mesh jobs:        last %.2f ms   worst 1 s %.2f ms   worker time %.0f ms/s (budget 3 ms per chunk)",
                c.lastMeshMs, c.worstMeshMs, c.meshMsAccum);
    ImGui::Text("Messages/frame:   %u   left in queue: %u", c.messagesThisFrame, c.messagesPending);
    ImGui::Text("Receive rate:     %.2f MiB/s   total %.1f MiB", c.bytesPerSecond / (1024.0 * 1024.0), ToMiB(c.bytesReceived));

    ImGui::SeparatorText("Jobs");
    ImGui::Text("Workers:          %u   queued: %u   running: %u   done: %llu", ctx.jobs.WorkerCount(), ctx.jobs.Queued(),
                ctx.jobs.Running(), static_cast<unsigned long long>(ctx.jobs.Completed()));

    ImGui::SeparatorText("Server");
    if (const ServerStats *s = ctx.server) {
        ImGui::Text("Entities:         %u   systems (movement, AI, physics) %.3f ms", s->entities.load(), s->systemsMs.load());
        ImGui::Text("Tick:             %.2f ms  (worst 1 s: %.2f ms, budget %.1f ms)", s->tickMs.load(), s->tickMsMax.load(),
                    TICK_SECONDS * 1000.0);
        ImGui::Text("Chunks loaded:    %u  (%.1f MiB)   generating: %u   queued: %u", s->chunksLoaded.load(),
                    ToMiB(s->worldBytes.load()), s->chunksLoading.load(), s->loadQueue.load());
        ImGui::Text("Generated:        %llu   unloaded: %llu", static_cast<unsigned long long>(s->chunksGenerated.load()),
                    static_cast<unsigned long long>(s->chunksUnloaded.load()));
        ImGui::Text("Sent:             %llu chunks, %.1f MiB   received %llu msgs",
                    static_cast<unsigned long long>(s->chunksSent.load()), ToMiB(s->bytesSent.load()),
                    static_cast<unsigned long long>(s->messagesReceived.load()));
    } else {
        ImGui::TextDisabled("remote server: no local counters");
    }
}

void vrtc::DebugUI::DrawRenderSection(const DebugContext &ctx) {
    if (!ImGui::CollapsingHeader("Rendering")) return;
    if (ImGui::Checkbox("Wireframe", &wireframe)) glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

    const ClientStats &c = ctx.client;
    ImGui::Text("Chunks drawn:     %u   culled: %u   on GPU: %u", c.chunksDrawn, c.chunksCulled, c.gpuChunks);
    ImGui::Text("Quads drawn:      %llu  (%.2f M triangles)", static_cast<unsigned long long>(c.quadsDrawn),
                static_cast<double>(c.quadsDrawn) * 2.0 / 1e6);
    ImGui::Text("Facing away:      %llu quads skipped   %u draw calls   %.2f M quads stored (12 B each)",
                static_cast<unsigned long long>(c.quadsSkipped), c.terrainDrawCalls, static_cast<double>(c.quadsStored) / 1e6);
    ImGui::Text("Entity draws:     %u  (%llu triangles)", ctx.entities.drawCalls,
                static_cast<unsigned long long>(ctx.entities.triangles));

    const GpuPipelineStats &pipe = ctx.gpu.PipelineStats();
    const double pixels = static_cast<double>(ctx.framebufferWidth) * ctx.framebufferHeight;
    ImGui::SeparatorText("Terrain pipeline (GPU)");
    ImGui::Text("Primitives in:    %llu   after clip: %llu", static_cast<unsigned long long>(pipe.primitivesSubmitted),
                static_cast<unsigned long long>(pipe.primitivesAfterClip));
    ImGui::Text("Vertex / frag:    %llu / %llu runs", static_cast<unsigned long long>(pipe.vertexInvocations),
                static_cast<unsigned long long>(pipe.fragmentInvocations));
    ImGui::Text("Overdraw:         %.2fx   at %d x %d", pixels > 0 ? pipe.fragmentInvocations / pixels : 0.0,
                ctx.framebufferWidth, ctx.framebufferHeight);
}

void vrtc::DebugUI::DrawMemorySection(const DebugContext &ctx) const {
    if (!ImGui::CollapsingHeader("Memory")) return;
    ImGui::Text("Chunk meshes:     %.2f MiB on GPU", ToMiB(ctx.client.gpuMeshBytes));
    ImGui::Text("Entity meshes:    %.2f MiB", ToMiB(ctx.entityMeshBytes));
    ImGui::Text("Textures (est):   %.2f MiB", ToMiB(ctx.textureBytes));
    if (hasNvMemInfo) {
        GLint totalKb = 0, freeKb = 0;
        glGetIntegerv(GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalKb);
        glGetIntegerv(GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &freeKb);
        ImGui::Text("VRAM used:        %d / %d MiB", (totalKb - freeKb) / 1024, totalKb / 1024);
    } else if (hasAtiMemInfo) {
        GLint info[4]{};
        glGetIntegerv(TEXTURE_FREE_MEMORY_ATI, info);
        ImGui::Text("VRAM free:        %d MiB", info[0] / 1024);
    } else {
        ImGui::TextDisabled("VRAM: driver doesn't report it");
    }
    ImGui::SeparatorText("Device");
    ImGui::TextWrapped("%s", glRenderer.c_str());
    ImGui::TextWrapped("%s", glVendor.c_str());
    ImGui::TextWrapped("GL %s", glVersion.c_str());
}

void vrtc::DebugUI::Render(const DebugContext &ctx) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    if (visible) {
        ImGui::SetNextWindowSize(ImVec2(560.f, 0.f), ImGuiCond_FirstUseEver);
        ImGui::Begin("vrtc debug  (F1 hides, click to fly, Esc releases)");
        ImGui::Text("%.0f FPS   %.2f ms   worst %.2f ms", ImGui::GetIO().Framerate, ctx.frame.totalMs, ctx.frame.worstMs);
    ImGui::Text("%.2f M quads drawn, %.2f M skipped facing away, %.1f MiB of meshes", ctx.client.quadsDrawn / 1e6,
                ctx.client.quadsSkipped / 1e6, ctx.client.gpuMeshBytes / (1024.0 * 1024.0));
        DrawToolSection(ctx);
        DrawFrameSection(ctx);
        DrawWorldSection(ctx);
        DrawRenderSection(ctx);
        DrawMemorySection(ctx);
        ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
