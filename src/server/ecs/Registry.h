#pragma once
// The entity registry (section 12). Destroy removes the entity from every pool that has it, then bumps the
// generation. Views and the deferred command buffer arrive with the mob milestone (M8).
#include <memory>
#include <vector>

#include "ComponentArray.h"
#include "Entity.h"

namespace vrtc {
    inline size_t NextComponentTypeId() {
        static size_t counter = 0;
        return counter++;
    }

    template<typename T>
    size_t ComponentTypeId() {
        static const size_t id = NextComponentTypeId();
        return id;
    }

    class Registry {
        EntityManager entities;
        std::vector<std::unique_ptr<IComponentArray>> pools;

    public:
        Entity Create() { return entities.Create(); }

        [[nodiscard]] bool IsAlive(Entity entity) const { return entities.IsAlive(entity); }

        void Destroy(Entity entity) {
            if (!entities.IsAlive(entity)) return;
            for (auto &pool: pools) {
                if (pool) pool->Remove(entity); // no-op where the entity has nothing
            }
            entities.Destroy(entity);
        }

        template<typename T>
        T &Add(Entity entity, T component) {
            VRTC_CHECK(entities.IsAlive(entity), "adding a component to a dead entity");
            return Pool<T>().Add(entity, std::move(component));
        }

        template<typename T>
        void Remove(Entity entity) { Pool<T>().Remove(entity); }

        template<typename T>
        [[nodiscard]] bool Has(Entity entity) { return Pool<T>().Has(entity); }

        template<typename T>
        [[nodiscard]] T *TryGet(Entity entity) { return Pool<T>().TryGet(entity); }

        template<typename T>
        [[nodiscard]] T &Get(Entity entity) {
            VRTC_ASSERT(entities.IsAlive(entity), "Get on a dead entity");
            return Pool<T>().Get(entity);
        }

        template<typename T>
        ComponentArray<T> &Pool() {
            const size_t id = ComponentTypeId<T>();
            if (id >= pools.size()) pools.resize(id + 1);
            if (!pools[id]) pools[id] = std::make_unique<ComponentArray<T>>();
            return static_cast<ComponentArray<T> &>(*pools[id]);
        }

        // Calls fn(Entity, First&, Rest&...) for every entity that has all the listed components. Iterates the
        // First pool; do not add or remove First components (or destroy entities) from inside fn.
        template<typename First, typename... Rest, typename Fn>
        void ForEach(Fn &&fn) {
            ComponentArray<First> &pool = Pool<First>();
            std::vector<First> &dense = pool.Dense();
            const std::vector<Entity> &owners = pool.Owners();
            for (size_t i = 0; i < dense.size(); ++i) {
                const Entity entity = owners[i];
                if constexpr (sizeof...(Rest) == 0) {
                    fn(entity, dense[i]);
                } else {
                    if ((Pool<Rest>().Has(entity) && ...)) fn(entity, dense[i], Pool<Rest>().Get(entity)...);
                }
            }
        }

        [[nodiscard]] uint32_t AliveCount() const { return entities.AliveCount(); }
    };
}
