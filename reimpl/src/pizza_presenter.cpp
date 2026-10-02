#include "niteraid/pizza_presenter.hpp"

#include <array>
#include <cmath>

namespace niteraid {
namespace {

enum class Phase {
    Ingress, Stop, Start, Approach, Greeting, Reply, OpenIn, LowerIn,
    Enter, CloseIn, Inside, OpenOut, Leave, RaiseOut, CloseOut, Return,
    DepartureStop, DepartureStart, Egress, Done
};

bool direct_draw(Phase phase) { return phase == Phase::LowerIn || phase == Phase::RaiseOut; }


}  // namespace

PizzaPresenterFrame original_pizza_presenter_frame(const WorldState& world)
{
    PizzaPresenterFrame result {};
    std::array<ScriptedPresenterActor, 4> actors {};
    auto& vehicle = actors[1];
    auto& trooper = actors[2];
    auto& door = actors[3];
    Phase phase = Phase::Ingress;
    bool enter_phase = true;
    std::uint32_t updates = 0;
    for (std::uint32_t draw_index = 0; phase != Phase::Done; ++draw_index) {
        if (!world.pizza_all_draws_diagnostic && !direct_draw(phase) &&
            updates > world.milestone_intermission_frame) return result;
        result.sounds.clear();
        result.stop_vehicle_sound = result.start_vehicle_sound = false;
        const auto wait = [](ScriptedPresenterActor& actor, std::uint32_t ticks) {
            actor.update_callback = 0x558c;
            actor.timer = ticks;
        };
        const auto walk = [&](int target) {
            trooper.update_callback = 0x55b7;
            trooper.counter = 8;
            trooper.vx_fixed = target * 65536 > trooper.x_fixed ? 65536 : -65536;
            trooper.vy_fixed = target * 65536;
        };
        if (enter_phase) {
            switch (phase) {
            case Phase::Ingress:
                vehicle.slot = 1; vehicle.type_id = 14;
                vehicle.x_fixed = 320 * 65536; vehicle.y_fixed = 182 * 65536;
                vehicle.vx_fixed = -102400; vehicle.counter = 80;
                vehicle.update_callback = 0x554f; vehicle.draw_callback = 0x3fe3;
                break;
            case Phase::Stop:
                vehicle.x_fixed = 195 * 65536;
                result.stop_vehicle_sound = true;
                result.sounds.push_back(0x33b); wait(vehicle, 70); break;
            case Phase::Start: case Phase::DepartureStart:
                result.sounds.push_back(0x33d); wait(vehicle, 35); break;
            case Phase::Approach:
                vehicle.update_callback = 0;
                trooper.slot = 2; trooper.type_id = 9; trooper.sprite = 0x2d7;
                trooper.x_fixed = 211 * 65536; trooper.y_fixed = 175 * 65536;
                trooper.draw_callback = 0x404a; walk(161); break;
            case Phase::Greeting: result.sounds.push_back(0x347); wait(trooper, 70); break;
            case Phase::Reply: result.sounds.push_back(0x345); wait(trooper, 210); break;
            case Phase::OpenIn: case Phase::OpenOut:
                trooper.update_callback = 0;
                door = {}; door.slot = 3; door.type_id = 1;
                door.x_fixed = 157 * 65536; door.y_fixed = 172 * 65536;
                door.update_callback = 0x3df8; door.draw_callback = 0x3e52;
                result.sounds.push_back(0x337); break;
            case Phase::LowerIn:
                trooper.y_fixed -= 65536; trooper.draw_callback = 0x40b2;
                trooper.update_callback = 0x55b7; break;
            case Phase::Enter: trooper.y_fixed -= 65536; walk(155); break;
            case Phase::CloseIn: case Phase::CloseOut:
                trooper.update_callback = 0;
                if (phase == Phase::CloseOut) trooper.y_fixed += 65536;
                door.counter = 0; door.timer = 0;
                door.update_callback = 0x3ef7; door.draw_callback = 0x3f48;
                result.sounds.push_back(0x339); break;
            case Phase::Inside:
                door = {}; door.slot = 3; door.type_id = 1; wait(door, 210); break;
            case Phase::Leave:
                trooper.sprite = 0x2db; trooper.draw_callback = 0x40b2; walk(161); break;
            case Phase::RaiseOut:
                trooper.update_callback = 0; trooper.y_fixed += 65536; break;
            case Phase::Return:
                door = {}; trooper.draw_callback = 0x404a; walk(211); break;
            case Phase::DepartureStop:
                trooper = {}; result.sounds.push_back(0x33b); wait(vehicle, 70); break;
            case Phase::Egress:
                result.start_vehicle_sound = true;
                vehicle.vx_fixed = (-31 - 195) * 65536 / 120;
                vehicle.counter = 120; vehicle.update_callback = 0x554f; break;
            case Phase::Done: break;
            }
            enter_phase = false;
        }
        bool complete = direct_draw(phase);
        if (!complete) {
            ++updates;
            for (auto& actor : actors) {
                if (actor.type_id) complete = update_scripted_presenter_actor(actor) || complete;
            }
        }
        result.simulation_updates = updates;
        result.draw_index = draw_index;
        result.actors.clear(); result.draws.clear(); result.walking_sound = false;
        for (const auto& actor : actors) if (actor.type_id) result.actors.push_back(actor);
        if (door.type_id && door.draw_callback) {
            const auto sprite = door.draw_callback == 0x3e52 ? 0x1e2 + door.counter :
                                door.draw_callback == 0x3f48 ? 0x1e4 - door.counter : door.sprite;
            result.draws.push_back({static_cast<std::uint16_t>(sprite), 157, 172});
        }
        if (trooper.type_id) {
            result.draws.push_back({static_cast<std::uint16_t>(trooper.sprite + trooper.animation % 4),
                                   trooper.x_fixed / 65536, trooper.y_fixed / 65536});
            result.walking_sound = trooper.vx_fixed && trooper.animation % 4 == 0;
            if (trooper.draw_callback == 0x40b2) result.draws.push_back({0x1df, 148, 166});
        }
        result.draws.push_back({0x2df,
            static_cast<int>(std::floor(static_cast<double>(vehicle.x_fixed) / 65536)), 170});
        if (world.pizza_all_draws_diagnostic && draw_index == world.milestone_intermission_frame) return result;
        if (complete) {
            phase = static_cast<Phase>(static_cast<int>(phase) + 1);
            enter_phase = true;
        }
    }
    if (!world.pizza_all_draws_diagnostic && updates > world.milestone_intermission_frame) return result;
    result.active = false;
    result.actors.clear(); result.draws.clear(); result.sounds.clear();
    result.walking_sound = result.start_vehicle_sound = false;
    result.stop_vehicle_sound = true;
    return result;
}

}  // namespace niteraid
