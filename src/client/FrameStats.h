#pragma once
#include <cstdint>

namespace vrtc {
    // CPU wall time of each phase of the client frame loop (section 11), in milliseconds, for the frame just done.
    struct FrameStats {
        double eventsMs = 0; // poll events, input, camera, send PlayerInput
        double drainMs = 0; // server messages applied to the replica
        double meshMs = 0; // mesh job hand-off, result collection and GPU uploads
        double renderMs = 0; // culling and terrain draw calls, not GPU time
        double debugUIMs = 0;
        double swapMs = 0; // blocks here when the GPU is behind, since vsync is off
        double totalMs = 0;
        double worstMs = 0; // longest frame in the last second
        uint32_t longFrames = 0; // frames over LONG_FRAME_MS since start
    };
}
