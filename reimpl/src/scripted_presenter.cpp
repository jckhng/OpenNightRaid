#include "niteraid/scripted_presenter.hpp"

namespace niteraid {

bool update_scripted_presenter_actor(ScriptedPresenterActor& actor, std::uint16_t special_sprite_base)
{
    if (actor.update_callback == 0x554f) {
        actor.x_fixed += actor.vx_fixed;
        actor.y_fixed += actor.vy_fixed;
        return actor.counter-- == 0;
    }
    if (actor.update_callback == 0x558c) return actor.timer-- == 0;
    if (actor.update_callback == 0x55b7) {
        if (++actor.timer >= actor.counter) {
            actor.timer = 0;
            actor.x_fixed += actor.vx_fixed;
            ++actor.animation;
        }
        if (actor.x_fixed == actor.vy_fixed) {
            actor.vx_fixed = actor.vy_fixed = 0;
            return true;
        }
    }
    if (actor.update_callback == 0x3df8 || actor.update_callback == 0x3ef7) {
        if (actor.timer++ >= 4) {
            actor.timer = 0;
            if (++actor.counter == 3) {
                const bool opening = actor.update_callback == 0x3df8;
                actor.update_callback = 0;
                actor.draw_callback = opening ? 0x543a : 0;
                if (opening) actor.sprite = special_sprite_base + 2;
                return true;
            }
        }
    }
    return false;
}

}  // namespace niteraid
