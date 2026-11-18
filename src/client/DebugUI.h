#pragma once
// The debug panel (hard rule 10). Carried over from the old code and extended with streaming, job, chunk and
// edit-tool controls; the wireframe toggle is a member rather than a function-static.
#include <array>
#include <cstdint>
#include <string>

#include "BlockRegistry.h"
#include "Camera.h"
#include "ClientStats.h"
#include "DebugCounters.h"
#include "FrameStats.h"
#include "GpuProfiler.h"
#include "Jobs.h"
#include "Renderer.h"

struct GLFWwindow;

namespace vrtc {
    struct DebugContext {
        const FrameStats &frame;
        const GpuProfiler &gpu;
        const ClientStats &client;
        const ServerStats *server; // null for a remote server
        const JobSystem &jobs;
        const Camera &camera;
        const RenderStats &entities;
        const BlockRegistry &blocks;
        ToolSettings &tool; // edited in place by the panel
        LightSettings &light; // edited in place by the panel
        int framebufferWidth, framebufferHeight;
        size_t textureBytes;
        size_t entityMeshBytes;
    };

    class DebugUI {
        static constexpr int HISTORY = 240;

        std::array<float, HISTORY> cpuHistory{};
        std::array<float, HISTORY> gpuHistory{};
        int historyHead = 0;
        std::string glVendor, glRenderer, glVersion;
        bool hasNvMemInfo = false, hasAtiMemInfo = false;
        bool wireframe = false;
        bool visible = true;

        void DrawFrameSection(const DebugContext &ctx);

        void DrawToolSection(const DebugContext &ctx) const;

        void DrawWorldSection(const DebugContext &ctx) const;

        void DrawRenderSection(const DebugContext &ctx);

        void DrawMemorySection(const DebugContext &ctx) const;

    public:
        explicit DebugUI(GLFWwindow *window);

        ~DebugUI();

        DebugUI(const DebugUI &) = delete;

        DebugUI &operator=(const DebugUI &) = delete;

        void Toggle() { visible = !visible; }

        [[nodiscard]] bool Visible() const { return visible; }

        // True while ImGui wants the mouse, so clicks on the panel do not capture the cursor.
        [[nodiscard]] static bool WantsMouse();

        // Builds and draws the debug windows; call once per rendered frame, after the scene and before the swap.
        void Render(const DebugContext &ctx);
    };
}
