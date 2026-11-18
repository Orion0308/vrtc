#pragma once
// The sort-key renderer carried over from the old code (section 19). Kept for entities and debug drawing;
// terrain has its own path in ChunkRenderer. Uses an unpacked vertex (section 10) and cached uniform locations.
#include <cstdint>
#include <span>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Material.h"
#include "Texture.h"

namespace vrtc {
    using MeshHandle = uint32_t;

    struct EntityVertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
        float layer; // texture array layer
    };

    struct Mesh {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei indexCount = 0;
        GLenum primitive = GL_TRIANGLES;
        size_t bytes = 0;
    };

    struct DrawCommand {
        uint64_t sortKey;
        MeshHandle mesh;
        MaterialHandle material;
        glm::mat4 transform; // relative to the camera's chunk origin
    };

    // Material in the high bits so Flush groups by shader and texture first, then by mesh.
    inline uint64_t MakeSortKey(MaterialHandle material, MeshHandle mesh) {
        return (static_cast<uint64_t>(material) << 32) | mesh;
    }

    struct RenderStats {
        uint32_t drawCalls = 0;
        uint64_t triangles = 0;
        uint32_t shaderBinds = 0;
        uint32_t textureBinds = 0;
    };

    class Renderer {
        std::vector<Mesh> meshes;
        std::vector<DrawCommand> commands;
        RenderStats stats{};
        size_t meshBytes = 0;
        const MaterialManager &materials; // not owned, must outlive the renderer
        TextureManager &textures;

    public:
        Renderer(const MaterialManager &materials, TextureManager &textures);

        ~Renderer();

        Renderer(const Renderer &) = delete;

        Renderer &operator=(const Renderer &) = delete;

        MeshHandle CreateMesh(std::span<const EntityVertex> vertices, std::span<const uint32_t> indices,
                              GLenum primitive = GL_TRIANGLES);

        void Submit(const DrawCommand &command) { commands.push_back(command); }

        // Sorts and draws everything submitted since the last Flush, then empties the queue.
        void Flush(const glm::mat4 &viewProj);

        [[nodiscard]] const RenderStats &Stats() const { return stats; }

        [[nodiscard]] size_t MeshCount() const { return meshes.size(); }

        [[nodiscard]] size_t MeshBytes() const { return meshBytes; }
    };
}
