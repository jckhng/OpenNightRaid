#pragma once

#include "niteraid/game_types.hpp"
#include "niteraid/scripted_presenter.hpp"

namespace niteraid {

struct HelicopterPresenterFrame : ScriptedPresenterFrame {
    bool active = true;
    std::uint32_t simulation_updates = 0;
    std::uint32_t draw_index = 0;
    std::vector<std::uint16_t> sounds {};
    bool park_vehicle_sound = false;
    bool start_vehicle_sound = false;
};

// Original 45a8/4513 owner, including the wait callback retained across doors.
[[nodiscard]] HelicopterPresenterFrame original_helicopter_presenter_frame(const WorldState& world);

}  // namespace niteraid
