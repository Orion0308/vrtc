#include "ChunkSerial.h"

// Masks are run-length encoded over their 256 row words: (run length 1..255, word). Terrain slope masks and
// carved spheres are mostly full or empty rows, so a 512-byte mask usually packs into a few dozen bytes.
void vrtc::WriteMicro(ByteWriter &out, const MicroBlock &mask) {
    const uint16_t *words = mask.Words();
    size_t i = 0;
    while (i < MicroBlock::WORDS) {
        size_t run = 1;
        while (i + run < MicroBlock::WORDS && run < 255 && words[i + run] == words[i]) ++run;
        out.U8(static_cast<uint8_t>(run));
        out.U16(words[i]);
        i += run;
    }
}

bool vrtc::ReadMicro(ByteReader &in, MicroBlock &mask) {
    uint16_t *words = mask.Words();
    size_t i = 0;
    while (i < MicroBlock::WORDS) {
        const size_t run = in.U8();
        const uint16_t word = in.U16();
        if (!in.Ok() || run == 0 || i + run > MicroBlock::WORDS) return false;
        for (size_t k = 0; k < run; ++k) words[i + k] = word;
        i += run;
    }
    return true;
}

void vrtc::WriteChunkRecord(ByteWriter &out, const Chunk &chunk) {
    out.U8(CHUNK_FORMAT_VERSION);
    if (chunk.IsUniform()) {
        out.U8(CHUNK_MODE_UNIFORM);
        out.U16(chunk.UniformId());
        return;
    }
    out.U8(CHUNK_MODE_DIRECT);
    out.U16Array({chunk.Data(), static_cast<size_t>(CHUNK_VOLUME)});
    const std::span<const uint16_t> owners = chunk.MicroOwners();
    out.VarU32(static_cast<uint32_t>(owners.size()));
    for (size_t slot = 0; slot < owners.size(); ++slot) {
        out.U16(owners[slot]);
        WriteMicro(out, chunk.MicroAt(slot));
    }
    // Generated slope blocks: 18 bytes each instead of a mask.
    const std::span<const SlopeBlock> slopes = chunk.Slopes();
    out.VarU32(static_cast<uint32_t>(slopes.size()));
    for (const SlopeBlock &s: slopes) {
        out.U16(s.index);
        for (const int16_t c: s.corner) out.U16(static_cast<uint16_t>(c));
    }
}

bool vrtc::ReadChunkRecord(ByteReader &in, Chunk &chunk) {
    const uint8_t version = in.U8();
    if (!in.Ok() || version != CHUNK_FORMAT_VERSION) return false;
    const uint8_t mode = in.U8();
    switch (mode) {
        case CHUNK_MODE_UNIFORM: {
            const BlockId id = in.U16();
            if (!in.Ok()) return false;
            chunk.Fill(id);
            return true;
        }
        case CHUNK_MODE_DIRECT: {
            chunk.ClearMicro();
            BlockId *data = chunk.ExpandForWrite();
            if (!in.U16Array({data, static_cast<size_t>(CHUNK_VOLUME)})) return false;
            const uint32_t count = in.VarU32();
            if (!in.Ok() || count > CHUNK_VOLUME) return false;
            for (uint32_t i = 0; i < count; ++i) {
                const uint16_t index = in.U16();
                if (!in.Ok() || index >= CHUNK_VOLUME) return false;
                MicroBlock &mask = chunk.GetOrCreateMicro(index);
                if (!ReadMicro(in, mask)) return false;
            }
            const uint32_t slopeCount = in.VarU32();
            if (!in.Ok() || slopeCount > CHUNK_VOLUME) return false;
            std::vector<SlopeBlock> slopes(slopeCount);
            for (SlopeBlock &s: slopes) {
                s.index = in.U16();
                for (int16_t &c: s.corner) c = static_cast<int16_t>(in.U16());
            }
            if (!in.Ok()) return false;
            chunk.SetSlopes(std::move(slopes));
            return true;
        }
        default:
            return false;
    }
}
