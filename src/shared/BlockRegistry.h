#pragma once
// Block definitions (section 6). Hard rule 9: engine code never switches on a block ID; it asks the registry.
// Hot loops read the packed side arrays (Opaque(), Solid()) rather than the full definitions.
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vrtc {
    using BlockId = uint16_t;
    inline constexpr BlockId BLOCK_AIR = 0;

    struct BlockDef {
        std::string name;
        bool solid = true; // blocks movement
        bool opaque = true; // hides neighbours, blocks light
        std::array<std::string, 6> faceTextures{}; // texture names per face, in face order
        uint8_t hardness = 1; // 0 to 15
        uint8_t lightEmission = 0; // 0 to 15
        std::string drop; // item name, empty for none
        bool hasBlockEntity = false;
        bool replaceable = false;
        bool unbreakable = false;
    };

    class BlockRegistry {
        std::vector<BlockDef> defs;
        std::vector<uint8_t> opaqueSide;
        std::vector<uint8_t> solidSide;
        std::unordered_map<std::string, BlockId> byName;

    public:
        // Registers air as ID 0.
        BlockRegistry();

        BlockId Register(BlockDef def);

        [[nodiscard]] const BlockDef &Get(BlockId id) const { return defs[id]; }

        [[nodiscard]] size_t Count() const { return defs.size(); }

        // BLOCK_AIR when the name is unknown.
        [[nodiscard]] BlockId Find(std::string_view name) const;

        // Stops the program when the name is unknown (hard rule 11).
        [[nodiscard]] BlockId Require(std::string_view name) const;

        [[nodiscard]] std::span<const uint8_t> Opaque() const { return opaqueSide; }

        [[nodiscard]] std::span<const uint8_t> Solid() const { return solidSide; }
    };

    // M2 stand-in for assets/data/blocks.json, which arrives with the survival layer (M7). See DECISIONS.md.
    void RegisterDefaultBlocks(BlockRegistry &registry);
}
