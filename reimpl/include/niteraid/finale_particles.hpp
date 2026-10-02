#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace niteraid {

enum class FinaleParticleKind { Star, Spark };

struct FinaleParticleActor {
    bool active = false;
    FinaleParticleKind kind = FinaleParticleKind::Star;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t vx = 0;
    std::int32_t vy = 0;
    int lifetime = 0;
    int fade = 0;
    std::uint8_t base_color = 0;
    std::uint16_t star_index = 0;
};

struct FinaleParticleFrame {
    std::array<FinaleParticleActor, 200> actors {};
    std::uint32_t random_seed = 0;
    std::uint32_t spawned = 0;
    std::array<std::uint32_t, 60> burst_ticks {};
    int free_slot_hint = 1;
    std::size_t highwater = 1;
};

// Tick zero is the first star's birth, after the owner's initial empty draw.
[[nodiscard]] FinaleParticleFrame original_finale_particle_frame(
    std::uint32_t seed, std::uint32_t target_tick);

}  // namespace niteraid
