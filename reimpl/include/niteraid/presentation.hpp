#pragma once

#include "niteraid/game_types.hpp"

namespace niteraid {

// 16c8:35d6 divides once; 16c8:554f accumulates the truncated 16.16 velocity.
[[nodiscard]] Vec2 original_presenter_route_position(Vec2 from, Vec2 to,
                                                     float updates,
                                                     std::uint32_t duration,
                                                     bool snap_to_pixels = false);

[[nodiscard]] WorldState make_presentation_world(const WorldState& previous,
                                                 const WorldState& current,
                                                 float alpha);

}  // namespace niteraid
