#pragma once
// GPU timer and pipeline-statistics queries kept several frames deep, so results are read only once the GPU has
// finished with them. Carried over unchanged in design from the old code.
#include <array>
#include <cstdint>
#include <glad/glad.h>

namespace vrtc {
    // GPU-side work between consecutive Mark() calls, in the order they are made each frame.
    enum class GpuSection : uint8_t { Terrain, DebugUI, Count };

    struct GpuPipelineStats {
        uint64_t primitivesSubmitted = 0;
        uint64_t vertexInvocations = 0;
        uint64_t primitivesAfterClip = 0;
        uint64_t fragmentInvocations = 0;
        uint64_t samplesPassed = 0;
    };

    class GpuProfiler {
        static constexpr int FRAMES_IN_FLIGHT = 4;
        static constexpr int MARKS = static_cast<int>(GpuSection::Count) + 1;
        static constexpr int STAT_QUERIES = 5;

        struct FrameQueries {
            std::array<GLuint, MARKS> timestamps{};
            std::array<GLuint, STAT_QUERIES> stats{};
            bool pending = false;
        };

        std::array<FrameQueries, FRAMES_IN_FLIGHT> frames{};
        int current = 0;
        int nextMark = 0;
        std::array<double, static_cast<size_t>(GpuSection::Count)> sectionMs{};
        GpuPipelineStats pipelineStats{};

        void Collect(FrameQueries &frame);

    public:
        GpuProfiler();

        ~GpuProfiler();

        GpuProfiler(const GpuProfiler &) = delete;

        GpuProfiler &operator=(const GpuProfiler &) = delete;

        // Reads back the oldest frame if the GPU is done with it, then records the start timestamp.
        void BeginFrame();

        // Ends `section`; sections must be marked in enum order.
        void Mark(GpuSection section);

        // Pipeline statistics bracket the terrain pass only.
        void BeginSceneStats();

        void EndSceneStats();

        [[nodiscard]] double SectionMs(GpuSection section) const { return sectionMs[static_cast<size_t>(section)]; }

        [[nodiscard]] double TotalMs() const;

        [[nodiscard]] const GpuPipelineStats &PipelineStats() const { return pipelineStats; }
    };
}
