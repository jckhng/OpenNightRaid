#pragma once

#include "niteraid/game_types.hpp"
#include "niteraid/scripted_presenter.hpp"

namespace niteraid {

// Completed-draw state of 4ae6 -> 4513, with the Sound Blaster 40ce waits.
[[nodiscard]] ScriptedPresenterFrame original_finale_presenter_frame(const WorldState& world);

}  // namespace niteraid
