#pragma once

#include "niteraid/game_types.hpp"
#include "niteraid/scripted_presenter.hpp"

namespace niteraid {

struct PizzaPresenterFrame : ScriptedPresenterFrame {
    bool active = true;
    std::uint32_t simulation_updates = 0;
    std::uint32_t draw_index = 0;
    std::vector<std::uint16_t> sounds {};
    bool stop_vehicle_sound = false;
    bool start_vehicle_sound = false;
};

// 4163 emits two extra 2e5e draws without simulation updates. Live mode
// returns the last draw at the requested update; diagnostics can enumerate all.
[[nodiscard]] PizzaPresenterFrame original_pizza_presenter_frame(const WorldState& world);

}  // namespace niteraid
