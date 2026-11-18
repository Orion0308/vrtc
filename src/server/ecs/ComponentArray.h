#pragma once
// Sparse-set component pools (section 12). Fixed from the old code: Get checks liveness and the handle's
// generation, Remove on a missing component is a no-op, TryGet and Has exist.
#include <cstdint>
#include <utility>
#include <vector>

#include "Check.h"
#include "Entity.h"

namespace vrtc {
    class IComponentArray {
    public:
        virtual ~IComponentArray() = default;

        virtual void Remove(Entity entity) = 0;

        [[nodiscard]] virtual size_t Size() const = 0;
    };

    template<typename T>
    class ComponentArray final : public IComponentArray {
        static constexpr uint32_t EMPTY = UINT32_MAX;
        std::vector<T> components;
        std::vector<uint32_t> sparse; // entity id -> dense index, EMPTY if none
        std::vector<Entity> owners; // dense index -> owning entity (with generation)

        [[nodiscard]] uint32_t IndexOf(Entity entity) const {
            if (entity.id >= sparse.size()) return EMPTY;
            const uint32_t index = sparse[entity.id];
            if (index == EMPTY || owners[index] != entity) return EMPTY; // missing or stale generation
            return index;
        }

    public:
        T &Add(Entity entity, T component) {
            VRTC_CHECK(IndexOf(entity) == EMPTY, "entity already has this component");
            if (entity.id >= sparse.size()) sparse.resize(entity.id + 1, EMPTY);
            sparse[entity.id] = static_cast<uint32_t>(components.size());
            components.push_back(std::move(component));
            owners.push_back(entity);
            return components.back();
        }

        // No-op when the entity has no component here.
        void Remove(Entity entity) override {
            const uint32_t index = IndexOf(entity);
            if (index == EMPTY) return;
            // Swap and pop so the dense arrays stay packed.
            const Entity last = owners.back();
            if (last != entity) {
                components[index] = std::move(components.back());
                owners[index] = last;
                sparse[last.id] = index;
            }
            sparse[entity.id] = EMPTY;
            components.pop_back();
            owners.pop_back();
        }

        [[nodiscard]] bool Has(Entity entity) const { return IndexOf(entity) != EMPTY; }

        [[nodiscard]] T *TryGet(Entity entity) {
            const uint32_t index = IndexOf(entity);
            return index == EMPTY ? nullptr : &components[index];
        }

        [[nodiscard]] T &Get(Entity entity) {
            const uint32_t index = IndexOf(entity);
            VRTC_ASSERT(index != EMPTY, "entity has no component of this type (or the handle is stale)");
            return components[index];
        }

        [[nodiscard]] size_t Size() const override { return components.size(); }

        [[nodiscard]] std::vector<T> &Dense() { return components; }

        [[nodiscard]] const std::vector<Entity> &Owners() const { return owners; }
    };
}
