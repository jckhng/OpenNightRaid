#include "test_harness.hpp"

#include "niteraid/presentation.hpp"
#include "niteraid/survivor_presenter.hpp"

using niteraid::GameplayState;
using niteraid::Object;
using niteraid::ObjectType;
using niteraid::Screen;
using niteraid::WorldState;
using niteraid::make_presentation_world;

namespace {

WorldState moving_world(float x, std::uint32_t tick)
{
    WorldState world {};
    world.screen = Screen::Gameplay;
    world.gameplay_state = GameplayState::Active;
    world.frame_tick = tick;

    Object aircraft {};
    aircraft.presentation_id = 7;
    aircraft.active = true;
    aircraft.type = ObjectType::Aircraft;
    aircraft.position = {x, 40.0f};
    world.objects.push_back(aircraft);
    return world;
}

}  // namespace

TEST_CASE("presentation interpolation retains fixed units immediately below an integer")
{
    constexpr double fixed_unit = 1.0 / 65536.0;
    auto previous = moving_world(256.0f, 20);
    auto current = moving_world(258.0f, 21);
    current.objects[0].position.x =
        static_cast<decltype(current.objects[0].position.x)>(258.0 - 2.0 * fixed_unit);

    // Both inputs fit float; the midpoint does not. Snap only after interpolation.
    const auto halfway = make_presentation_world(previous, current, 0.5f);
    CHECK_EQ(halfway.objects[0].position.x, 257.0 - fixed_unit);
    CHECK_EQ(std::floor(halfway.objects[0].position.x), 256.0);
    CHECK_EQ(make_presentation_world(previous, current, 0.0f).objects[0].position.x, 256.0);
    CHECK_EQ(make_presentation_world(previous, current, 1.0f).objects[0].position.x,
             258.0 - 2.0 * fixed_unit);
    CHECK_EQ(current.objects[0].position.x, 258.0 - 2.0 * fixed_unit);
}

TEST_CASE("survivor presenter handoff retains the gameplay coordinate low word")
{
    constexpr double fixed_unit = 1.0 / 65536.0;
    constexpr double survivor_x = 257.0 - fixed_unit;
    WorldState world {};
    world.survivor_intermission_rng_seed = 1;
    Object survivor {};
    survivor.active = true;
    survivor.type = ObjectType::LandedInvader;
    survivor.position = {static_cast<decltype(survivor.position.x)>(survivor_x), 172};
    world.objects.push_back(survivor);

    const auto frame = niteraid::original_survivor_presenter_frame(world);
    CHECK_EQ(frame.retained_troopers.size(), std::size_t {1});
    if (frame.retained_troopers.size() != 1) {
        return;
    }
    CHECK_EQ(frame.retained_troopers[0].x, survivor_x);
    CHECK_EQ(frame.retained_troopers[0].y, 172.0);
    CHECK_EQ(std::floor(frame.retained_troopers[0].x), 256.0);
    CHECK_EQ(world.objects[0].position.x, survivor_x);
}

TEST_CASE("presentation snapshot interpolates stable object identities")
{
    const auto previous = moving_world(10.0f, 20);
    const auto current = moving_world(12.0f, 21);

    const auto halfway = make_presentation_world(previous, current, 0.5f);
    CHECK_NEAR(halfway.presentation_alpha, 0.5f, 0.0001f);
    CHECK_NEAR(halfway.objects[0].position.x, 11.0f, 0.0001f);
    CHECK_NEAR(halfway.objects[0].position.y, 40.0f, 0.0001f);
    CHECK_NEAR(current.objects[0].position.x, 12.0f, 0.0001f);

    CHECK_NEAR(make_presentation_world(previous, current, -1.0f).objects[0].position.x,
               10.0f, 0.0001f);
    CHECK_NEAR(make_presentation_world(previous, current, 2.0f).objects[0].position.x,
               12.0f, 0.0001f);
}

TEST_CASE("original presenter route accumulates truncated fixed point velocity")
{
    using niteraid::original_presenter_route_position;
    // (40 << 16) / 30 = 87381, so three updates stop one fixed unit short of 4 pixels.
    const auto forward = original_presenter_route_position({140, 0}, {180, 0}, 3, 30);
    CHECK_NEAR(forward.x, 144.0f - 1.0f / 65536.0f, 0.000001f);
    CHECK(static_cast<int>(forward.x) == 143);
    const auto backward = original_presenter_route_position({180, 0}, {140, 0}, 3, 30);
    CHECK_NEAR(backward.x, 176.0f + 1.0f / 65536.0f, 0.000001f);
    const auto endpoint = original_presenter_route_position({140, 0}, {180, 0}, 30, 30);
    CHECK(endpoint.x < 180.0f);
    CHECK(original_presenter_route_position({280, 0}, {320, 0}, 3, 30, true).x == 283.0f);
    CHECK(original_presenter_route_position({0, 0}, {-40, 0}, 1, 30, true).x == -2.0f);
    // The inclusive callback can update once more before its owner snaps to the target.
    CHECK(original_presenter_route_position({140, 0}, {180, 0}, 31, 30).x > 180.0f);
    CHECK(original_presenter_route_position({0, 0}, {10, 20}, 0, 30).x == 0.0f);
    CHECK(original_presenter_route_position({0, 0}, {10, 20}, 1, 0).y == 20.0f);
}

TEST_CASE("survivor presenter matches halted original ingress and rare RNG states")
{
    niteraid::SurvivorPresenterInput input {};
    input.timer_origin = 2583;
    input.random_seed = 0xd130ed6d;
    input.forced_failure_mask = 0xffff;
    input.troopers = {{{133, 172}, 8, 4, 1, 161}, {{194, 172}}};
    const auto first = niteraid::original_survivor_presenter_frame(input, 1);
    CHECK(first.timer_tick == 2584);
    CHECK(first.x_fixed == 264 * 65536 - 52428);
    CHECK(first.counter == 29);
    CHECK(first.draw_callback == 0x3644);
    CHECK(first.random_seed == 0xd130ed6d);
    CHECK(first.retained_troopers[0].x == 133);
    CHECK(first.draws.size() == 2);
    CHECK(first.draws[1].sprite == 0x28d);
    CHECK(first.draws[1].position.x == 264);
    CHECK(first.draws[1].position.y == 27);

    const auto overshoot = niteraid::original_survivor_presenter_frame(input, 155);
    CHECK(overshoot.counter == -1);
    CHECK(overshoot.sprite == 0x292);
    CHECK(overshoot.retained_troopers[0].x == 152);
    const auto transfer = niteraid::original_survivor_presenter_frame(input, 156);
    CHECK(transfer.counter == 29);
    CHECK(transfer.draw_callback == 0x366d);
    CHECK(transfer.retained_troopers[0].x == 153);

    const auto pickup = niteraid::original_survivor_presenter_frame(input, 187);
    CHECK(pickup.x_fixed == 142 * 65536);
    CHECK(pickup.y_fixed == 144 * 65536);
    CHECK(pickup.counter == 0);
    CHECK(pickup.animation_timer == 1);
    CHECK(pickup.draw_callback == 0x378f);
    CHECK(pickup.random_seed == 364632891u);
    CHECK(pickup.retained_troopers.size() == 1);
    const auto later = niteraid::original_survivor_presenter_frame(input, 201);
    CHECK(later.counter == 2);
    CHECK(later.animation_timer == 1);
    CHECK(later.random_seed == 162012061u);
}

TEST_CASE("survivor presenter does not invent walking for stationary survivors")
{
    niteraid::SurvivorPresenterInput input {};
    input.troopers = {{{120, 172}}, {{194, 172}}};
    const auto state = niteraid::original_survivor_presenter_frame(input, 100);
    CHECK(state.retained_troopers[0].x == 120);
    CHECK(state.retained_troopers[1].x == 194);
    CHECK(niteraid::original_survivor_presenter_frame(input, 0).draws.empty());
    input.troopers.clear();
    CHECK(!niteraid::original_survivor_presenter_frame(input, 100).active);
}

TEST_CASE("survivor draw RNG is independent of update batches and repeat rendering")
{
    niteraid::SurvivorPresenterInput input {};
    input.timer_origin = 2583;
    input.random_seed = 0xd130ed6d;
    input.forced_failure_mask = 0xffff;
    input.troopers = {{{133, 172}, 8, 4, 1, 161}, {{194, 172}}};
    const auto single = niteraid::original_survivor_presenter_frame(input, 187);
    input.update_batches.assign(187, 1);
    input.update_batches.back() = 3;
    const auto batch = niteraid::original_survivor_presenter_frame(input, 187);
    CHECK(batch.timer_tick == single.timer_tick + 2);
    CHECK(batch.animation_timer == 3);
    CHECK(batch.random_seed == single.random_seed);
    CHECK(niteraid::original_survivor_presenter_frame(input, 187).random_seed == batch.random_seed);
    input.update_batches.assign(307, 1);
    input.update_batches.back() = 3;
    const auto aftermath = niteraid::original_survivor_presenter_frame(input, 307);
    CHECK(aftermath.counter == 1);
    CHECK(aftermath.animation_timer == 3);
    CHECK(aftermath.random_seed == 0x90a56684);
}

TEST_CASE("survivor native duration includes each inclusive movement update")
{
    CHECK(niteraid::original_survivor_duration(0, 0) == 0);
    CHECK(niteraid::original_survivor_duration(2, 0) == 708);
    CHECK(niteraid::original_survivor_duration(2, 3) == 1252);
    CHECK(niteraid::original_survivor_failure_mask(0xd130ed6d, 2) == 0);
    niteraid::SurvivorPresenterInput input {};
    input.troopers = {{{152, 172}}, {{194, 172}}};
    input.forced_failure_mask = 0;
    CHECK(niteraid::original_survivor_presenter_frame(input, 708).active);
    CHECK(!niteraid::original_survivor_presenter_frame(input, 709).active);
    input.forced_failure_mask = 3;
    CHECK(niteraid::original_survivor_presenter_frame(input, 1252).active);
    CHECK(!niteraid::original_survivor_presenter_frame(input, 1253).active);
}

TEST_CASE("survivor sounds follow recovered callback boundaries")
{
    using niteraid::SurvivorSoundAction;
    niteraid::SurvivorPresenterInput input {};
    input.troopers = {{{152, 172}}};
    input.forced_failure_mask = 1;
    CHECK(niteraid::original_survivor_presenter_frame(input, 186).sounds.empty());
    const auto pickup = niteraid::original_survivor_presenter_frame(input, 187);
    CHECK(pickup.sounds.size() == 1);
    CHECK(pickup.sounds[0].action == SurvivorSoundAction::Bind);
    CHECK(pickup.sounds[0].sound == 0x361);
    const auto stop = niteraid::original_survivor_presenter_frame(input, 242);
    CHECK(stop.counter == 8);
    CHECK(stop.sounds.size() == 1);
    CHECK(stop.sounds[0].action == SurvivorSoundAction::Stop);
    const auto aftermath = niteraid::original_survivor_presenter_frame(input, 306);
    CHECK(aftermath.sounds.size() == 2);
    CHECK(aftermath.sounds[1].sound == 0x35b);
    CHECK(niteraid::original_survivor_presenter_frame(input, 483).sounds[0].sound == 0x35d);
    CHECK(niteraid::original_survivor_presenter_frame(input, 590).sounds[0].sound == 0x35f);
    input.forced_failure_mask = 0;
    CHECK(niteraid::original_survivor_presenter_frame(input, 187).sounds[0].sound == 0x363);
    CHECK(niteraid::original_survivor_presenter_frame(input, 355).sounds[0].action == SurvivorSoundAction::Stop);
}

TEST_CASE("no survivor flyby updates before drawing and includes its terminal update")
{
    WorldState world {};
    world.survivor_intermission_timer_origin = 2583;
    world.survivor_intermission_rng_seed = 0xd130ed6d;
    const auto first = niteraid::original_no_survivor_presenter_frame(world);
    CHECK(first.x_fixed == 319 * 65536);
    CHECK(first.timer_tick == 2584);
    CHECK(first.counter == 543);
    CHECK(first.draws.size() == 5);
    CHECK(first.draws[0].sprite == 0x267);
    world.survivor_intermission_frame = 544;
    const auto last = niteraid::original_no_survivor_presenter_frame(world);
    CHECK(last.active);
    CHECK(last.counter == -1);
    CHECK(last.x_fixed == -225 * 65536);
    CHECK(last.random_seed == first.random_seed);
    world.survivor_intermission_frame = 545;
    CHECK(!niteraid::original_no_survivor_presenter_frame(world).active);
    CHECK(niteraid::original_no_survivor_presenter_frame(world).draws.empty());
}

TEST_CASE("presentation snapshot follows identity instead of object slot")
{
    auto previous = moving_world(10.0f, 20);
    Object other = previous.objects[0];
    other.presentation_id = 8;
    other.position.x = 80.0f;
    previous.objects.push_back(other);

    auto current = moving_world(82.0f, 21);
    current.objects[0].presentation_id = 8;
    Object moved = current.objects[0];
    moved.presentation_id = 7;
    moved.position.x = 12.0f;
    current.objects.push_back(moved);

    const auto halfway = make_presentation_world(previous, current, 0.5f);
    CHECK_NEAR(halfway.objects[0].position.x, 81.0f, 0.0001f);
    CHECK_NEAR(halfway.objects[1].position.x, 11.0f, 0.0001f);
}

TEST_CASE("presentation snapshot does not interpolate a reused object slot")
{
    const auto previous = moving_world(10.0f, 20);
    auto replacement = moving_world(12.0f, 21);
    replacement.objects[0].presentation_id = 9;

    const auto halfway = make_presentation_world(previous, replacement, 0.5f);
    CHECK_NEAR(halfway.objects[0].position.x, 12.0f, 0.0001f);
}

TEST_CASE("presentation snapshot rejects discontinuities and teleports")
{
    const auto previous = moving_world(10.0f, 20);

    auto spawned = moving_world(12.0f, 21);
    spawned.objects[0].active = false;
    CHECK_NEAR(make_presentation_world(previous, spawned, 0.5f).objects[0].position.x,
               12.0f, 0.0001f);

    auto teleported = moving_world(100.0f, 21);
    CHECK_NEAR(make_presentation_world(previous, teleported, 0.5f).objects[0].position.x,
               100.0f, 0.0001f);

    auto changed_screen = moving_world(12.0f, 21);
    changed_screen.screen = Screen::ControlPanel;
    const auto changed_screen_presented =
        make_presentation_world(previous, changed_screen, 0.5f);
    CHECK_NEAR(changed_screen_presented.objects[0].position.x, 12.0f, 0.0001f);
    CHECK_NEAR(changed_screen_presented.presentation_alpha, 1.0f, 0.0001f);

    auto skipped_tick = moving_world(12.0f, 22);
    CHECK_NEAR(make_presentation_world(previous, skipped_tick, 0.5f).objects[0].position.x,
               12.0f, 0.0001f);
}

TEST_CASE("presentation snapshot does not interpolate smart bomb callback transition")
{
    WorldState previous {};
    previous.screen = Screen::Gameplay;
    previous.gameplay_state = GameplayState::Active;
    previous.frame_tick = 20;

    Object armed {};
    armed.presentation_id = 9;
    armed.active = true;
    armed.type = ObjectType::SmartBomb;
    armed.position = {100.0f, 49.0f};
    armed.extent = {8.5f, 2.0f};
    previous.objects.push_back(armed);

    auto current = previous;
    current.frame_tick = 21;
    current.objects[0].position = {97.0f, 39.0f};
    current.objects[0].extent = {10.0f, 7.0f};

    const auto halfway = make_presentation_world(previous, current, 0.5f);
    CHECK_NEAR(halfway.objects[0].position.x, 97.0f, 0.0001f);
    CHECK_NEAR(halfway.objects[0].position.y, 39.0f, 0.0001f);
}
