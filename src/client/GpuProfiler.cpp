#include "GpuProfiler.h"

namespace {
    // Same order as the fields of GpuPipelineStats.
    constexpr std::array<GLenum, 5> STAT_TARGETS = {
        GL_PRIMITIVES_SUBMITTED, GL_VERTEX_SHADER_INVOCATIONS, GL_CLIPPING_OUTPUT_PRIMITIVES,
        GL_FRAGMENT_SHADER_INVOCATIONS, GL_SAMPLES_PASSED,
    };
}

vrtc::GpuProfiler::GpuProfiler() {
    for (auto &frame: frames) {
        glGenQueries(MARKS, frame.timestamps.data());
        glGenQueries(STAT_QUERIES, frame.stats.data());
    }
}

vrtc::GpuProfiler::~GpuProfiler() {
    for (auto &frame: frames) {
        glDeleteQueries(MARKS, frame.timestamps.data());
        glDeleteQueries(STAT_QUERIES, frame.stats.data());
    }
}

void vrtc::GpuProfiler::Collect(FrameQueries &frame) {
    GLint available = 0;
    glGetQueryObjectiv(frame.timestamps[MARKS - 1], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) return; // keep the previous numbers rather than stall

    std::array<GLuint64, MARKS> ns{};
    for (int i = 0; i < MARKS; ++i) glGetQueryObjectui64v(frame.timestamps[i], GL_QUERY_RESULT, &ns[i]);
    for (int i = 0; i + 1 < MARKS; ++i) sectionMs[i] = static_cast<double>(ns[i + 1] - ns[i]) / 1e6;

    std::array<GLuint64, STAT_QUERIES> stats{};
    for (int i = 0; i < STAT_QUERIES; ++i) glGetQueryObjectui64v(frame.stats[i], GL_QUERY_RESULT, &stats[i]);
    pipelineStats = {stats[0], stats[1], stats[2], stats[3], stats[4]};
    frame.pending = false;
}

void vrtc::GpuProfiler::BeginFrame() {
    current = (current + 1) % FRAMES_IN_FLIGHT;
    FrameQueries &frame = frames[current];
    if (frame.pending) Collect(frame);
    nextMark = 0;
    glQueryCounter(frame.timestamps[nextMark++], GL_TIMESTAMP);
}

void vrtc::GpuProfiler::Mark(GpuSection section) {
    FrameQueries &frame = frames[current];
    if (nextMark != static_cast<int>(section) + 1) return; // out of order: skip rather than misattribute
    glQueryCounter(frame.timestamps[nextMark++], GL_TIMESTAMP);
    if (nextMark == MARKS) frame.pending = true;
}

void vrtc::GpuProfiler::BeginSceneStats() {
    for (int i = 0; i < STAT_QUERIES; ++i) glBeginQuery(STAT_TARGETS[i], frames[current].stats[i]);
}

void vrtc::GpuProfiler::EndSceneStats() {
    for (const GLenum target: STAT_TARGETS) glEndQuery(target);
}

double vrtc::GpuProfiler::TotalMs() const {
    double total = 0;
    for (const double ms: sectionMs) total += ms;
    return total;
}
