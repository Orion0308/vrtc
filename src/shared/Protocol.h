#pragma once
// The message protocol (section 11). Hard rule 2: server and client talk only through these, even in single-player.
// Each message is one byte of type followed by little-endian fields.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <glm/vec3.hpp>

#include "BlockRegistry.h"
#include "Coords.h"
#include "MicroBlock.h"
#include "Serialise.h"

namespace vrtc {
    enum class MsgType : uint8_t {
        // client to server
        Join = 1,
        PlayerInput,
        Dig,
        Place,
        Interact,
        InventoryAction,
        // server to client
        Welcome = 64,
        ChunkData,
        ChunkUnload,
        BlockDelta,
        EntitySpawn,
        EntityDespawn,
        EntitySnapshot,
        PlayerState,
        InventoryUpdate,
        WorldEvent,
        GameState,
    };

    enum class Channel : uint8_t { Reliable, Unreliable };

    // How an edit shape is applied (DECISIONS.md: the test tool's modes until tools come from item data in M7).
    enum class EditMode : uint8_t {
        WholeBlocks = 0, // every block whose centre is inside the shape
        Micro = 1, // exact micro-voxels (Carve / Fill)
    };

    struct JoinMsg {
        uint32_t protocolVersion = 0;
        std::string playerName;
    };

    // M2: the fly camera's position rides along so the server can place the player's ticket (DECISIONS.md).
    // M3 replaces it with move axes consumed by shared/PlayerMovement.
    struct PlayerInputMsg {
        uint32_t sequence = 0;
        glm::dvec3 position{}; // used only in fly mode, where the client moves freely
        float yaw = 0, pitch = 0;
        uint8_t buttons = 0; // INPUT_* bits
        float moveForward = 0, moveRight = 0; // -1..1, relative to yaw
    };

    inline constexpr uint8_t INPUT_JUMP = 1, INPUT_SPRINT = 2, INPUT_FLY = 4;

    // Server to the owning client, every tick: where the server's physics put the player.
    struct PlayerStateMsg {
        uint32_t lastSequence = 0;
        glm::dvec3 position{}; // centre of the player's box
        glm::dvec3 velocity{};
        bool onGround = false;
    };

    struct EntitySpawnMsg {
        uint32_t id = 0;
        uint16_t visual = 0; // VISUAL_* : the only link from a server entity to how it is drawn
        glm::dvec3 position{};
        glm::vec3 halfExtents{0.5f};
    };

    struct EntityDespawnMsg {
        uint32_t id = 0;
    };

    struct EntityState {
        uint32_t id = 0;
        glm::dvec3 position{};
        float yaw = 0;
    };

    struct EntitySnapshotMsg {
        uint32_t tick = 0;
        std::vector<EntityState> entities;
    };

    inline constexpr uint16_t VISUAL_PLAYER = 1, VISUAL_ENEMY = 2;

    // Dig: remove material in a sphere around the hit point. The server validates reach, radius and cooldown.
    struct DigMsg {
        glm::dvec3 point{}; // sphere centre, world metres
        float radius = 0;
        uint8_t strength = 0; // hardness tiers this tool can cut, 0 to 15
        EditMode mode = EditMode::Micro;
    };

    // Place: add material in a sphere around the hit point.
    struct PlaceMsg {
        glm::dvec3 point{};
        float radius = 0;
        EditMode mode = EditMode::Micro;
        BlockId block = BLOCK_AIR;
    };

    struct WelcomeMsg {
        uint32_t playerEntity = 0;
        uint64_t seed = 0;
        uint8_t chunkBits = 0;
        uint8_t loadRadiusH = 0, loadRadiusV = 0;
    };

    struct ChunkDataMsg {
        ChunkPos pos{};
        uint32_t version = 0;
        std::span<const uint8_t> record; // points into the message buffer being decoded
    };

    struct ChunkUnloadMsg {
        ChunkPos pos{};
    };

    struct BlockDeltaEntry {
        uint16_t index = 0; // block index inside the chunk
        BlockId id = BLOCK_AIR;
        bool hasMask = false;
        MicroBlock mask{}; // valid when hasMask
    };

    struct BlockDeltaMsg {
        ChunkPos pos{};
        uint32_t version = 0; // the chunk's version after these changes
        std::vector<BlockDeltaEntry> entries;
    };

    // The type byte of a message, or nothing for an empty buffer.
    std::optional<MsgType> PeekType(std::span<const uint8_t> message);

    // Encode writes the type byte and the body. Decode expects the type byte to have been consumed already.
    void Encode(ByteWriter &out, const JoinMsg &m);

    void Encode(ByteWriter &out, const PlayerInputMsg &m);

    void Encode(ByteWriter &out, const DigMsg &m);

    void Encode(ByteWriter &out, const PlaceMsg &m);

    void Encode(ByteWriter &out, const WelcomeMsg &m);

    void Encode(ByteWriter &out, const ChunkDataMsg &m);

    void Encode(ByteWriter &out, const ChunkUnloadMsg &m);

    void Encode(ByteWriter &out, const BlockDeltaMsg &m);

    void Encode(ByteWriter &out, const PlayerStateMsg &m);

    void Encode(ByteWriter &out, const EntitySpawnMsg &m);

    void Encode(ByteWriter &out, const EntityDespawnMsg &m);

    void Encode(ByteWriter &out, const EntitySnapshotMsg &m);

    bool Decode(ByteReader &in, JoinMsg &m);

    bool Decode(ByteReader &in, PlayerInputMsg &m);

    bool Decode(ByteReader &in, DigMsg &m);

    bool Decode(ByteReader &in, PlaceMsg &m);

    bool Decode(ByteReader &in, WelcomeMsg &m);

    bool Decode(ByteReader &in, ChunkDataMsg &m);

    bool Decode(ByteReader &in, ChunkUnloadMsg &m);

    bool Decode(ByteReader &in, BlockDeltaMsg &m);

    bool Decode(ByteReader &in, PlayerStateMsg &m);

    bool Decode(ByteReader &in, EntitySpawnMsg &m);

    bool Decode(ByteReader &in, EntityDespawnMsg &m);

    bool Decode(ByteReader &in, EntitySnapshotMsg &m);
}
