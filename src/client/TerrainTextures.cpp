#include "TerrainTextures.h"

#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Log.h"

namespace {
    constexpr int T = vrtc::TERRAIN_TILE_SIZE;

    uint32_t Hash(uint32_t x) {
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x;
    }

    struct Rgb {
        int r, g, b;
    };

    // Speckled flat colour: `base` plus a per-pixel jitter of up to `variance`.
    void Speckle(std::vector<uint8_t> &pixels, int y0, int y1, Rgb base, int variance, uint32_t salt) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < T; ++x) {
                const uint32_t h = Hash(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^ salt);
                const int jitter = static_cast<int>(h % (2 * variance + 1)) - variance;
                uint8_t *p = &pixels[(static_cast<size_t>(y) * T + x) * 4];
                p[0] = static_cast<uint8_t>(std::clamp(base.r + jitter, 0, 255));
                p[1] = static_cast<uint8_t>(std::clamp(base.g + jitter, 0, 255));
                p[2] = static_cast<uint8_t>(std::clamp(base.b + jitter, 0, 255));
                p[3] = 255;
            }
        }
    }

    // Placeholder art by texture name. Rows are bottom-first, matching what TextureManager expects.
    std::optional<std::vector<uint8_t>> MakeTile(const std::string &name) {
        std::vector<uint8_t> pixels(static_cast<size_t>(T) * T * 4);
        if (name == "stone") {
            Speckle(pixels, 0, T, {128, 128, 132}, 14, 1);
        } else if (name == "dirt") {
            Speckle(pixels, 0, T, {121, 85, 58}, 12, 2);
        } else if (name == "grass_top") {
            Speckle(pixels, 0, T, {92, 160, 64}, 14, 3);
        } else if (name == "grass_side") {
            Speckle(pixels, 0, T - 4, {121, 85, 58}, 12, 2);
            Speckle(pixels, T - 4, T, {92, 160, 64}, 14, 3);
        } else if (name == "bedrock") {
            Speckle(pixels, 0, T, {58, 58, 62}, 20, 4);
        } else {
            return std::nullopt;
        }
        return pixels;
    }
}

vrtc::TextureHandle vrtc::BuildTerrainTextures(TextureManager &textures, const BlockRegistry &blocks,
                                                MeshTables &tables) {
    const TextureHandle array = textures.CreateArray(T, T, 256);
    std::unordered_map<std::string, uint32_t> layers;

    const size_t count = blocks.Count();
    tables.opaque.assign(blocks.Opaque().begin(), blocks.Opaque().end());
    tables.visible.assign(blocks.Solid().begin(), blocks.Solid().end());
    tables.faceLayer.assign(count * 6, 0);

    for (size_t id = 1; id < count; ++id) {
        const BlockDef &def = blocks.Get(static_cast<BlockId>(id));
        for (int face = 0; face < 6; ++face) {
            const std::string &name = def.faceTextures[face];
            if (name.empty()) continue; // layer 0: missing
            auto it = layers.find(name);
            if (it == layers.end()) {
                uint32_t layer = 0;
                if (auto tile = MakeTile(name)) {
                    layer = textures.AddLayer(array, *tile);
                } else {
                    log::Warn("textures: no tile for '{}' (block '{}'), using the missing-texture layer", name,
                              def.name);
                }
                it = layers.emplace(name, layer).first;
            }
            tables.faceLayer[id * 6 + face] = static_cast<uint16_t>(it->second);
        }
    }
    log::Info("textures: {} terrain tiles for {} blocks", layers.size(), count - 1);
    return array;
}
