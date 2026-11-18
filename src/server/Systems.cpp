#include "Systems.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "Collision.h"
#include "Components.h"
#include "Config.h"
#include "Shapes.h"

namespace {
    using namespace vrtc;

    // World access for collision. Looks each chunk up once while the queries stay inside it, and keeps the
    // expanded masks of generated slope blocks in a small direct-mapped cache, since a walking body tests the
    // same few blocks many times per tick. Unloaded space counts as solid so nothing falls out of the world.
    class CollisionQuery {
        struct CachedMask {
            BlockPos pos{};
            bool valid = false;
            MicroBlock mask;
        };

        World &world;
        const uint8_t *solid;
        ChunkPos cachedPos{};
        const Chunk *cached = nullptr;
        bool cachedValid = false;
        std::array<CachedMask, 64> masks{};

        const Chunk *ChunkAt(BlockPos pos) {
            const ChunkPos cp = ToChunkPos(pos);
            if (!cachedValid || !(cp == cachedPos)) {
                cachedPos = cp;
                cached = world.GetChunk(cp);
                cachedValid = true;
            }
            return cached;
        }

    public:
        CollisionQuery(World &world, const BlockRegistry &blocks) : world(world), solid(blocks.Solid().data()) {
        }

        static constexpr BlockId UNLOADED = 0xFFFF;

        BlockId Block(BlockPos pos) {
            if (pos.y < WORLD_MIN_BLOCK_Y) return UNLOADED; // a floor under the world
            if (pos.y > WORLD_MAX_BLOCK_Y) return BLOCK_AIR;
            const Chunk *chunk = ChunkAt(pos);
            if (!chunk) return UNLOADED;
            return chunk->Get(LocalCoord(pos.x), LocalCoord(pos.y), LocalCoord(pos.z));
        }

        [[nodiscard]] bool Solid(BlockId id) const { return id == UNLOADED || solid[id] != 0; }

        const MicroBlock *Mask(BlockPos pos) {
            const Chunk *chunk = ChunkAt(pos);
            if (!chunk) return nullptr;
            const uint16_t index = BlockIndex(LocalCoord(pos.x), LocalCoord(pos.y), LocalCoord(pos.z));
            if (const MicroBlock *mask = chunk->Micro(index)) return mask;
            const SlopeBlock *slope = chunk->Slope(index);
            if (!slope) return nullptr;
            const auto slot = static_cast<size_t>(static_cast<uint32_t>(pos.x * 73856093 ^ pos.y * 19349663 ^ pos.z * 83492791)) % masks.size();
            CachedMask &entry = masks[slot];
            if (!entry.valid || !(entry.pos == pos)) {
                ExpandSlope(slope->corner, entry.mask);
                entry.pos = pos;
                entry.valid = true;
            }
            return &entry.mask;
        }

        // Edits invalidate expanded masks.
        void Invalidate() {
            for (CachedMask &entry: masks) entry.valid = false;
            cachedValid = false;
        }
    };

    bool Overlaps(CollisionQuery &query, glm::dvec3 centre, glm::dvec3 half) {
        return BoxOverlapsSolid(query, centre - half, centre + half);
    }

    // Moves one body through the world for one tick. Collision is tested every PHYSICS_SUBSTEP metres per axis,
    // which is finer than a micro-voxel, so no speed lets a body skip through a thin wall.
    void MoveBody(SystemContext &ctx, CollisionQuery &query, Transform &transform, RigidBody &body,
                  const Collider &collider) {
        const glm::dvec3 half = collider.halfExtents;
        const bool wasOnGround = body.onGround;
        body.onGround = false;
        body.blockedHorizontally = false;

        const glm::dvec3 delta = body.velocity * ctx.dt;
        const double longest = std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)});
        const int steps = std::clamp(static_cast<int>(std::ceil(longest / PHYSICS_SUBSTEP)), 1, PHYSICS_MAX_SUBSTEPS);
        glm::dvec3 step = delta / static_cast<double>(steps);
        glm::dvec3 impact(0.0); // speed lost on each axis, for bounce and impact damage
        glm::dvec3 &pos = transform.position;

        for (int s = 0; s < steps; ++s) {
            for (const int axis: {0, 2, 1}) { // horizontal first, then vertical
                if (step[axis] == 0.0) continue;
                glm::dvec3 next = pos;
                next[axis] += step[axis];
                if (!Overlaps(query, next, half)) {
                    pos = next;
                    continue;
                }
                // Auto-step: a grounded body blocked sideways climbs a ledge up to stepHeight, one micro-voxel
                // at a time. Without it every 6.25 cm stair of a slope would stop a walking body.
                if (axis != 1 && (wasOnGround || body.onGround) && body.stepHeight > 0.0) {
                    bool climbed = false;
                    const int maxLift = static_cast<int>(body.stepHeight * MICRO_RES + 0.5);
                    for (int k = 1; k <= maxLift && !climbed; ++k) {
                        const double lift = static_cast<double>(k) / MICRO_RES;
                        glm::dvec3 raised = pos;
                        raised.y += lift;
                        if (Overlaps(query, raised, half)) break; // no headroom
                        glm::dvec3 forward = next;
                        forward.y += lift;
                        if (!Overlaps(query, forward, half)) {
                            pos = forward;
                            climbed = true;
                        }
                    }
                    if (climbed) continue;
                }
                // Blocked: stop on this axis, remember how hard it hit.
                impact[axis] = body.velocity[axis];
                if (axis == 1 && step[axis] < 0.0) body.onGround = true;
                if (axis != 1) body.blockedHorizontally = true;
                const double speed = std::abs(body.velocity[axis]);
                body.velocity[axis] = speed > BOUNCE_MIN_SPEED ? -body.velocity[axis] * body.restitution : 0.0;
                step[axis] = body.velocity[axis] * ctx.dt / static_cast<double>(steps);
            }
        }

        // Standing still on the ground does not move down, so probe for support explicitly.
        if (!body.onGround && body.velocity.y <= 0.0) {
            glm::dvec3 below = pos;
            below.y -= 0.5 / MICRO_RES;
            body.onGround = Overlaps(query, below, half);
        }

        // A hard, fast impact chips the terrain: kinetic energy sets the size, hardness what it can cut.
        const double impactSpeed = glm::length(impact);
        if (body.hardness > 0 && impactSpeed > IMPACT_CARVE_SPEED) {
            const double energy = 0.5 * body.mass * impactSpeed * impactSpeed; // joules
            const double radius = std::min(1.5, 0.12 * std::cbrt(energy / 100.0));
            const glm::dvec3 direction = impact / impactSpeed;
            const glm::dvec3 contact = pos + direction * half; // the face that hit
            ctx.edit.Carve(Sphere{contact, radius}, body.hardness);
            query.Invalidate();
        }
    }
}

void vrtc::PlayerMovementSystem(SystemContext &ctx) {
    const double jumpSpeed = std::sqrt(2.0 * GRAVITY * JUMP_HEIGHT);
    ctx.registry.ForEach<PlayerControl, Transform, RigidBody>(
        [&](Entity, PlayerControl &control, Transform &transform, RigidBody &body) {
            body.frozen = control.fly;
            if (control.fly) {
                body.velocity = glm::dvec3(0.0);
                return;
            }
            // Input axes are relative to where the player looks: yaw 0 faces north (-Z), positive turns east.
            const double yaw = glm::radians(static_cast<double>(transform.yaw));
            const glm::dvec3 forward(std::sin(yaw), 0.0, -std::cos(yaw));
            const glm::dvec3 right(std::cos(yaw), 0.0, std::sin(yaw));
            glm::dvec3 wish = forward * static_cast<double>(control.moveForward) + right * static_cast<double>(control.moveRight);
            const double length = glm::length(wish);
            if (length > 1.0) wish /= length;
            const glm::dvec3 target = wish * (control.sprint ? SPRINT_SPEED : WALK_SPEED);

            // Accelerate the horizontal velocity toward the wished one; much weaker control in the air.
            const double response = body.onGround ? GROUND_ACCEL : AIR_ACCEL;
            const glm::dvec3 horizontal(body.velocity.x, 0.0, body.velocity.z);
            body.acceleration += (target - horizontal) * response;

            if (control.jump && body.onGround) {
                body.velocity.y = jumpSpeed;
                body.onGround = false;
            }
        });
}

void vrtc::MobAISystem(SystemContext &ctx) {
    const double jumpSpeed = std::sqrt(2.0 * GRAVITY * JUMP_HEIGHT);
    ctx.registry.ForEach<MobAI, Transform, RigidBody>([&](Entity, MobAI &ai, Transform &transform, RigidBody &body) {
        // Pick the nearest player (re-evaluated every tick; there are few players).
        double best = 1e300;
        glm::dvec3 targetPosition{};
        ai.target = Entity{};
        ctx.registry.ForEach<PlayerControl, Transform>([&](Entity player, PlayerControl &, Transform &other) {
            const glm::dvec3 d = other.position - transform.position;
            const double distanceSq = glm::dot(d, d);
            if (distanceSq < best) {
                best = distanceSq;
                targetPosition = other.position;
                ai.target = player;
            }
        });
        if (ai.target.IsNull()) return;

        glm::dvec3 toTarget = targetPosition - transform.position;
        toTarget.y = 0.0;
        const double distance = glm::length(toTarget);
        glm::dvec3 target(0.0);
        if (distance > ENEMY_STOP_DISTANCE) { // hold position near the player instead of grinding into it
            target = toTarget / distance * ENEMY_SPEED;
            transform.yaw = static_cast<float>(glm::degrees(std::atan2(toTarget.x, -toTarget.z)));
        }
        const double response = body.onGround ? GROUND_ACCEL : AIR_ACCEL;
        const glm::dvec3 horizontal(body.velocity.x, 0.0, body.velocity.z);
        body.acceleration += (target - horizontal) * response;

        // A wall taller than the auto-step: hop over it.
        if (body.blockedHorizontally && body.onGround) {
            body.velocity.y = jumpSpeed;
            body.onGround = false;
        }
    });
}

void vrtc::PhysicsSystem(SystemContext &ctx) {
    CollisionQuery query(ctx.world, ctx.blocks);

    // 1. Integrate and collide with the world.
    ctx.registry.ForEach<RigidBody, Transform, Collider>(
        [&](Entity, RigidBody &body, Transform &transform, Collider &collider) {
            const glm::dvec3 requested = body.acceleration, force = body.force;
            body.acceleration = glm::dvec3(0.0);
            body.force = glm::dvec3(0.0);
            if (body.frozen) return;
            // Hold a body in place until the chunk it is in has loaded.
            if (!ctx.world.GetChunk(ToChunkPos(ToBlockPos(transform.position)))) return;

            // Semi-implicit Euler: a = F/m + requested + gravity, then v += a dt, then x += v dt (in MoveBody).
            glm::dvec3 acceleration = requested + force / body.mass;
            acceleration.y -= GRAVITY * body.gravityScale;
            body.velocity += acceleration * ctx.dt;
            body.velocity /= 1.0 + body.drag * ctx.dt;
            if (body.onGround) {
                const double keep = std::max(0.0, 1.0 - body.friction * ctx.dt);
                body.velocity.x *= keep;
                body.velocity.z *= keep;
            }
            MoveBody(ctx, query, transform, body, collider);
        });

    // 2. Body against body: separate overlapping boxes along the axis of least penetration, sharing the push by
    //    inverse mass, and exchange momentum along that axis. Few bodies for now, so all pairs are tested; the
    //    spatial hash from section 12 replaces this loop when hordes arrive.
    ComponentArray<RigidBody> &bodies = ctx.registry.Pool<RigidBody>();
    const std::vector<Entity> &owners = bodies.Owners();
    for (size_t i = 0; i < owners.size(); ++i) {
        for (size_t j = i + 1; j < owners.size(); ++j) {
            const Entity a = owners[i], b = owners[j];
            Transform *ta = ctx.registry.TryGet<Transform>(a), *tb = ctx.registry.TryGet<Transform>(b);
            const Collider *ca = ctx.registry.TryGet<Collider>(a), *cb = ctx.registry.TryGet<Collider>(b);
            if (!ta || !tb || !ca || !cb) continue;
            RigidBody &ba = bodies.Dense()[i], &bb = bodies.Dense()[j];
            if (ba.frozen || bb.frozen) continue;

            const glm::dvec3 d = tb->position - ta->position;
            const glm::dvec3 overlap = ca->halfExtents + cb->halfExtents - glm::abs(d);
            if (overlap.x <= 0.0 || overlap.y <= 0.0 || overlap.z <= 0.0) continue;
            int axis = 0;
            if (overlap.y < overlap[axis]) axis = 1;
            if (overlap.z < overlap[axis]) axis = 2;
            const double sign = d[axis] >= 0.0 ? 1.0 : -1.0;
            const double inverseA = 1.0 / ba.mass, inverseB = 1.0 / bb.mass, inverseSum = inverseA + inverseB;

            // Positional correction, skipped for a body it would push into terrain.
            glm::dvec3 moveA(0.0), moveB(0.0);
            moveA[axis] = -sign * overlap[axis] * inverseA / inverseSum;
            moveB[axis] = sign * overlap[axis] * inverseB / inverseSum;
            if (!Overlaps(query, ta->position + moveA, ca->halfExtents)) ta->position += moveA;
            if (!Overlaps(query, tb->position + moveB, cb->halfExtents)) tb->position += moveB;

            // Impulse along the contact axis, only if they are approaching.
            const double approach = (bb.velocity[axis] - ba.velocity[axis]) * sign;
            if (approach < 0.0) {
                const double restitution = std::max(ba.restitution, bb.restitution);
                const double impulse = -(1.0 + restitution) * approach / inverseSum;
                ba.velocity[axis] -= sign * impulse * inverseA;
                bb.velocity[axis] += sign * impulse * inverseB;
            }
            // Standing on another body counts as ground.
            if (axis == 1) (sign > 0.0 ? bb : ba).onGround = true;
        }
    }
}
