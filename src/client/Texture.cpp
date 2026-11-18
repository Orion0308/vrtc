#include "Texture.h"

#include <algorithm>
#include <bit>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "ChunkVertex.h"
#include "Log.h"

namespace {
    // Magenta and black squares: unmistakable.
    std::vector<uint8_t> MissingTile(int width, int height) {
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const bool a = ((x * 2 / std::max(width, 1)) + (y * 2 / std::max(height, 1))) % 2 == 0;
                uint8_t *p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
                p[0] = a ? 255 : 0;
                p[1] = 0;
                p[2] = a ? 255 : 0;
                p[3] = 255;
            }
        }
        return pixels;
    }
}

vrtc::TextureManager::~TextureManager() {
    for (auto &texture: textures) glDeleteTextures(1, &texture.id);
}

vrtc::TextureHandle vrtc::TextureManager::CreateArray(int width, int height, uint32_t capacity) {
    Texture texture;
    texture.width = width;
    texture.height = height;
    texture.capacity = std::clamp(capacity, 2u, MAX_TEXTURE_LAYERS);

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &texture.id);
    // Full mip chain down to 1x1. Mips are built per layer, so tiles never bleed into each other.
    const auto levels = static_cast<GLsizei>(std::bit_width(static_cast<uint32_t>(std::max(width, height))));
    glTextureStorage3D(texture.id, levels, GL_SRGB8_ALPHA8, width, height, static_cast<GLsizei>(texture.capacity));
    glTextureParameteri(texture.id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(texture.id, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTextureParameteri(texture.id, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(texture.id, GL_TEXTURE_WRAP_T, GL_REPEAT);

    textures.push_back(texture);
    const auto handle = static_cast<TextureHandle>(textures.size() - 1);
    AddLayer(handle, MissingTile(width, height)); // layer 0
    return handle;
}

uint32_t vrtc::TextureManager::AddLayer(TextureHandle handle, std::span<const uint8_t> rgba) {
    Texture &texture = textures[handle];
    if (texture.layerCount >= texture.capacity) {
        log::Error("texture: array full, all {} layers in use", texture.capacity);
        return 0;
    }
    if (rgba.size() != static_cast<size_t>(texture.width) * texture.height * 4) {
        log::Error("texture: layer size mismatch, expected {}x{} RGBA8", texture.width, texture.height);
        return 0;
    }
    const uint32_t layer = texture.layerCount++;
    glTextureSubImage3D(texture.id, 0, 0, 0, static_cast<GLint>(layer), texture.width, texture.height, 1, GL_RGBA,
                        GL_UNSIGNED_BYTE, rgba.data());
    texture.mipsDirty = true;
    return layer;
}

uint32_t vrtc::TextureManager::LoadLayer(TextureHandle handle, const char *path) {
    const Texture &texture = textures[handle];
    stbi_set_flip_vertically_on_load(true); // image files store the top row first, GL wants the bottom row first
    int width, height, channels;
    stbi_uc *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        log::Error("texture: cannot read '{}': {}", path, stbi_failure_reason());
        return 0;
    }
    uint32_t layer = 0;
    if (width != texture.width || height != texture.height) {
        log::Error("texture: '{}' is {}x{}, expected {}x{}", path, width, height, texture.width, texture.height);
    } else {
        layer = AddLayer(handle, {pixels, static_cast<size_t>(width) * height * 4});
    }
    stbi_image_free(pixels);
    return layer;
}

void vrtc::TextureManager::Bind(TextureHandle handle, GLuint unit) {
    Texture &texture = textures[handle];
    if (texture.mipsDirty) {
        glGenerateTextureMipmap(texture.id);
        texture.mipsDirty = false;
    }
    glBindTextureUnit(unit, texture.id);
}

size_t vrtc::TextureManager::GpuBytes() const {
    size_t bytes = 0;
    for (const Texture &texture: textures) {
        const size_t base = static_cast<size_t>(texture.width) * texture.height * 4 * texture.capacity;
        bytes += base + base / 3; // a full mip chain adds about a third
    }
    return bytes;
}
