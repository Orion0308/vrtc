#pragma once
// Entity handles (section 12): a 20-bit index and a 12-bit generation. Carried over from the old code with the
// listed bugs fixed: double free is rejected and a generation that would wrap retires the index instead.
#include <cstdint>
#include <vector>

#include "Check.h"

namespace vrtc {
    struct Entity {
        uint32_t id: 20 {0};
        uint32_t gen: 12 {0};

        friend bool operator==(const Entity &lhs, const Entity &rhs) {
            return lhs.id == rhs.id && lhs.gen == rhs.gen;
        }

        friend bool operator!=(const Entity &lhs, const Entity &rhs) { return !(lhs == rhs); }

        [[nodiscard]] bool IsNull() const { return gen == 0; }
    };

    inline constexpr uint32_t MAX_ENTITY_ID = (1u << 20) - 1;
    inline constexpr uint32_t MAX_GENERATION = (1u << 12) - 1;

    // Records the live generation of every index so stale handles can be detected.
    class EntityManager {
        std::vector<uint16_t> generations; // index -> live generation; 0 means never allocated or retired
        std::vector<uint32_t> freeIds;
        uint32_t nextId = 0;
        uint32_t aliveCount = 0;

    public:
        Entity Create() {
            Entity e;
            if (!freeIds.empty()) {
                e.id = freeIds.back();
                freeIds.pop_back();
                e.gen = generations[e.id]; // bumped at destroy time
            } else {
                VRTC_CHECK(nextId <= MAX_ENTITY_ID, "entity index overflow");
                e.id = nextId++;
                generations.push_back(1);
                e.gen = 1;
            }
            ++aliveCount;
            return e;
        }

        [[nodiscard]] bool IsAlive(Entity e) const {
            return e.gen != 0 && e.id < generations.size() && generations[e.id] == e.gen;
        }

        // False when the handle is stale or already destroyed.
        bool Destroy(Entity e) {
            if (!IsAlive(e)) return false;
            --aliveCount;
            if (generations[e.id] == MAX_GENERATION) {
                generations[e.id] = 0; // retire the index rather than let the generation wrap silently
                return true;
            }
            ++generations[e.id];
            freeIds.push_back(e.id);
            return true;
        }

        [[nodiscard]] uint32_t AliveCount() const { return aliveCount; }
    };
}
