#pragma once
// The chunk record (section 8): the same bytes go into region files and into the ChunkData message.
// Format 4: version, storage mode (0 uniform, 16 direct IDs), the data, then for direct chunks a list of
// (block index, run-length encoded mask) for edited blocks and a list of 18-byte SlopeBlocks for generated slopes.
// Palette packing, block entities and LZ4 arrive in later milestones.
#include "Chunk.h"
#include "Serialise.h"

namespace vrtc {
    inline constexpr uint8_t CHUNK_FORMAT_VERSION = 4;
    inline constexpr uint8_t CHUNK_MODE_UNIFORM = 0;
    inline constexpr uint8_t CHUNK_MODE_DIRECT = 16;

    void WriteChunkRecord(ByteWriter &out, const Chunk &chunk);

    // Replaces the contents of `chunk`. False on a malformed record.
    bool ReadChunkRecord(ByteReader &in, Chunk &chunk);

    void WriteMicro(ByteWriter &out, const MicroBlock &mask);

    bool ReadMicro(ByteReader &in, MicroBlock &mask);
}
