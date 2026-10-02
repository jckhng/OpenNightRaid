#include "niteraid/finale_presenter.hpp"
#include "niteraid/presenter_timing.hpp"

#include <algorithm>
#include <cmath>

namespace niteraid {

ScriptedPresenterFrame original_finale_presenter_frame(const WorldState& world)
{
    ScriptedPresenterFrame result {};
    const auto frame = world.finale_presenter_frame;
    result.hardware_tick = world.finale_presenter_timer_origin + frame + 1;
    if (frame < world.finale_hardware_tick_schedule.size()) {
        result.hardware_tick = world.finale_hardware_tick_schedule[frame];
    }
    const auto egress_start = presenter_timing::finale_egress_start(world);
    if (frame >= presenter_timing::finale_particle_start(world)) return result;

    ScriptedPresenterActor vehicle {};
    vehicle.slot = 1;
    vehicle.type_id = 14;
    vehicle.x_fixed = 195 * 65536;
    vehicle.y_fixed = 154 * 65536;
    vehicle.vx_fixed = -102400;
    vehicle.counter = 0xffff;
    vehicle.draw_callback = 0x4a5a;
    if (frame < 81) {
        vehicle.x_fixed = 320 * 65536 + vehicle.vx_fixed * static_cast<int>(frame + 1);
        vehicle.counter = static_cast<std::uint16_t>(79 - static_cast<int>(frame));
        vehicle.update_callback = 0x554f;
    } else if (frame >= egress_start) {
        vehicle.vx_fixed = (-61 - 195) * 65536 / 100;
        vehicle.x_fixed += vehicle.vx_fixed * static_cast<int>(frame - egress_start + 1);
        vehicle.counter = static_cast<std::uint16_t>(99 - static_cast<int>(frame - egress_start));
        vehicle.timer = 0xffffffff;
        vehicle.update_callback = 0x554f;
    } else if (frame >= 591) {
        if (audio_internals::composite_effects_enabled(world)) {
            vehicle.timer = static_cast<std::uint32_t>(34 - static_cast<int>((frame - 591) % 36));
        } else {
            const auto end = frame < 600 ? 599 : frame < 741 ? 740 : 776;
            vehicle.timer = static_cast<std::uint32_t>(end - static_cast<int>(frame) - 1);
        }
        vehicle.update_callback = 0x558c;
    }
    result.actors.push_back(vehicle);

    // Actor layers, not allocation order: door 1, trooper 2, vehicle 4.
    if (frame >= 81 && frame < 159) {
        ScriptedPresenterActor door {};
        door.slot = 2;
        door.type_id = 1;
        door.x_fixed = 157 * 65536;
        door.y_fixed = 172 * 65536;
        int sprite = 0x1e4;
        if (frame < 95) {
            door.counter = static_cast<std::uint16_t>((frame - 80) / 5);
            door.timer = (frame - 80) % 5;
            door.update_callback = 0x3df8;
            door.draw_callback = 0x3e52;
            sprite = 0x1e2 + door.counter;
        } else if (frame < 144) {
            door.counter = 3;
            door.sprite = 0x1e4;
            door.draw_callback = 0x543a;
        } else {
            door.counter = static_cast<std::uint16_t>((frame - 143) / 5);
            door.timer = (frame - 143) % 5;
            door.sprite = 0x1e4;
            if (frame < 158) {
                door.update_callback = 0x3ef7;
                door.draw_callback = 0x3f48;
            }
            sprite = 0x1e4 - door.counter;
        }
        result.actors.push_back(door);
        if (door.draw_callback) result.draws.push_back({static_cast<std::uint16_t>(sprite), 157, 172});
    }
    if (frame >= 96 && frame < 591) {
        ScriptedPresenterActor trooper {};
        trooper.slot = 3;
        trooper.type_id = 9;
        trooper.sprite = 0x2cd;
        trooper.counter = 8;
        trooper.x_fixed = 161 * 65536;
        trooper.y_fixed = 175 * 65536;
        trooper.animation = 6;
        trooper.draw_callback = 0x404a;
        if (frame < 144) {
            const auto elapsed = frame - 95;
            trooper.animation = static_cast<std::uint16_t>(elapsed / 8);
            trooper.x_fixed = (155 + trooper.animation) * 65536;
            trooper.y_fixed = 173 * 65536;
            trooper.timer = elapsed % 8;
            trooper.vx_fixed = frame == 143 ? 0 : 65536;
            trooper.vy_fixed = frame == 143 ? 0 : 161 * 65536;
            trooper.update_callback = 0x55b7;
            trooper.draw_callback = 0x40b2;
        } else if (frame >= 159) {
            const auto elapsed = frame - 158;
            trooper.animation += static_cast<std::uint16_t>(elapsed / 8);
            trooper.x_fixed += static_cast<int>(elapsed / 8) * 65536;
            trooper.timer = elapsed % 8;
            trooper.vx_fixed = frame == 590 ? 0 : 65536;
            trooper.vy_fixed = frame == 590 ? 0 : 215 * 65536;
            trooper.update_callback = 0x55b7;
        }
        result.actors.push_back(trooper);
        result.walking_sound = trooper.vx_fixed && trooper.animation % 4 == 0;
        result.draws.push_back({static_cast<std::uint16_t>(trooper.sprite + trooper.animation % 4),
                               trooper.x_fixed / 65536, trooper.y_fixed / 65536});
        // 40b2 calls 54a9 after the trooper, occluding only the doorway walk.
        if (trooper.draw_callback == 0x40b2) result.draws.push_back({0x1df, 148, 166});
    }
    const auto x = static_cast<int>(std::floor(static_cast<double>(vehicle.x_fixed) / 65536));
    result.draws.push_back({0x30b, x, 154});
    result.draws.push_back({static_cast<std::uint16_t>(0x30c + (result.hardware_tick / 15) % 6), x + 19, 156});
    if (world.female_finale_enabled) result.draws.push_back({0x312, x + 27, 158});
    return result;
}

}  // namespace niteraid
