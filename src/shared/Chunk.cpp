#include "Chunk.h"

#include <algorithm>

void vrtc::Chunk::Compact() {
    if (!blocks || !micro.empty() || !slopes.empty()) return;
    const BlockId first = (*blocks)[0];
    const BlockId *data = blocks->data();
    for (int i = 1; i < CHUNK_VOLUME; ++i) {
        if (data[i] != first) return;
    }
    Fill(first);
}

vrtc::MicroBlock &vrtc::Chunk::GetOrCreateMicro(uint16_t index) {
    if (MicroBlock *existing = MicroMutable(index)) return *existing;
    if (Materialise(index)) return *MicroMutable(index);
    if (!blocks) Expand(); // a damaged chunk is never stored as uniform
    const auto slot = static_cast<uint16_t>(micro.size());
    micro.push_back(MicroBlock::Full());
    microOwner.push_back(index);
    microSlot.Set(index, slot);
    return micro.back();
}

void vrtc::Chunk::RemoveMicro(uint16_t index) {
    const uint16_t *found = microSlot.Find(index);
    if (!found) return;
    const uint16_t slot = *found;
    const uint16_t lastOwner = microOwner.back();
    if (lastOwner != index) {
        // Swap and pop: move the last mask into the hole and repoint its owner.
        micro[slot] = micro.back();
        microOwner[slot] = lastOwner;
        microSlot.Set(lastOwner, slot);
    }
    micro.pop_back();
    microOwner.pop_back();
    microSlot.Erase(index);
}

void vrtc::Chunk::ClearMicro() {
    slopes.clear();
    micro.clear();
    microOwner.clear();
    microSlot.Clear();
}

const vrtc::SlopeBlock *vrtc::Chunk::Slope(uint16_t index) const {
    const auto it = std::lower_bound(slopes.begin(), slopes.end(), index,
                                     [](const SlopeBlock &s, uint16_t i) { return s.index < i; });
    return it != slopes.end() && it->index == index ? &*it : nullptr;
}

void vrtc::Chunk::RemoveSlope(uint16_t index) {
    if (slopes.empty()) return;
    const auto it = std::lower_bound(slopes.begin(), slopes.end(), index,
                                     [](const SlopeBlock &s, uint16_t i) { return s.index < i; });
    if (it != slopes.end() && it->index == index) slopes.erase(it);
}

bool vrtc::Chunk::Materialise(uint16_t index) {
    const SlopeBlock *slope = Slope(index);
    if (!slope) return false;
    MicroBlock mask;
    ExpandSlope(slope->corner, mask);
    RemoveSlope(index);
    const auto slot = static_cast<uint16_t>(micro.size());
    micro.push_back(mask);
    microOwner.push_back(index);
    microSlot.Set(index, slot);
    return true;
}

bool vrtc::Chunk::GetMask(uint16_t index, MicroBlock &out) const {
    if (const MicroBlock *mask = Micro(index)) {
        out = *mask;
        return true;
    }
    if (const SlopeBlock *slope = Slope(index)) {
        ExpandSlope(slope->corner, out);
        return true;
    }
    return false;
}
