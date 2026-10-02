#include "niteraid/game.hpp"
#include "niteraid/gameplay_clock.hpp"
#include "niteraid/presenter_timing.hpp"
#include "niteraid/survivor_presenter.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>

using niteraid::Game;
using niteraid::GameplayState;
using niteraid::InputState;
using niteraid::MilestoneIntermission;
using niteraid::Object;
using niteraid::ObjectType;
using niteraid::Screen;
using niteraid::WorldState;

namespace {

void tick_neutral(Game& game, int ticks)
{
    for (int index = 0; index < ticks; ++index) {
        game.tick(InputState {});
        game.prepare_gameplay_page();
    }
}

Object bunker()
{
    Object object {};
    object.active = true;
    object.type = ObjectType::PlayerCannon;
    object.position = {160.0f, 180.0f};
    object.extent = {13.0f, 6.0f};
    object.frame = 11;
    return object;
}

Object survivor(float x)
{
    Object object {};
    object.active = true;
    object.type = ObjectType::LandedInvader;
    object.position = {x, 172.0f};
    object.frame = 7;
    object.timer = 4;
    return object;
}

std::uint32_t seed_for_mask(std::uint32_t count, std::uint16_t mask)
{
    for (std::uint32_t seed = 1; seed < 100000; ++seed) {
        if (niteraid::original_survivor_failure_mask(seed, count) == mask) {
            return seed;
        }
    }
    CHECK(false);
    return 1;
}

void check_no_active_landed_objects(const WorldState& world)
{
    CHECK(std::none_of(world.objects.begin(), world.objects.end(), [](const Object& object) {
        return object.active && object.type == ObjectType::LandedInvader;
    }));
}

void exercise_handoff(std::uint16_t level, std::uint32_t count, std::uint16_t failure_mask)
{
    Game game(level, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    world.objects.push_back(bunker());
    for (std::uint32_t index = 0; index < count; ++index) {
        world.objects.push_back(survivor(index == 0 ? 120.0f : 194.0f));
    }
    world.current_level = level;
    world.gameplay_state = GameplayState::LevelComplete;
    world.finale_budget_seed = 37;
    world.gameplay_rng_seed = seed_for_mask(count, failure_mask);
    world.object_highwater = world.objects.size();

    game.tick(InputState {});
    CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::Intermission));
    CHECK(world.survivor_intermission_active);

    const auto survivor_duration = niteraid::original_survivor_duration(count, failure_mask);
    CHECK_EQ(world.survivor_intermission_failure_mask, failure_mask);
    tick_neutral(game, static_cast<int>(survivor_duration) - 1);

    CHECK(world.survivor_intermission_active);
    CHECK_EQ(static_cast<int>(std::count_if(
                 world.objects.begin(), world.objects.end(), [](const Object& object) {
                     return object.active && object.type == ObjectType::LandedInvader;
                 })),
             0);
    CHECK(niteraid::original_survivor_presenter_frame(world).active);

    game.tick(InputState {});
    CHECK(world.survivor_live);
    CHECK(!world.survivor_live->owner_return_ready);
    game.prepare_gameplay_page();
    CHECK(world.survivor_live->owner_return_ready);
    CHECK(world.survivor_intermission_active);
    game.tick(InputState {});

    const auto expected_milestone = level == 3
        ? MilestoneIntermission::Level4Pizza
        : MilestoneIntermission::Level8Helicopter;
    const auto expected_milestone_duration = level == 3
        ? niteraid::original_timer_ticks_to_video_frames(1866)
        : niteraid::original_timer_ticks_to_video_frames(1675);
    CHECK_EQ(static_cast<int>(world.milestone_intermission),
             static_cast<int>(expected_milestone));
    CHECK(!world.survivor_intermission_active);
    CHECK_EQ(world.intermission_frames_remaining, expected_milestone_duration - 1);
    CHECK_EQ(world.milestone_intermission_frame, level == 3 ? 0u : 1u);
    check_no_active_landed_objects(world);

    const auto expected_budget = static_cast<std::uint16_t>(
        37 + count - std::popcount(static_cast<unsigned>(failure_mask)));
    CHECK_EQ(world.finale_budget_seed, expected_budget);

    // The next presenter may reuse a consumed trooper's freed slot.
    CHECK(world.objects[0].active);

    game.tick(InputState {});
    check_no_active_landed_objects(world);
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::PlayerCannon);
    CHECK_NEAR(world.objects[0].position.x, 160.0f, 0.0f);
    CHECK_EQ(world.intermission_frames_remaining, expected_milestone_duration - 2);

    tick_neutral(game, expected_milestone_duration - 2);
    if (level == 3) {
        CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::Intermission));
        CHECK_EQ(world.milestone_intermission_frame, 1865u);
        game.tick(InputState {});
    }
    CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::Gameplay));
    CHECK_EQ(world.current_level, static_cast<std::uint16_t>(level + 1));
    CHECK_EQ(world.finale_budget_seed, expected_budget);
}

}  // namespace

TEST_CASE("survivor handoff covers common and rare one and two trooper level milestones")
{
    for (const auto level : {std::uint16_t {3}, std::uint16_t {7}}) {
        for (const auto count : {std::uint32_t {1}, std::uint32_t {2}}) {
            const auto rare_mask = static_cast<std::uint16_t>((1u << count) - 1u);
            exercise_handoff(level, count, 0);
            exercise_handoff(level, count, rare_mask);
            if (count == 2) {
                exercise_handoff(level, count, 1);
                exercise_handoff(level, count, 2);
            }
        }
    }
}

TEST_CASE("survivor handoff retirement preserves source records and bunker")
{
    WorldState world {};
    world.objects.push_back(bunker());
    world.objects.push_back(survivor(133.0f));
    world.objects[1].pending_destroy = true;

    niteraid::retire_survivor_handoff_objects(world);

    CHECK(!world.objects[1].active);
    CHECK(!world.objects[1].pending_destroy);
    CHECK_NEAR(world.objects[1].position.x, 133.0f, 0.0f);
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::PlayerCannon);
}

TEST_CASE("survivor owner allocates its live controller before ingress")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.objects = {bunker(), survivor(120)};
    world.gameplay_rng_seed = seed_for_mask(1, 0);
    world.object_highwater = world.objects.size();
    world.gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});

    const auto controller = std::find_if(world.objects.begin(), world.objects.end(), [](const Object& object) {
        return object.active && object.type == ObjectType::Presenter;
    });
    CHECK(controller != world.objects.end());
    if (controller == world.objects.end()) {
        return;
    }
    CHECK_EQ(controller->sprite_id, 0x28e);
    CHECK_EQ(controller->frame, 30);
    CHECK_NEAR(controller->position.x, 264, 0);
    CHECK_NEAR(controller->position.y, 34, 0);
    CHECK_EQ(world.objects[1].sprite_id, 0x2ca);
    CHECK(world.gameplay_state == GameplayState::Active);

    tick_neutral(game, 186);
    CHECK(world.objects[1].active);
    CHECK_EQ(world.finale_budget_seed, 0);
    game.tick(InputState {});
    CHECK(!world.objects[1].active);
    CHECK_EQ(world.finale_budget_seed, 1);
}

TEST_CASE("live survivor callbacks match the recovered common and rare draw sequence")
{
    for (const auto mask : {std::uint16_t {0}, std::uint16_t {1}, std::uint16_t {2}, std::uint16_t {3}}) {
        Game game(std::uint16_t {3});
        auto& world = game.diagnostic_world();
        world.objects = {bunker(), survivor(120), survivor(194)};
        world.object_highwater = world.objects.size();
        world.gameplay_rng_seed = seed_for_mask(2, mask);
        world.gameplay_state = GameplayState::LevelComplete;
        game.tick(InputState {});
        niteraid::SurvivorPresenterInput input {};
        input.timer_origin = world.frame_tick;
        input.random_seed = world.gameplay_rng_seed;
        input.troopers = {{{120, 172}}, {{194, 172}}};
        const auto duration = niteraid::original_survivor_duration(2, mask);
        for (std::uint32_t draw = 1; draw <= duration; ++draw) {
            game.tick(InputState {});
            game.prepare_gameplay_page();
            const auto& page = world.survivor_live ? world : *game.before_owner_page();
            const auto actual = niteraid::original_survivor_presenter_frame(page);
            const auto expected = niteraid::original_survivor_presenter_frame(input, draw);
            CHECK_EQ(actual.x_fixed, expected.x_fixed);
            CHECK_EQ(actual.y_fixed, expected.y_fixed);
            CHECK_EQ(actual.vx_fixed, expected.vx_fixed);
            CHECK_EQ(actual.vy_fixed, expected.vy_fixed);
            CHECK_EQ(actual.counter, expected.counter);
            CHECK_EQ(actual.animation_timer, expected.animation_timer);
            CHECK_EQ(actual.update_callback, expected.update_callback);
            CHECK_EQ(actual.draw_callback, expected.draw_callback);
            CHECK_EQ(actual.random_seed, expected.random_seed);
            CHECK_EQ(actual.timer_tick, expected.timer_tick);
            CHECK_EQ(actual.retained_troopers.size(), expected.retained_troopers.size());
            CHECK_EQ(actual.draws.size(), expected.draws.size());
            if (actual.draws.size() == expected.draws.size()) {
                for (std::size_t index = 0; index < actual.draws.size(); ++index) {
                    CHECK_EQ(actual.draws[index].sprite, expected.draws[index].sprite);
                    CHECK_EQ(actual.draws[index].position.x, expected.draws[index].position.x);
                    CHECK_EQ(actual.draws[index].position.y, expected.draws[index].position.y);
                }
            }
        }
        CHECK(world.survivor_live);
        CHECK(world.survivor_live->owner_return_ready);
        game.tick(InputState {});
        CHECK(!world.survivor_live);
        CHECK(world.milestone_intermission == MilestoneIntermission::Level4Pizza);
    }
}

TEST_CASE("rare survivor RNG belongs to actual page preparation not rendering or updates")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.objects = {bunker(), survivor(120)};
    world.object_highwater = world.objects.size();
    world.gameplay_rng_seed = seed_for_mask(1, 1);
    world.gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});
    tick_neutral(game, 186);
    auto branch_seed = world.gameplay_rng_seed;
    niteraid::original_random_bounded(branch_seed, 15);
    game.tick(InputState {});
    CHECK_EQ(world.gameplay_rng_seed, branch_seed);
    CHECK(!world.objects[1].active);
    CHECK(world.survivor_live->phase == niteraid::SurvivorPhase::Rare);
    CHECK_EQ(world.objects[world.survivor_live->controller_slot].sound_id, 0x361);
    CHECK_EQ(niteraid::original_survivor_presenter_frame(world).random_seed, branch_seed);
    CHECK_EQ(niteraid::original_survivor_presenter_frame(world).random_seed, branch_seed);
    auto draw_seed = branch_seed;
    niteraid::original_random_bounded(draw_seed, 2);
    game.prepare_gameplay_page();
    CHECK_EQ(world.gameplay_rng_seed, draw_seed);
    game.tick(InputState {});
    CHECK_EQ(world.gameplay_rng_seed, draw_seed);
}

TEST_CASE("live UFO motion retains mutations until the recovered target snap")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.objects = {bunker(), survivor(120)};
    world.object_highwater = world.objects.size();
    world.gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});
    const auto slot = world.survivor_live->controller_slot;
    auto& controller = world.objects[slot];
    controller.position.x += 1;
    controller.velocity.x = -0.5f;
    game.tick(InputState {});
    CHECK_EQ(controller.position.x, 264.5);
    tick_neutral(game, 30);
    CHECK_EQ(controller.position.x, 249.5);
    CHECK_EQ(controller.frame, -1);
    game.tick(InputState {});
    CHECK_EQ(controller.position.x, 240.0 + controller.velocity.x);
    CHECK_EQ(controller.frame, 29);
}

TEST_CASE("survivor blocking owner waits for a completed page before changing legs")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.objects = {bunker(), survivor(120)};
    world.object_highwater = world.objects.size();
    world.gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});
    auto& controller = world.objects.at(world.survivor_live->controller_slot);
    controller.frame = 1;
    const auto x = controller.position.x;
    const auto vx = controller.velocity.x;
    const auto advanced = niteraid::advance_gameplay_batch(game, {}, 4, [](const WorldState&) {});
    CHECK_EQ(advanced, 4u);
    CHECK_EQ(world.survivor_live->leg, 0);
    CHECK_EQ(controller.frame, -3);
    CHECK_EQ(controller.position.x, x + vx * 4.0);
    game.prepare_gameplay_page();
    CHECK_EQ(world.survivor_live->leg, 0);
    CHECK_EQ(niteraid::advance_gameplay_batch(game, {}, 1, [](const WorldState&) {}), 1u);
    CHECK_EQ(world.survivor_live->leg, 1);
    CHECK_EQ(controller.frame, 29);
    CHECK_EQ(controller.position.x, 240.0 + controller.velocity.x);
}

TEST_CASE("live survivor multi-update pages match recovered batched choreography")
{
    for (const auto mask : {std::uint16_t {0}, std::uint16_t {1}, std::uint16_t {2}, std::uint16_t {3}}) {
        Game game(std::uint16_t {3});
        auto& world = game.diagnostic_world();
        world.objects = {bunker(), survivor(120), survivor(194)};
        world.object_highwater = world.objects.size();
        world.gameplay_rng_seed = seed_for_mask(2, mask);
        world.gameplay_state = GameplayState::LevelComplete;
        game.tick(InputState {});
        niteraid::SurvivorPresenterInput input {};
        input.timer_origin = world.frame_tick;
        input.random_seed = world.gameplay_rng_seed;
        input.troopers = {{{120, 172}}, {{194, 172}}};
        constexpr std::array<unsigned, 6> budgets {6, 4, 1, 3, 2, 5};
        unsigned draws = 0;
        while (world.survivor_live && draws < 1000) {
            const auto budget = budgets[draws % budgets.size()];
            const auto advanced = niteraid::advance_gameplay_batch(game, {}, budget, [](const WorldState&) {});
            if (!world.survivor_live) {
                break;
            }
            CHECK_EQ(advanced, budget);
            input.update_batches.push_back(static_cast<std::uint8_t>(advanced));
            game.prepare_gameplay_page();
            const auto actual = niteraid::original_survivor_presenter_frame(world);
            const auto expected = niteraid::original_survivor_presenter_frame(input, ++draws);
            CHECK_EQ(actual.x_fixed, expected.x_fixed);
            CHECK_EQ(actual.y_fixed, expected.y_fixed);
            CHECK_EQ(actual.counter, expected.counter);
            CHECK_EQ(actual.animation_timer, expected.animation_timer);
            CHECK_EQ(actual.random_seed, expected.random_seed);
            CHECK_EQ(actual.timer_tick, expected.timer_tick);
            CHECK_EQ(actual.draw_callback, expected.draw_callback);
            CHECK_EQ(actual.retained_troopers.size(), expected.retained_troopers.size());
            CHECK_EQ(actual.draws.size(), expected.draws.size());
            if (actual.draws.size() == expected.draws.size()) {
                for (std::size_t index = 0; index < actual.draws.size(); ++index) {
                    CHECK_EQ(actual.draws[index].sprite, expected.draws[index].sprite);
                    CHECK_EQ(actual.draws[index].position.x, expected.draws[index].position.x);
                    CHECK_EQ(actual.draws[index].position.y, expected.draws[index].position.y);
                }
            }
        }
        CHECK(draws < 1000);
        CHECK(!world.survivor_live);
        CHECK(world.milestone_intermission == MilestoneIntermission::Level4Pizza);
    }
}
