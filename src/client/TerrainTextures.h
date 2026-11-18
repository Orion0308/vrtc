#pragma once
// Builds the terrain texture array and the mesher's side tables from the block registry.
// M2 generates placeholder tiles by texture name; real images load through TextureManager::LoadLayer later.
#include "BlockRegistry.h"
#include "Mesher.h"
#include "Texture.h"

namespace vrtc {
    inline constexpr int TERRAIN_TILE_SIZE = 16;

    TextureHandle BuildTerrainTextures(TextureManager &textures, const BlockRegistry &blocks, MeshTables &tables);
}
