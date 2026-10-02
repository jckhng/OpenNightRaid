#pragma once

#include "niteraid/game_types.hpp"

#include <optional>
#include <vector>

namespace niteraid {

namespace survivor_timing {
inline constexpr int kMovementCounter = 30;
inline constexpr std::array<Vec2, 5> kIngressTargets {{
    {240, 50}, {250, 70}, {230, 90}, {260, 110}, {220, 130},
}};
inline constexpr std::array<Vec2, 5> kEgressTargets {{
    {260, 110}, {230, 90}, {250, 70}, {240, 50}, {264, 34},
}};
inline constexpr std::array<std::uint32_t, 6> kAftermathWaits {{0, 140, 35, 70, 35, 35}};
}  // namespace survivor_timing

struct SurvivorTrooper {
    WorldPosition position {};
    int walk_period = 0;
    std::uint32_t walk_timer = 0;
    float walk_velocity = 0;
    float walk_target = 0;
};

struct SurvivorPresenterInput {
    std::vector<SurvivorTrooper> troopers {};
    std::uint32_t timer_origin = 0;
    std::uint32_t random_seed = 1;
    std::optional<std::uint16_t> forced_failure_mask {};
    std::vector<std::uint8_t> update_batches {};
};

struct SurvivorDrawCommand {
    std::uint16_t sprite = 0;
    Vec2 position {};
};

enum class SurvivorSoundAction { Bind, Stop, Play };

struct SurvivorSoundCommand {
    SurvivorSoundAction action = SurvivorSoundAction::Stop;
    std::uint16_t sound = 0;
};

struct SurvivorPresenterFrame {
    bool active = false;
    bool successful_pickup = false;
    std::uint32_t timer_tick = 0;
    std::uint32_t random_seed = 0;
    std::int32_t x_fixed = 0;
    std::int32_t y_fixed = 0;
    std::int32_t vx_fixed = 0;
    std::int32_t vy_fixed = 0;
    std::uint16_t sprite = 0;
    std::uint16_t update_callback = 0;
    std::uint16_t draw_callback = 0;
    int counter = 0;
    std::uint32_t animation_timer = 0;
    std::vector<SurvivorDrawCommand> draws {};
    std::vector<WorldPosition> retained_troopers {};
    std::vector<SurvivorSoundCommand> sounds {};
};

[[nodiscard]] std::uint16_t original_survivor_failure_mask(std::uint32_t seed, std::uint32_t count);
[[nodiscard]] std::uint32_t original_survivor_duration(std::uint32_t count, std::uint16_t failure_mask);

// Retire gameplay-owned landed survivors after the completed presenter has
// handed off to a milestone presenter. Their record contents remain available
// for any final render replay before this boundary.
void retire_survivor_handoff_objects(WorldState& world);

// One completed draw per step, with one update by default or an explicit DOS
// update batch. Enhanced display frames must not advance this clock or its RNG.
[[nodiscard]] SurvivorPresenterFrame original_survivor_presenter_frame(
    const SurvivorPresenterInput& input, std::uint32_t draw_count);
[[nodiscard]] SurvivorPresenterFrame original_survivor_presenter_frame(const WorldState& world);
[[nodiscard]] SurvivorPresenterFrame original_no_survivor_presenter_frame(const WorldState& world);

}  // namespace niteraid
