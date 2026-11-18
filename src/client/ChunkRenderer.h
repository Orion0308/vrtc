#pragma once
// The terrain draw path (section 10), per-chunk form: one vertex array and buffer per chunk, drawn with the chunk
// origin as a uniform.
//
// Blocky chunks hold one 12-byte ChunkQuad per quad, drawn instanced as a 4-vertex strip. Quads are grouped by
// face direction, and a direction is skipped for a chunk when the camera cannot see faces pointing that way.
#include <array>
#include <cstdint>
#include <utility>
#include <vector>
#include <glad/glad.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "ClientWorld.h"
#include "Coords.h"
#include "Frustum.h"
#include "Mesher.h"
#include "Shader.h"
#include "Texture.h"

namespace vrtc {
    // What the terrain shader needs to light a frame. Engine derives it from LightSettings.
    struct TerrainLighting {
        glm::vec3 sunDir{0.f, 1.f, 0.f}; // unit vector toward the sun
        glm::vec3 sunColour{1.f}; // direct light
        glm::vec3 ambient{0.5f}; // sky ambient in fully open cells
        glm::vec3 fogColour{0.5f}; // also the clear colour
        float fogStart = 1e9f, fogEnd = 2e9f; // metres
    };

    class ChunkRenderer {
        struct Slot {
            GLuint vao = 0, vbo = 0;
            uint32_t quads = 0;
            std::array<uint32_t, 7> faceStart{};
            size_t bytes = 0;
            ChunkPos pos{};
            bool live = false;
        };

        Shader quadShader;
        GLint uQuadViewProj = -1, uQuadOrigin = -1;
        std::vector<Slot> slots;
        std::vector<uint32_t> freeSlots;
        std::vector<std::pair<float, uint32_t>> drawOrder; // scratch: (distance, slot)
        size_t gpuBytes = 0;
        uint32_t liveCount = 0;
        uint64_t storedQuads = 0;


    public:
        struct DrawResult {
            uint32_t drawn = 0, culled = 0;
            uint64_t quads = 0; // quads submitted after face-direction culling
            uint64_t quadsSkipped = 0; // quads in visible chunks skipped because they face away
            uint32_t drawCalls = 0;
        };

        ChunkRenderer();

        ~ChunkRenderer();

        ChunkRenderer(const ChunkRenderer &) = delete;

        ChunkRenderer &operator=(const ChunkRenderer &) = delete;

        // Uploads the mesh into `existing` (or a new slot). An empty mesh frees the slot. Returns the handle.
        uint32_t Upload(const MeshOutput &mesh, uint32_t existing);

        void Free(uint32_t handle);

        // Frustum-culls, sorts front to back and draws every live chunk relative to the camera's chunk.
        // `eye` is the camera position relative to the camera chunk's origin.
        DrawResult Draw(const glm::mat4 &viewProj, const Frustum &frustum, ChunkPos cameraChunk, glm::vec3 eye,
                        TextureManager &textures, TextureHandle terrain, const TerrainLighting &lighting);

        [[nodiscard]] size_t GpuBytes() const { return gpuBytes; }

        [[nodiscard]] uint32_t Count() const { return liveCount; }

        [[nodiscard]] uint64_t StoredQuads() const { return storedQuads; }
    };
}
