#include "niteraid/survivor_presenter.hpp"
#include "niteraid/game_internals.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace niteraid {
namespace {

std::int32_t fixed(double value)
{
    return static_cast<std::int32_t>(std::llround(value * 65536.0));
}

float pixel(std::int32_t value)
{
    return static_cast<float>(std::floor(static_cast<double>(value) / 65536.0));
}

int bounded(std::uint32_t& seed, int limit)
{
    seed = seed * 0x015a4e35u + 1u;
    return static_cast<int>(((seed >> 16u) & 0x7fffu) * static_cast<std::uint32_t>(limit) / 0x8000u);
}

enum class Phase { Ingress, PickupMove, Common, Rare, Aftermath, Egress, Done };

using survivor_timing::kIngressTargets;
using survivor_timing::kEgressTargets;
using survivor_timing::kAftermathWaits;

SurvivorPresenterFrame live_survivor_frame(const WorldState& world)
{
    const auto& scene = *world.survivor_live;
    const auto& controller = world.objects.at(scene.controller_slot);
    SurvivorPresenterFrame result {};
    result.active = controller.active;
    result.timer_tick = world.frame_tick;
    result.random_seed = world.gameplay_rng_seed;
    result.x_fixed = fixed(controller.position.x);
    result.y_fixed = fixed(controller.position.y);
    result.vx_fixed = fixed(controller.velocity.x);
    result.vy_fixed = fixed(controller.velocity.y);
    result.sprite = static_cast<std::uint16_t>(controller.sprite_id);
    result.counter = controller.frame;
    result.animation_timer = controller.timer;
    switch (scene.phase) {
    case SurvivorPhase::Ingress:
        result.update_callback = 0x554f;
        result.draw_callback = scene.leg == 0 ? 0x3644 : 0x543a;
        break;
    case SurvivorPhase::PickupMove:
        result.update_callback = 0x554f;
        result.draw_callback = 0x366d;
        break;
    case SurvivorPhase::Common:
        result.update_callback = 0x3741;
        result.draw_callback = scene.animation_complete ? 0x366d : 0x36fc;
        break;
    case SurvivorPhase::Rare:
        result.update_callback = 0x380e;
        result.draw_callback = scene.animation_complete ? 0x366d : 0x378f;
        break;
    case SurvivorPhase::Aftermath:
        result.update_callback = 0x38e4;
        result.draw_callback = scene.animation_complete ? 0x366d : 0x385c;
        break;
    case SurvivorPhase::Egress:
        result.update_callback = 0x554f;
        result.draw_callback = scene.leg >= 3 ? 0x3644 : 0x543a;
        break;
    }
    if (!result.active) {
        return result;
    }
    const Vec2 position {pixel(result.x_fixed), pixel(result.y_fixed)};
    const auto draw = [&](int sprite, float dx = 0, float dy = 0) {
        result.draws.push_back({static_cast<std::uint16_t>(sprite), {position.x + dx, position.y + dy}});
    };
    if (result.draw_callback == 0x3644 || result.draw_callback == 0x543a) {
        draw(result.sprite);
        if (result.draw_callback == 0x3644) {
            result.draws.push_back({0x28d, {264, 27}});
        }
    } else {
        draw(0x293);
        draw(0x294 + (result.timer_tick / 4) % 3);
        draw(0x297 + (result.timer_tick / 15) % 8, 7);
        if (result.draw_callback == 0x36fc) {
            draw(0x29f + result.counter, 9, 8);
        } else if (result.draw_callback == 0x378f) {
            if (result.counter < 8) {
                draw(0x2b4 + scene.beam_variant, 0, 8);
            }
            draw(0x2b9 + result.counter, 10, 28);
        } else if (result.draw_callback == 0x385c) {
            if (result.counter == 1) {
                draw(0x2b6, 20, -16);
            } else if (result.counter == 3) {
                draw(0x2b7, -14, -10);
            } else if (result.counter == 5) {
                draw(0x2b8, 20, -10);
            }
        }
    }
    for (const auto& object : world.objects) {
        if (object.active && !object.pending_destroy && object.type == ObjectType::LandedInvader) {
            result.retained_troopers.push_back(object.position);
        }
    }
    return result;
}

}  // namespace

std::uint16_t original_survivor_failure_mask(std::uint32_t seed, std::uint32_t count)
{
    std::uint16_t mask = 0;
    for (std::uint32_t index = 0; index < count && index < 16; ++index) {
        if (bounded(seed, 15) == 0) {
            mask |= static_cast<std::uint16_t>(1u << index);
            // 380e switches away from 378f on update 119, so only the preceding
            // 118 completed draws consume the rare beam's random(2) call.
            for (int draw = 0; draw < 118; ++draw) bounded(seed, 2);
        }
    }
    return mask;
}

std::uint32_t original_survivor_duration(std::uint32_t count, std::uint16_t failure_mask)
{
    if (count == 0) return 0;
    std::uint32_t duration = 10 * 31;
    for (std::uint32_t index = 0; index < count; ++index) {
        duration += 31 + (index < 16 && (failure_mask & (1u << index)) ? 440 : 168);
    }
    return duration;
}

void retire_survivor_handoff_objects(WorldState& world)
{
    for (auto& object : world.objects) {
        if (!object.active || object.type != ObjectType::LandedInvader) {
            continue;
        }

        // Keep the immutable presenter inputs in the record, but make the
        // consumed gameplay actor invisible and non-owning before milestone
        // rendering starts.
        object.active = false;
        object.pending_destroy = false;
        object.sound_id = 0;
    }
}

SurvivorPresenterFrame original_survivor_presenter_frame(
    const SurvivorPresenterInput& input, std::uint32_t draw_count)
{
    SurvivorPresenterFrame result {};
    result.timer_tick = input.timer_origin;
    result.random_seed = input.random_seed;
    if (input.troopers.empty() || draw_count == 0) {
        return result;
    }
    if (!input.update_batches.empty() && input.update_batches.size() < draw_count) {
        throw std::invalid_argument("survivor update schedule is shorter than the requested draw count");
    }

    auto troopers = input.troopers;
    std::size_t retained_from = 0;
    std::size_t pickup = 0;
    int leg = 0;
    Phase phase = Phase::Ingress;
    bool complete = false;
    WorldPosition target = kIngressTargets[0];
    result.active = true;
    result.x_fixed = fixed(264);
    result.y_fixed = fixed(34);

    const auto move = [&](WorldPosition next, std::uint16_t sprite, std::uint16_t draw) {
        target = next;
        result.vx_fixed = (fixed(next.x) - result.x_fixed) / 30;
        result.vy_fixed = (fixed(next.y) - result.y_fixed) / 30;
        result.counter = 30;
        result.sprite = sprite;
        result.update_callback = 0x554f;
        result.draw_callback = draw;
    };
    const auto pickup_move = [&]() {
        phase = Phase::PickupMove;
        move({std::floor(troopers[pickup].position.x) - 10,
              std::floor(troopers[pickup].position.y) - 28},
             0xffff, 0x366d);
    };
    const auto finish_pickup = [&]() {
        ++pickup;
        if (pickup < troopers.size()) {
            pickup_move();
        } else {
            phase = Phase::Egress;
            leg = 0;
            move(kEgressTargets[0], 0x292, 0x543a);
        }
    };
    move(target, 0x28e, 0x3644);

    for (std::uint32_t tick = 0; tick < draw_count && phase != Phase::Done; ++tick) {
        result.sounds.clear();
        result.successful_pickup = false;
        // The blocking owner changes callbacks and snaps the target only after
        // the previous completed draw, never before its final movement update.
        if (complete) {
            complete = false;
            if (phase == Phase::Ingress || phase == Phase::PickupMove || phase == Phase::Egress) {
                result.x_fixed = fixed(target.x);
                result.y_fixed = fixed(target.y);
            }
            switch (phase) {
            case Phase::Ingress:
                if (++leg < 5) {
                    move(kIngressTargets[leg], static_cast<std::uint16_t>(0x28e + leg), 0x543a);
                } else {
                    pickup_move();
                }
                break;
            case Phase::PickupMove: {
                const bool random_failure = bounded(result.random_seed, 15) == 0;
                const bool failed = input.forced_failure_mask && pickup < 16
                    ? (*input.forced_failure_mask & (1u << pickup)) != 0 : random_failure;
                phase = failed ? Phase::Rare : Phase::Common;
                result.update_callback = failed ? 0x380e : 0x3741;
                result.draw_callback = failed ? 0x378f : 0x36fc;
                result.counter = 0;
                result.animation_timer = 0;
                retained_from = pickup + 1;
                result.successful_pickup = !failed;
                result.sounds.push_back({SurvivorSoundAction::Bind,
                                         static_cast<std::uint16_t>(failed ? 0x361 : 0x363)});
                break;
            }
            case Phase::Common:
                result.sounds.push_back({SurvivorSoundAction::Stop});
                finish_pickup();
                break;
            case Phase::Rare:
                result.sounds.push_back({SurvivorSoundAction::Stop});
                phase = Phase::Aftermath;
                result.counter = 0;
                result.animation_timer = 0;
                result.update_callback = 0x38e4;
                result.draw_callback = 0x385c;
                break;
            case Phase::Aftermath:
                finish_pickup();
                break;
            case Phase::Egress:
                if (++leg < 5) {
                    move(kEgressTargets[leg], static_cast<std::uint16_t>(0x292 - leg),
                         leg >= 3 ? 0x3644 : 0x543a);
                } else {
                    phase = Phase::Done;
                    result.active = false;
                }
                break;
            case Phase::Done:
                break;
            }
        }
        if (phase == Phase::Done) {
            result.draws.clear();
            break;
        }

        const auto batch = input.update_batches.empty() ? 1u : input.update_batches[tick];
        if (batch < 1 || batch > 6) {
            throw std::invalid_argument("survivor update batch is outside the original 1..6 clamp");
        }
        for (unsigned step = 0; step < batch; ++step) {
            ++result.timer_tick;
            if (phase == Phase::Ingress || phase == Phase::PickupMove || phase == Phase::Egress) {
                result.x_fixed += result.vx_fixed;
                result.y_fixed += result.vy_fixed;
                complete = result.counter-- < 1;
            } else if (phase == Phase::Common || phase == Phase::Rare) {
                const auto duration = phase == Phase::Common ? 8u : 7u;
                const int frame_count = phase == Phase::Common ? 21 : 17;
                if (++result.animation_timer >= duration) {
                    result.animation_timer = 0;
                    if (++result.counter >= frame_count) {
                        complete = true;
                        result.draw_callback = 0x366d;
                    }
                }
            } else if (phase == Phase::Aftermath) {
                const auto old_timer = result.animation_timer++;
                if (result.counter <= 5 && old_timer >= kAftermathWaits[result.counter]) {
                    result.animation_timer = 0;
                    if (++result.counter > 5) {
                        complete = true;
                        result.draw_callback = 0x366d;
                    } else if (result.counter % 2 == 1) {
                        result.sounds.push_back({SurvivorSoundAction::Play,
                            static_cast<std::uint16_t>(0x35b + result.counter - 1)});
                    }
                }
            }

            for (std::size_t index = retained_from; index < troopers.size(); ++index) {
                auto& trooper = troopers[index];
                if (trooper.walk_period > 0 && ++trooper.walk_timer >= static_cast<std::uint32_t>(trooper.walk_period)) {
                    trooper.walk_timer = 0;
                    trooper.position.x += trooper.walk_velocity;
                    if (trooper.position.x == trooper.walk_target) {
                        trooper.walk_velocity = 0;
                    }
                }
            }
        }

        result.draws.clear();
        const Vec2 position {pixel(result.x_fixed), pixel(result.y_fixed)};
        const auto draw = [&](int sprite, float dx = 0, float dy = 0) {
            result.draws.push_back({static_cast<std::uint16_t>(sprite), {position.x + dx, position.y + dy}});
        };
        if (result.draw_callback == 0x3644 || result.draw_callback == 0x543a) {
            draw(result.sprite);
            if (result.draw_callback == 0x3644) {
                result.draws.push_back({0x28d, {264, 27}});
            }
        } else {
            draw(0x293);
            draw(0x294 + (result.timer_tick / 4) % 3);
            draw(0x297 + (result.timer_tick / 15) % 8, 7);
            if (result.draw_callback == 0x36fc) {
                draw(0x29f + result.counter, 9, 8);
            } else if (result.draw_callback == 0x378f) {
                const auto flicker = bounded(result.random_seed, 2);
                if (result.counter < 8) {
                    draw(0x2b4 + flicker, 0, 8);
                } else if (result.counter == 8) {
                    result.sounds.push_back({SurvivorSoundAction::Stop});
                }
                draw(0x2b9 + result.counter, 10, 28);
            } else if (result.draw_callback == 0x385c) {
                if (result.counter == 1) draw(0x2b6, 20, -16);
                if (result.counter == 3) draw(0x2b7, -14, -10);
                if (result.counter == 5) draw(0x2b8, 20, -10);
            }
        }
        result.retained_troopers.clear();
        for (std::size_t index = retained_from; index < troopers.size(); ++index) {
            result.retained_troopers.push_back(troopers[index].position);
        }
    }
    return result;
}

SurvivorPresenterFrame original_survivor_presenter_frame(const WorldState& world)
{
    if (world.survivor_live) {
        return live_survivor_frame(world);
    }
    SurvivorPresenterInput input {};
    input.timer_origin = world.survivor_intermission_timer_origin;
    input.random_seed = world.survivor_intermission_rng_seed;
    input.forced_failure_mask = world.survivor_intermission_failure_mask;
    input.update_batches = world.survivor_presenter_update_batches;
    for (const auto& object : world.objects) {
        if (object.active && !object.pending_destroy && object.type == ObjectType::LandedInvader) {
            input.troopers.push_back({object.position, object.assaulting ? object.frame : 0,
                                     object.timer, object.velocity.x, object.velocity.y});
        }
    }
    return original_survivor_presenter_frame(input, world.survivor_intermission_frame + 1);
}

SurvivorPresenterFrame original_no_survivor_presenter_frame(const WorldState& world)
{
    SurvivorPresenterFrame result {};
    const auto updates = std::min(world.survivor_intermission_frame, 544u) + 1;
    result.active = world.survivor_intermission_frame < 545;
    result.timer_tick = world.survivor_intermission_timer_origin + updates;
    result.random_seed = world.survivor_intermission_rng_seed;
    result.x_fixed = (320 - static_cast<std::int32_t>(updates)) * 65536;
    result.y_fixed = 18 * 65536;
    result.vx_fixed = -65536;
    result.counter = 544 - static_cast<int>(updates);
    const auto actor = std::find_if(world.objects.begin(), world.objects.end(), [](const Object& object) {
        return object.active && !object.pending_destroy && object.type == ObjectType::Presenter &&
               object.sprite_id == internals::kBannerFlybyAircraftSprite;
    });
    if (world.no_survivor_intermission_active && actor != world.objects.end()) {
        result.active = actor->frame >= 0 && actor->frame < 544;
        result.timer_tick = world.frame_tick;
        result.random_seed = world.gameplay_rng_seed;
        result.x_fixed = fixed(actor->position.x);
        result.y_fixed = fixed(actor->position.y);
        result.vx_fixed = fixed(actor->velocity.x);
        result.counter = actor->frame;
    }
    result.sprite = 0xffff;
    result.update_callback = 0x554f;
    result.draw_callback = 0x3ccd;
    if (result.active) {
        const auto x = pixel(result.x_fixed);
        const auto animation = static_cast<std::uint16_t>(result.counter) % 4;
        result.draws.push_back({static_cast<std::uint16_t>(0x264 + animation), {x, 18}});
        for (int index = 0; index < 4; ++index) {
            result.draws.push_back({static_cast<std::uint16_t>(0x260 + index), {x + index * 56.0f, 18}});
        }
    }
    return result;
}

}  // namespace niteraid
