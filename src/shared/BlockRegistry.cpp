#include "BlockRegistry.h"

#include "Log.h"

vrtc::BlockRegistry::BlockRegistry() {
    BlockDef air;
    air.name = "air";
    air.solid = false;
    air.opaque = false;
    air.replaceable = true;
    Register(std::move(air));
}

vrtc::BlockId vrtc::BlockRegistry::Register(BlockDef def) {
    if (defs.size() >= 65535) log::Fatal("BlockRegistry: too many blocks");
    if (byName.contains(def.name)) log::Fatal("BlockRegistry: duplicate block name '{}'", def.name);
    const auto id = static_cast<BlockId>(defs.size());
    byName.emplace(def.name, id);
    opaqueSide.push_back(def.opaque ? 1 : 0);
    solidSide.push_back(def.solid ? 1 : 0);
    defs.push_back(std::move(def));
    return id;
}

vrtc::BlockId vrtc::BlockRegistry::Find(std::string_view name) const {
    const auto it = byName.find(std::string(name));
    return it == byName.end() ? BLOCK_AIR : it->second;
}

vrtc::BlockId vrtc::BlockRegistry::Require(std::string_view name) const {
    const auto it = byName.find(std::string(name));
    if (it == byName.end()) log::Fatal("BlockRegistry: required block '{}' is not defined", name);
    return it->second;
}

void vrtc::RegisterDefaultBlocks(BlockRegistry &registry) {
    auto all = [](const char *texture) {
        std::array<std::string, 6> faces;
        faces.fill(texture);
        return faces;
    };

    BlockDef stone;
    stone.name = "stone";
    stone.faceTextures = all("stone");
    stone.hardness = 3;
    stone.drop = "stone";
    registry.Register(std::move(stone));

    BlockDef dirt;
    dirt.name = "dirt";
    dirt.faceTextures = all("dirt");
    dirt.hardness = 1;
    dirt.drop = "dirt";
    registry.Register(std::move(dirt));

    BlockDef grass;
    grass.name = "grass";
    // Grass on every side: with micro-voxel slopes the surface is made of sixteenth-high risers, and a dirt side
    // tile on each of them would stripe every hillside brown.
    grass.faceTextures = all("grass_top");
    grass.faceTextures[5] = "dirt"; // -Y
    grass.hardness = 1;
    grass.drop = "dirt";
    registry.Register(std::move(grass));

    BlockDef bedrock;
    bedrock.name = "bedrock";
    bedrock.faceTextures = all("bedrock");
    bedrock.hardness = 15;
    bedrock.unbreakable = true;
    registry.Register(std::move(bedrock));
}
