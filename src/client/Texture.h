#pragma once
// Texture arrays of equal-sized tiles (section 10): nearest up close, mipmaps at distance, repeat wrapping, sRGB.
// Section 19 fix: layer 0 of every array is a visible "missing texture" tile, so the 0 returned by a failed
// AddLayer or LoadLayer is obvious on screen.
#include <cstdint>
#include <span>
#include <vector>
#include <glad/glad.h>

namespace vrtc {
    using TextureHandle = uint32_t;
    inline constexpr TextureHandle NO_TEXTURE = ~0u;

    struct Texture {
        GLuint id = 0;
        int width = 0, height = 0; // size of every layer
        uint32_t capacity = 0; // layers allocated
        uint32_t layerCount = 0; // layers filled so far, including the missing tile
        bool mipsDirty = false;
    };

    class TextureManager {
        std::vector<Texture> textures;

    public:
        // Needs a current GL context, like everything below.
        ~TextureManager();

        TextureManager() = default;

        TextureManager(const TextureManager &) = delete;

        TextureManager &operator=(const TextureManager &) = delete;

        // Allocates an empty sRGB array of `capacity` layers and fills layer 0 with the missing-texture tile.
        TextureHandle CreateArray(int width, int height, uint32_t capacity = 256);

        // Appends a layer from tightly packed RGBA8 pixels (bottom row first). Returns 0 (missing) on failure.
        uint32_t AddLayer(TextureHandle handle, std::span<const uint8_t> rgba);

        // Appends a layer from an image file that matches the layer size. Returns 0 (missing) on failure.
        uint32_t LoadLayer(TextureHandle handle, const char *path);

        // Rebuilds mipmaps first if layers were added since the last Bind.
        void Bind(TextureHandle handle, GLuint unit = 0);

        [[nodiscard]] const Texture &Get(TextureHandle handle) const { return textures[handle]; }

        // Estimated VRAM of every array as allocated.
        [[nodiscard]] size_t GpuBytes() const;
    };
}
