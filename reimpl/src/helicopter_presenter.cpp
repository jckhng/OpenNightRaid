#include "niteraid/helicopter_presenter.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace niteraid {
namespace {

enum class Phase {
    Ingress, Descend, OpenBunkerOut, Leave, CloseBunkerOut, Approach,
    OpenOuthouseIn, CloseOuthouseIn, Inside, Flush, OpenOuthouseOut,
    CloseOuthouseOut, Return, OpenBunkerIn, Enter, CloseBunkerIn,
    Ascend, Egress, Done
};

}  // namespace

HelicopterPresenterFrame original_helicopter_presenter_frame(const WorldState& world)
{
    HelicopterPresenterFrame result {};
    std::array<ScriptedPresenterActor, 5> actors {};
    auto& vehicle = actors[1];
    auto& outhouse = actors[2];
    auto& door = actors[3];
    auto& trooper = actors[4];
    Phase phase = Phase::Ingress;
    bool enter_phase = true;
    std::uint16_t special_base = 0x1e2;
    for (std::uint32_t index = 0; phase != Phase::Done; ++index) {
        result.sounds.clear();
        result.park_vehicle_sound = result.start_vehicle_sound = false;
        const auto move = [&](int x, int y) {
            vehicle.vx_fixed = (x * 65536 - vehicle.x_fixed) / 80;
            vehicle.vy_fixed = (y * 65536 - vehicle.y_fixed) / 80;
            vehicle.counter = 80; vehicle.update_callback = 0x554f;
        };
        const auto walk = [&](int x) {
            trooper.update_callback = 0x55b7; trooper.counter = 8;
            trooper.vx_fixed = x * 65536 > trooper.x_fixed ? 65536 : -65536;
            trooper.vy_fixed = x * 65536;
        };
        const auto open = [&](std::uint16_t base, int x, int y) {
            special_base = base;
            door = {}; door.slot = 3; door.type_id = 1;
            door.x_fixed = x * 65536; door.y_fixed = y * 65536;
            door.update_callback = 0x3df8; door.draw_callback = 0x3e52;
            result.sounds.push_back(0x337);
        };
        const auto close = [&] {
            door.counter = 0; door.timer = 0;
            door.update_callback = 0x3ef7; door.draw_callback = 0x3f48;
            result.sounds.push_back(0x339);
        };
        if (enter_phase) {
            switch (phase) {
            case Phase::Ingress:
                vehicle.slot = 1; vehicle.type_id = 5;
                vehicle.x_fixed = 320 * 65536; vehicle.y_fixed = 34 * 65536;
                vehicle.draw_callback = 0x4400; move(195, 34);
                outhouse.slot = 2; outhouse.type_id = 1;
                outhouse.draw_callback = 0x4457; outhouse.sprite = 0x305; break;
            case Phase::Descend:
                vehicle.x_fixed = 195 * 65536; move(195, 137); break;
            case Phase::OpenBunkerOut:
                vehicle.y_fixed = 137 * 65536; vehicle.update_callback = 0;
                result.park_vehicle_sound = true; open(0x1e2, 157, 172); break;
            case Phase::Leave:
                trooper.slot = 4; trooper.type_id = 9; trooper.sprite = 0x2cd;
                trooper.x_fixed = 155 * 65536; trooper.y_fixed = 173 * 65536;
                trooper.draw_callback = 0x40b2; walk(161); break;
            case Phase::CloseBunkerOut:
                trooper.update_callback = 0; trooper.draw_callback = 0x404a;
                trooper.y_fixed += 2 * 65536; close(); break;
            case Phase::Approach: door = {}; walk(206); break;
            case Phase::OpenOuthouseIn:
                outhouse.sprite = 0xffff; trooper.update_callback = 0;
                open(0x305, 208, 171); break;
            case Phase::CloseOuthouseIn:
                special_base = 0x308; trooper.draw_callback = 0; close(); break;
            case Phase::Inside:
                door = {}; trooper.update_callback = 0x558c; trooper.timer = 280; break;
            case Phase::Flush:
                result.sounds.push_back(0x34d); trooper.timer = 140; break;
            case Phase::OpenOuthouseOut:
                // 558c is deliberately still installed: its unsigned timer
                // decrements through both door loops before the return walk.
                open(0x308, 208, 171); break;
            case Phase::CloseOuthouseOut:
                special_base = 0x305; trooper.draw_callback = 0x404a; close(); break;
            case Phase::Return:
                door = {}; outhouse.sprite = 0x305; trooper.sprite = 0x2d3;
                walk(161); break;
            case Phase::OpenBunkerIn:
                trooper.update_callback = 0; open(0x1e2, 157, 172); break;
            case Phase::Enter:
                trooper.y_fixed -= 2 * 65536; trooper.draw_callback = 0x40b2;
                walk(155); break;
            case Phase::CloseBunkerIn:
                trooper.update_callback = 0; close(); break;
            case Phase::Ascend:
                door = {}; trooper = {}; result.start_vehicle_sound = true;
                move(195, 34); break;
            case Phase::Egress:
                vehicle.y_fixed = 34 * 65536; move(-49, 34); break;
            case Phase::Done: break;
            }
            enter_phase = false;
        }
        bool complete = false;
        for (auto& actor : actors) if (actor.type_id) {
            complete = update_scripted_presenter_actor(actor, special_base) || complete;
        }
        result.simulation_updates = index + 1;
        result.draw_index = index;
        result.actors.clear(); result.draws.clear(); result.walking_sound = false;
        for (const auto& actor : actors) if (actor.type_id) result.actors.push_back(actor);
        const int x = static_cast<int>(std::floor(static_cast<double>(vehicle.x_fixed) / 65536));
        const int y = static_cast<int>(std::floor(static_cast<double>(vehicle.y_fixed) / 65536));
        result.draws.push_back({0x2e1, x, y});
        const auto simulation_tick = world.milestone_timer_origin + result.simulation_updates;
        result.draws.push_back({static_cast<std::uint16_t>(0x2e2 + simulation_tick % 29), x, y});
        if (y >= 121) {
            const int lowering = std::clamp(y - 121, 0, 16);
            const auto sprite = lowering < 12 ? 0x105 + lowering : 0x2ff + lowering - 12;
            result.draws.push_back({static_cast<std::uint16_t>(sprite), x - (11 - lowering), 179});
        }
        result.draws.push_back({0x304, x + 9, y + 26});
        if (outhouse.sprite != 0xffff) result.draws.push_back({outhouse.sprite, x + 13, y + 34});
        if (door.type_id && door.draw_callback) {
            const auto sprite = door.draw_callback == 0x3e52 ? special_base + door.counter :
                door.draw_callback == 0x3f48 ? special_base + 2 - door.counter : door.sprite;
            result.draws.push_back({static_cast<std::uint16_t>(sprite), door.x_fixed / 65536, door.y_fixed / 65536});
        }
        if (trooper.type_id && trooper.draw_callback) {
            result.draws.push_back({static_cast<std::uint16_t>(trooper.sprite + trooper.animation % 4),
                trooper.x_fixed / 65536, trooper.y_fixed / 65536});
            result.walking_sound = trooper.vx_fixed && trooper.animation % 4 == 0;
            if (trooper.draw_callback == 0x40b2) result.draws.push_back({0x1df, 148, 166});
        }
        if (index == world.milestone_intermission_frame) return result;
        if (complete) {
            phase = static_cast<Phase>(static_cast<int>(phase) + 1);
            enter_phase = true;
        }
    }
    result.active = false; result.actors.clear(); result.draws.clear(); result.sounds.clear();
    result.walking_sound = result.park_vehicle_sound = result.start_vehicle_sound = false;
    return result;
}

}  // namespace niteraid
