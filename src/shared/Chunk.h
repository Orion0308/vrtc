#pragma once
// Chunk storage (section 6): a flat array of block IDs behind Get and Set (palette packing arrives in M4 without
// changing this interface) plus micro-voxel masks for damaged blocks only. A chunk whose blocks are all the same
// and undamaged stores no array at all.
//
// Micro-voxel invariants: a damaged block keeps its BlockId; a mask of all ones is never stored (the block is
// whole); a mask of all zeros is never stored (the block becomes air); slots are freed by swap-and-pop.
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "BlockRegistry.h"
#include "Coords.h"
#include "FlatMap.h"
#include "MicroBlock.h"

namespace vrtc {
    class Chunk {
        using Blocks = std::array<BlockId, CHUNK_VOLUME>;
        std::unique_ptr<Blocks> blocks; // null while uniform
        BlockId uniformId = BLOCK_AIR;

        // Damaged blocks only.
        std::vector<MicroBlock> micro;
        std::vector<uint16_t> microOwner; // slot -> block index, for swap-and-pop
        FlatMap<uint16_t, uint16_t> microSlot; // block index -> slot
        std::vector<SlopeBlock> slopes; // generated partial blocks, sorted by block index

        void Expand() {
            blocks = std::make_unique<Blocks>();
            blocks->fill(uniformId);
        }

    public:
        std::array<Chunk *, 6> neighbours{}; // maintained by the owning world on insert and remove
        uint32_t version = 0; // incremented on every edit
        uint8_t dirty = 0; // bit flags, see DIRTY_*

        static constexpr uint8_t DIRTY_SAVE = 1, DIRTY_NAV = 2, DIRTY_MODIFIED_SINCE_GEN = 4;

        Chunk() = default;

        explicit Chunk(BlockId uniform) : uniformId(uniform) {
        }

        Chunk(const Chunk &) = delete;

        Chunk &operator=(const Chunk &) = delete;

        // ---- Block IDs ----

        [[nodiscard]] bool IsUniform() const { return !blocks; }

        [[nodiscard]] BlockId UniformId() const { return uniformId; }

        [[nodiscard]] BlockId Get(int x, int y, int z) const {
            return blocks ? (*blocks)[BlockIndex(x, y, z)] : uniformId;
        }

        [[nodiscard]] BlockId GetIndex(uint16_t index) const { return blocks ? (*blocks)[index] : uniformId; }

        // Writers: the world edit API on the server (hard rule 4), delta application on the client, and
        // generation / deserialisation before the chunk is inserted into a world.
        void Set(int x, int y, int z, BlockId id) { SetIndex(BlockIndex(x, y, z), id); }

        // Changing a block's ID makes it whole again, so any mask is dropped.
        void SetIndex(uint16_t index, BlockId id) {
            if (!blocks) {
                if (id == uniformId) return;
                Expand();
            }
            (*blocks)[index] = id;
            RemoveMicro(index);
            RemoveSlope(index);
        }

        // Drops the array and every mask; every block becomes `id`.
        void Fill(BlockId id) {
            blocks.reset();
            uniformId = id;
            ClearMicro();
        }

        // Makes sure the array exists (filled with the uniform ID if it did not) and returns it for bulk writes.
        BlockId *ExpandForWrite() {
            if (!blocks) Expand();
            return blocks->data();
        }

        // nullptr while uniform.
        [[nodiscard]] const BlockId *Data() const { return blocks ? blocks->data() : nullptr; }

        // Collapses to uniform storage when every block is the same and nothing is damaged.
        void Compact();

        // ---- Micro-voxel masks ----

        // nullptr when the block is whole.
        [[nodiscard]] const MicroBlock *Micro(uint16_t index) const {
            const uint16_t *slot = microSlot.Find(index);
            return slot ? &micro[*slot] : nullptr;
        }

        [[nodiscard]] MicroBlock *MicroMutable(uint16_t index) {
            const uint16_t *slot = microSlot.Find(index);
            return slot ? &micro[*slot] : nullptr;
        }

        // Returns the block's mask, creating it as all ones (a whole block) if it had none.
        MicroBlock &GetOrCreateMicro(uint16_t index);

        void RemoveMicro(uint16_t index);

        void ClearMicro();

        [[nodiscard]] size_t MicroCount() const { return micro.size(); }

        // Block indices of every damaged block, in slot order; MicroAt(slot) is the matching mask.
        [[nodiscard]] std::span<const uint16_t> MicroOwners() const { return microOwner; }

        [[nodiscard]] const MicroBlock &MicroAt(size_t slot) const { return micro[slot]; }

        // ---- Generated slope blocks (compact, see SlopeBlock) ----

        // nullptr unless the block is an unedited generated slope.
        [[nodiscard]] const SlopeBlock *Slope(uint16_t index) const;

        [[nodiscard]] std::span<const SlopeBlock> Slopes() const { return slopes; }

        // Replaces all slope blocks; `sorted` must be ordered by block index. Used by generation and loading.
        void SetSlopes(std::vector<SlopeBlock> &&sorted) {
            if (!sorted.empty() && !blocks) Expand();
            slopes = std::move(sorted);
        }

        void RemoveSlope(uint16_t index);

        // Turns a slope block into an ordinary mask so it can be edited. True if it was a slope.
        bool Materialise(uint16_t index);

        // True when the block is partial for any reason: an edited mask or a generated slope.
        [[nodiscard]] bool IsDamaged(uint16_t index) const { return Micro(index) || Slope(index); }

        // Copies or expands the block mask into `out`. False (and `out` untouched) when the block is whole.
        bool GetMask(uint16_t index, MicroBlock &out) const;

        [[nodiscard]] size_t DamagedCount() const { return micro.size() + slopes.size(); }

        [[nodiscard]] size_t MemoryBytes() const {
            return sizeof(Chunk) + (blocks ? sizeof(Blocks) : 0) + micro.capacity() * sizeof(MicroBlock)
                   + microOwner.capacity() * sizeof(uint16_t) + microSlot.MemoryBytes()
                   + slopes.capacity() * sizeof(SlopeBlock);
        }
    };
}
