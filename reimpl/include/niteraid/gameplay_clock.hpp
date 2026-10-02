#pragma once

#include <algorithm>
#include <cstdint>

#include "niteraid/game.hpp"

namespace niteraid {

// 32d5 measures from before update/draw, then supplies elapsed ticks to the
// NEXT frame's update loop (342c..3450). Drawing cost is not an added delay.
class GameplayClock {
public:
    void begin(std::uint64_t hardware_tick) { started_ = hardware_tick; }
    [[nodiscard]] std::uint64_t deadline() const { return started_ + 1; }
    [[nodiscard]] std::uint32_t update_budget() const { return budget_; }
    void finish(std::uint64_t hardware_tick) {
        const auto elapsed = hardware_tick > started_ ? hardware_tick - started_ : 1;
        budget_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(elapsed, 6));
    }
    void reset() { budget_ = 1; }

private:
    std::uint64_t started_ = 0;
    std::uint32_t budget_ = 1;
};

inline bool uses_gameplay_clock(const WorldState& world, const InputState& input = {})
{
    const bool restarts_level = input.control_modifier && input.alt_modifier &&
        input.level_warp_digit >= 0 && input.level_warp_digit <= 9;
    if (world.screen == Screen::Intermission && world.survivor_live) {
        const auto& scene = *world.survivor_live;
        // 3973 returns only after the current 32d5 page, not when an update
        // first sets state 1. A completed egress starts a different owner.
        const bool final_page_ready = scene.owner_return_ready &&
            scene.phase == SurvivorPhase::Egress && scene.leg == 4;
        return !final_page_ready && !input.escape &&
            (world.gameplay_state == GameplayState::Active ||
             world.gameplay_state == GameplayState::ScriptedSequence) &&
            world.confirmation_prompt == ConfirmationPromptAction::None;
    }
    return world.screen == Screen::Gameplay &&
           world.gameplay_state == GameplayState::Active && !world.player_dead &&
           !world.bunker_assault_active && !restarts_level &&
           world.confirmation_prompt == ConfirmationPromptAction::None;
}

// Further catch-up ticks retain held controls, not the same keyboard make event.
inline void consume_menu_key_events(InputState& input)
{
    input.menu_key_action = MenuKeyAction::None;
    input.menu_navigation_up_repeat = false;
    input.menu_navigation_down_repeat = false;
    input.start_repeat = false;
}

// Dispatch each tick's audio before Game::tick clears its transient events.
// A new presenter, level or modal owner must not consume a stale catch-up budget.
template <class AfterTick>
std::uint32_t advance_gameplay_batch(Game& game, const InputState& input,
                                    std::uint32_t budget, AfterTick after_tick)
{
    const auto level = game.world().current_level;
    const auto count = uses_gameplay_clock(game.world(), input) ? std::clamp(budget, 1u, 6u) : 1u;
    std::uint32_t advanced = 0;
    do {
        game.tick(input);
        after_tick(game.world());
        ++advanced;
    } while (advanced < count && uses_gameplay_clock(game.world(), input) &&
             game.world().current_level == level && !game.before_owner_page() &&
             !game.world().quit_requested);
    return advanced;
}

} // namespace niteraid
