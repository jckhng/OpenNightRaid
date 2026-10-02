#pragma once

#include <cstdint>
#include <vector>

namespace niteraid {

struct ScriptedPresenterActor {
    int slot = 0;
    int type_id = 0;
    std::int32_t x_fixed = 0, y_fixed = 0, vx_fixed = 0, vy_fixed = 0;
    std::uint16_t sprite = 0xffff, counter = 0, animation = 0;
    std::uint32_t timer = 0;
    std::uint16_t update_callback = 0, draw_callback = 0;
};

struct ScriptedPresenterDraw {
    std::uint16_t sprite = 0;
    int x = 0, y = 0;
};

struct ScriptedPresenterFrame {
    std::uint32_t hardware_tick = 0;
    std::vector<ScriptedPresenterActor> actors {};
    std::vector<ScriptedPresenterDraw> draws {};
    bool walking_sound = false;
};

// Shared original 554f/558c/55b7 motion and 3df8/3ef7 special-actor callbacks.
bool update_scripted_presenter_actor(ScriptedPresenterActor& actor,
                                    std::uint16_t special_sprite_base = 0x1e2);

}  // namespace niteraid
