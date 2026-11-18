#include "Protocol.h"

#include "ChunkSerial.h"

namespace {
    void WritePos(vrtc::ByteWriter &out, vrtc::ChunkPos p) {
        out.I32(p.x);
        out.I32(p.y);
        out.I32(p.z);
    }

    vrtc::ChunkPos ReadPos(vrtc::ByteReader &in) {
        vrtc::ChunkPos p{};
        p.x = in.I32();
        p.y = in.I32();
        p.z = in.I32();
        return p;
    }

    void WriteVec(vrtc::ByteWriter &out, glm::dvec3 v) {
        out.F64(v.x);
        out.F64(v.y);
        out.F64(v.z);
    }

    glm::dvec3 ReadVec(vrtc::ByteReader &in) {
        glm::dvec3 v;
        v.x = in.F64();
        v.y = in.F64();
        v.z = in.F64();
        return v;
    }
}

std::optional<vrtc::MsgType> vrtc::PeekType(std::span<const uint8_t> message) {
    if (message.empty()) return std::nullopt;
    return static_cast<MsgType>(message[0]);
}

void vrtc::Encode(ByteWriter &out, const JoinMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::Join));
    out.U32(m.protocolVersion);
    out.Str(m.playerName);
}

void vrtc::Encode(ByteWriter &out, const PlayerInputMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::PlayerInput));
    out.U32(m.sequence);
    WriteVec(out, m.position);
    out.F32(m.yaw);
    out.F32(m.pitch);
    out.U8(m.buttons);
    out.F32(m.moveForward);
    out.F32(m.moveRight);
}

void vrtc::Encode(ByteWriter &out, const DigMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::Dig));
    WriteVec(out, m.point);
    out.F32(m.radius);
    out.U8(m.strength);
    out.U8(static_cast<uint8_t>(m.mode));
}

void vrtc::Encode(ByteWriter &out, const PlaceMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::Place));
    WriteVec(out, m.point);
    out.F32(m.radius);
    out.U8(static_cast<uint8_t>(m.mode));
    out.U16(m.block);
}

void vrtc::Encode(ByteWriter &out, const WelcomeMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::Welcome));
    out.U32(m.playerEntity);
    out.U64(m.seed);
    out.U8(m.chunkBits);
    out.U8(m.loadRadiusH);
    out.U8(m.loadRadiusV);
}

void vrtc::Encode(ByteWriter &out, const ChunkDataMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::ChunkData));
    WritePos(out, m.pos);
    out.U32(m.version);
    out.VarU32(static_cast<uint32_t>(m.record.size()));
    out.Raw(m.record);
}

void vrtc::Encode(ByteWriter &out, const ChunkUnloadMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::ChunkUnload));
    WritePos(out, m.pos);
}

void vrtc::Encode(ByteWriter &out, const BlockDeltaMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::BlockDelta));
    WritePos(out, m.pos);
    out.U32(m.version);
    out.VarU32(static_cast<uint32_t>(m.entries.size()));
    for (const BlockDeltaEntry &e: m.entries) {
        out.U16(e.index);
        out.U16(e.id);
        out.U8(e.hasMask ? 1 : 0);
        if (e.hasMask) WriteMicro(out, e.mask);
    }
}

bool vrtc::Decode(ByteReader &in, JoinMsg &m) {
    m.protocolVersion = in.U32();
    m.playerName = in.Str();
    return in.Ok();
}

bool vrtc::Decode(ByteReader &in, PlayerInputMsg &m) {
    m.sequence = in.U32();
    m.position = ReadVec(in);
    m.yaw = in.F32();
    m.pitch = in.F32();
    m.buttons = in.U8();
    m.moveForward = in.F32();
    m.moveRight = in.F32();
    return in.Ok();
}

bool vrtc::Decode(ByteReader &in, DigMsg &m) {
    m.point = ReadVec(in);
    m.radius = in.F32();
    m.strength = in.U8();
    m.mode = static_cast<EditMode>(in.U8());
    return in.Ok();
}

bool vrtc::Decode(ByteReader &in, PlaceMsg &m) {
    m.point = ReadVec(in);
    m.radius = in.F32();
    m.mode = static_cast<EditMode>(in.U8());
    m.block = in.U16();
    return in.Ok();
}

bool vrtc::Decode(ByteReader &in, WelcomeMsg &m) {
    m.playerEntity = in.U32();
    m.seed = in.U64();
    m.chunkBits = in.U8();
    m.loadRadiusH = in.U8();
    m.loadRadiusV = in.U8();
    return in.Ok();
}

bool vrtc::Decode(ByteReader &in, ChunkDataMsg &m) {
    m.pos = ReadPos(in);
    m.version = in.U32();
    const uint32_t length = in.VarU32();
    m.record = in.Take(length);
    return in.Ok();
}

bool vrtc::Decode(ByteReader &in, ChunkUnloadMsg &m) {
    m.pos = ReadPos(in);
    return in.Ok();
}

bool vrtc::Decode(ByteReader &in, BlockDeltaMsg &m) {
    m.pos = ReadPos(in);
    m.version = in.U32();
    const uint32_t count = in.VarU32();
    if (!in.Ok() || count > CHUNK_VOLUME) return false;
    m.entries.resize(count);
    for (BlockDeltaEntry &e: m.entries) {
        e.index = in.U16();
        e.id = in.U16();
        e.hasMask = in.U8() != 0;
        if (e.hasMask && !ReadMicro(in, e.mask)) return false;
        if (e.index >= CHUNK_VOLUME) return false;
    }
    return in.Ok();
}

void vrtc::Encode(ByteWriter &out, const PlayerStateMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::PlayerState));
    out.U32(m.lastSequence);
    WriteVec(out, m.position);
    WriteVec(out, m.velocity);
    out.U8(m.onGround ? 1 : 0);
}

void vrtc::Encode(ByteWriter &out, const EntitySpawnMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::EntitySpawn));
    out.U32(m.id);
    out.U16(m.visual);
    WriteVec(out, m.position);
    out.F32(m.halfExtents.x);
    out.F32(m.halfExtents.y);
    out.F32(m.halfExtents.z);
}

void vrtc::Encode(ByteWriter &out, const EntityDespawnMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::EntityDespawn));
    out.U32(m.id);
}

void vrtc::Encode(ByteWriter &out, const EntitySnapshotMsg &m) {
    out.U8(static_cast<uint8_t>(MsgType::EntitySnapshot));
    out.U32(m.tick);
    out.VarU32(static_cast<uint32_t>(m.entities.size()));
    for (const EntityState &e: m.entities) {
        out.U32(e.id);
        WriteVec(out, e.position);
        out.F32(e.yaw);
    }
}

bool vrtc::Decode(ByteReader &in, PlayerStateMsg &m) {
    m.lastSequence = in.U32();
    m.position = ReadVec(in);
    m.velocity = ReadVec(in);
    m.onGround = in.U8() != 0;
    return in.Ok();
}

bool vrtc::Decode(ByteReader &in, EntitySpawnMsg &m) {
    m.id = in.U32();
    m.visual = in.U16();
    m.position = ReadVec(in);
    m.halfExtents.x = in.F32();
    m.halfExtents.y = in.F32();
    m.halfExtents.z = in.F32();
    return in.Ok();
}

bool vrtc::Decode(ByteReader &in, EntityDespawnMsg &m) {
    m.id = in.U32();
    return in.Ok();
}

bool vrtc::Decode(ByteReader &in, EntitySnapshotMsg &m) {
    m.tick = in.U32();
    const uint32_t count = in.VarU32();
    if (!in.Ok() || count > 100000) return false;
    m.entities.resize(count);
    for (EntityState &e: m.entities) {
        e.id = in.U32();
        e.position = ReadVec(in);
        e.yaw = in.F32();
    }
    return in.Ok();
}
