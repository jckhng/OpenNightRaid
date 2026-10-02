#include "niteraid/finale_particles.hpp"
#include "niteraid/game_internals.hpp"

#include <algorithm>
#include <cmath>

namespace niteraid {

FinaleParticleFrame original_finale_particle_frame(std::uint32_t seed, std::uint32_t target_tick)
{
    FinaleParticleFrame result {};
    result.random_seed = seed;
    const auto next = [&]() {
        result.random_seed = result.random_seed * 0x015a4e35u + 1u;
        return static_cast<std::uint16_t>((result.random_seed >> 16u) & 0x7fffu);
    };
    const auto bounded = [&](int limit) { return static_cast<int>(next()) * limit / 0x8000; };
    const auto signed_fixed = [&]() {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(next()) << 17u) / 0x7fff - 0x8000;
    };
    const auto allocate = [&]() -> FinaleParticleActor* {
        // 0040 consumes the most recently freed minimum slot before scanning.
        // That hint need not be the lowest hole already present in the table.
        if (result.free_slot_hint >= 0) {
            const auto slot = static_cast<std::size_t>(result.free_slot_hint);
            result.free_slot_hint = -1;
            result.highwater = std::max(result.highwater, slot + 1);
            return &result.actors[slot];
        }
        const auto found = std::find_if(result.actors.begin() + 1, result.actors.end(),
            [](const FinaleParticleActor& actor) { return !actor.active; });
        if (found == result.actors.end()) return nullptr;
        result.highwater = std::max(result.highwater,
            static_cast<std::size_t>(found - result.actors.begin()) + 1);
        return &*found;
    };
    const auto spawn_star = [&]() {
        auto* actor = allocate();
        if (!actor) return;
        const auto velocity = internals::kProjectileVelocityTable[32 + bounded(0x3f)];
        *actor = {true, FinaleParticleKind::Star, 160 << 16, 152 << 16,
                  static_cast<std::int32_t>(std::lround(velocity.x * 65536.0f)) * 3,
                  static_cast<std::int32_t>(std::lround(velocity.y * 65536.0f)) * 3,
                  0x20 + bounded(0x10)};
        actor->star_index = static_cast<std::uint16_t>(result.spawned);
        ++result.spawned;
    };
    const auto release = [&](FinaleParticleActor& actor) {
        actor.active = false;
        const auto slot = static_cast<std::size_t>(&actor - result.actors.data());
        if (result.free_slot_hint < 0 || slot < static_cast<std::size_t>(result.free_slot_hint)) {
            result.free_slot_hint = static_cast<int>(slot);
        }
        if (result.highwater == slot + 1) --result.highwater;
    };
    const auto burst = [&](const FinaleParticleActor& source) {
        const int count = 0x10 + bounded(0x10);
        constexpr std::array<std::uint8_t, 6> colors {{0x10, 0x20, 0x60, 0x90, 0xa0, 0xb0}};
        const auto color = colors[bounded(6)];
        for (int index = 0; index < count; ++index) {
            auto* actor = allocate();
            if (actor) {
                *actor = {true, FinaleParticleKind::Spark, source.x + (2 << 16), source.y + (2 << 16),
                          signed_fixed(), signed_fixed(), 0x20 + bounded(0x30), 0, color};
            }
        }
    };
    spawn_star();
    for (std::uint32_t tick = 1; tick <= target_tick; ++tick) {
        for (auto& actor : result.actors) {
            if (!actor.active) continue;
            if (actor.lifetime-- == 0) {
                if (actor.kind == FinaleParticleKind::Star) {
                    result.burst_ticks[actor.star_index] = tick;
                    const auto source = actor;
                    burst(source);
                }
                release(actor);
                continue;
            }
            if (actor.kind == FinaleParticleKind::Spark && actor.fade < 0x2f) ++actor.fade;
            actor.vy += 0x800;
            actor.x += actor.vx;
            actor.y += actor.vy;
            // 5378 destroys actors at the inclusive left/top or exclusive
            // right/bottom extended viewport bounds, even before lifetime ends.
            if (actor.x <= -100 * 65536 || actor.y <= -40 * 65536 ||
                actor.x >= 320 * 65536 || actor.y >= 200 * 65536) release(actor);
        }
        // The owning 4ae6 routine creates stars after the completed draw.
        // The first star is born after the loop's initial empty draw. Subsequent
        // 35-tick thresholds are relative to loop entry, one tick before birth.
        if (tick < target_tick && result.spawned < 60 && tick + 1 >= result.spawned * 0x23u) spawn_star();
    }
    return result;
}

}  // namespace niteraid
