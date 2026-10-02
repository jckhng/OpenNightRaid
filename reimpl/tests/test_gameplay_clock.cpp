#include "niteraid/gameplay_clock.hpp"
#include "niteraid/game_internals.hpp"
#include "niteraid/state_serializer.hpp"
#include "test_harness.hpp"

#include <vector>

using namespace niteraid;

TEST_CASE("aircraft updater does not advance draw-owned D and A rotor phases")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    for (const auto variant : {AircraftVariant::D, AircraftVariant::A}) {
        Object aircraft {};
        aircraft.active = true;
        aircraft.type = ObjectType::Aircraft;
        aircraft.aircraft_variant = variant;
        aircraft.wave_bank = variant == AircraftVariant::D ? 0 : 1;
        aircraft.position = {40.0f, 20.0f};
        aircraft.frame = 0;
        world.objects.push_back(aircraft);
    }
    const auto advanced = advance_gameplay_batch(game, {}, 2, [](const WorldState&) {});
    CHECK_EQ(advanced, 2u);
    CHECK_EQ(game.world().objects[0].frame, 0);
    CHECK_EQ(game.world().objects[1].frame, 0);
    CHECK_EQ(game.world().objects[0].timer, 2u);
    const auto clock = game.world().frame_tick;
    const auto seed = game.world().gameplay_rng_seed;
    const auto sounds = game.world().sound_events;
    game.prepare_gameplay_page();
    CHECK_EQ(game.world().objects[0].frame, 1);
    CHECK_EQ(game.world().objects[1].frame, 1);
    CHECK_EQ(internals::aircraft_rotor_frame(game.world().objects[0], clock), 1);
    CHECK_EQ(internals::aircraft_rotor_frame(game.world().objects[1], clock), 0);
    CHECK_EQ(game.world().frame_tick, clock);
    CHECK_EQ(game.world().gameplay_rng_seed, seed);
    CHECK(game.world().sound_events == sounds);
    CHECK_EQ(game.world().objects[0].timer, 2u);
    CHECK_EQ(advance_gameplay_batch(game, {}, 2, [](const WorldState&) {}), 2u);
    CHECK_EQ(game.world().objects[0].frame, 1);
    game.prepare_gameplay_page();
    CHECK_EQ(game.world().objects[0].frame, 2);
    CHECK_EQ(game.world().objects[1].frame, 2);
}

TEST_CASE("gameplay page preparation preserves frozen owners and skipped aircraft")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    for (const auto variant : {AircraftVariant::D, AircraftVariant::A, AircraftVariant::B, AircraftVariant::C}) {
        Object aircraft {};
        aircraft.active = true;
        aircraft.type = ObjectType::Aircraft;
        aircraft.aircraft_variant = variant;
        aircraft.frame = variant == AircraftVariant::D ? 4 : 3;
        world.objects.push_back(aircraft);
    }
    game.prepare_gameplay_page();
    CHECK_EQ(world.objects[0].frame, 0);
    CHECK_EQ(world.objects[1].frame, 0);
    CHECK_EQ(internals::aircraft_rotor_frame(world.objects[1], world.frame_tick), 3);
    CHECK_EQ(world.objects[2].frame, 3);
    CHECK_EQ(world.objects[3].frame, 3);
    world.objects[0].active = false;
    world.objects[1].pending_destroy = true;
    game.prepare_gameplay_page();
    CHECK_EQ(world.objects[0].frame, 0);
    CHECK_EQ(world.objects[1].frame, 0);
    world.objects[0].active = true;
    world.objects[1].pending_destroy = false;
    for (const auto screen : {Screen::ControlPanel, Screen::Intermission, Screen::GameOver, Screen::Title}) {
        world.screen = screen;
        game.prepare_gameplay_page();
        CHECK_EQ(world.objects[0].frame, 0);
        CHECK_EQ(world.objects[1].frame, 0);
    }
    world.screen = Screen::Gameplay;
    world.player_dead = true;
    game.prepare_gameplay_page();
    CHECK_EQ(world.objects[0].frame, 0);
    world.player_dead = false;
    world.gameplay_state = GameplayState::ScriptedSequence;
    game.prepare_gameplay_page();
    CHECK_EQ(world.objects[1].frame, 0);
}

TEST_CASE("converted A carrier draws its pre-modulo rotor selector")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    Object carrier {};
    carrier.active = true;
    carrier.type = ObjectType::AircraftDebris;
    carrier.aircraft_variant = AircraftVariant::A;
    carrier.frame = 4;
    world.objects.push_back(carrier);

    game.prepare_gameplay_page();

    CHECK_EQ(world.objects[0].frame, 1);
    CHECK_EQ(internals::aircraft_rotor_frame(world.objects[0], world.frame_tick), 4);
}

TEST_CASE("fatal actor page advances aircraft before palette-only retention")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    for (const auto variant : {AircraftVariant::D, AircraftVariant::A}) {
        Object aircraft {};
        aircraft.active = true;
        aircraft.type = ObjectType::Aircraft;
        aircraft.aircraft_variant = variant;
        aircraft.frame = 0;
        world.objects.push_back(aircraft);
    }
    world.player_dead = true;
    world.gameplay_state = GameplayState::GameOver;
    world.transition_started_tick = world.frame_tick;
    world.death_flash_frames_remaining = 15;
    const auto clock = world.frame_tick;
    const auto seed = world.gameplay_rng_seed;
    game.prepare_gameplay_page();
    CHECK_EQ(world.objects[0].frame, 1);
    CHECK_EQ(world.objects[1].frame, 1);
    CHECK_EQ(world.frame_tick, clock);
    CHECK_EQ(world.gameplay_rng_seed, seed);

    world.fatal_retained_page = RetainedPage {world.objects, clock};
    game.prepare_gameplay_page();
    CHECK_EQ(world.objects[0].frame, 1);
    CHECK_EQ(world.fatal_retained_page->objects[0].frame, 1);
    world.fatal_retained_page.reset();
    ++world.frame_tick;
    game.prepare_gameplay_page();
    CHECK_EQ(world.objects[1].frame, 1);
    world.transition_started_tick = world.frame_tick;
    world.death_flash_frames_remaining = 0;
    game.prepare_gameplay_page();
    CHECK_EQ(world.objects[0].frame, 1);
}

TEST_CASE("gameplay wait is anchored before drawing and budgets the next frame")
{
    GameplayClock clock;
    CHECK_EQ(clock.update_budget(), 1u);
    clock.begin(100);
    CHECK_EQ(clock.deadline(), 101u);
    clock.finish(104);
    CHECK_EQ(clock.deadline(), 101u);
    CHECK_EQ(clock.update_budget(), 4u);
    clock.begin(104);
    CHECK_EQ(clock.deadline(), 105u);
    clock.finish(105);
    CHECK_EQ(clock.update_budget(), 1u);
    clock.begin(105);
    clock.finish(10000);
    CHECK_EQ(clock.update_budget(), 6u);
    clock.reset();
    CHECK_EQ(clock.update_budget(), 1u);
}

TEST_CASE("enhanced catch-up consumes each menu make once while retaining held controls")
{
    for (const bool held_arrow : {false, true}) {
        Game game(std::nullopt, std::nullopt, false, false, true);
        game.tick(InputState {.start = true});
        game.tick({});
        CHECK(game.world().screen == Screen::ControlPanel);
        CHECK_EQ(game.world().control_panel_row, 3);
        InputState input {};
        input.move_up = held_arrow;
        input.menu_key_action = MenuKeyAction::Up;
        input.menu_navigation_up_repeat = true;
        input.start_repeat = true;
        for (int tick = 0; tick < 4; ++tick) {
            game.tick(input);
            consume_menu_key_events(input);
            CHECK_EQ(game.world().control_panel_row, 2);
            CHECK_EQ(input.move_up, held_arrow);
            CHECK(!input.menu_navigation_up_repeat);
            CHECK(!input.start_repeat);
        }
        game.tick({});
        input.menu_key_action = MenuKeyAction::Down;
        input.menu_navigation_down_repeat = true;
        game.tick(input);
        consume_menu_key_events(input);
        CHECK_EQ(game.world().control_panel_row, 3);
        CHECK(!input.menu_navigation_down_repeat);
    }
}

TEST_CASE("batched gameplay preserves every tick and dispatches audio once per update")
{
    Game batched(std::uint16_t {0}, std::nullopt);
    Game scalar(std::uint16_t {0}, std::nullopt);
    InputState input {};
    input.fire = true;
    for (auto budget : {1u, 6u, 4u, 2u, 6u}) {
        std::vector<std::vector<std::uint16_t>> actual, expected;
        const auto first = batched.world().frame_tick;
        const auto advanced = advance_gameplay_batch(batched, input, budget, [&](const WorldState& world) {
            CHECK_EQ(world.frame_tick, first + actual.size() + 1);
            actual.push_back(world.sound_events);
        });
        CHECK_EQ(advanced, budget);
        for (unsigned i = 0; i < budget; ++i) {
            scalar.tick(input);
            expected.push_back(scalar.world().sound_events);
        }
        CHECK(actual == expected);
        CHECK_EQ(batched.world().frame_tick, scalar.world().frame_tick);
        CHECK_EQ(batched.world().gameplay_rng_seed, scalar.world().gameplay_rng_seed);
        CHECK_EQ(batched.world().scores.score, scalar.world().scores.score);
        CHECK_EQ(batched.world().objects.size(), scalar.world().objects.size());
        CHECK(serialize_world_state_dat2730(batched.world()) == serialize_world_state_dat2730(scalar.world()));
    }
}

TEST_CASE("gameplay batching stops at menu warp and overrun ownership boundaries")
{
    Game menu(std::uint16_t {0}, std::nullopt);
    InputState escape {};
    escape.escape = true;
    CHECK_EQ(advance_gameplay_batch(menu, escape, 6, [](const WorldState&) {}), 1u);
    CHECK(menu.world().screen == Screen::ControlPanel);
    CHECK_EQ(advance_gameplay_batch(menu, {}, 6, [](const WorldState&) {}), 1u);

    Game warp(std::uint16_t {0}, std::nullopt);
    InputState input {};
    input.control_modifier = true;
    input.alt_modifier = true;
    input.level_warp_digit = 8;
    CHECK_EQ(advance_gameplay_batch(warp, input, 6, [](const WorldState&) {}), 1u);
    CHECK_EQ(warp.world().current_level, 7u);
    CHECK_EQ(advance_gameplay_batch(warp, input, 6, [](const WorldState&) {}), 1u);

    Game overrun(std::uint16_t {0}, std::nullopt);
    while (overrun.world().frame_tick < 2013) {
        overrun.tick({});
    }
    CHECK_EQ(advance_gameplay_batch(overrun, {}, 6, [](const WorldState&) {}), 1u);
    CHECK(overrun.before_owner_page().has_value());
    CHECK(overrun.world().bunker_assault_active);
    CHECK_EQ(overrun.world().frame_tick, 2014u);
}

TEST_CASE("update batches clamp pathological budgets and leave scripted owners scalar")
{
    Game game(std::uint16_t {0}, std::nullopt);
    CHECK_EQ(advance_gameplay_batch(game, {}, 0, [](const WorldState&) {}), 1u);
    CHECK_EQ(advance_gameplay_batch(game, {}, 100, [](const WorldState&) {}), 6u);
    for (const auto screen : {Screen::Intermission, Screen::GameOver, Screen::HighScoreEntry,
                              Screen::ControlPanel, Screen::Finale, Screen::SharewareEnding}) {
        auto world = game.world();
        world.screen = screen;
        CHECK(!uses_gameplay_clock(world));
    }
    auto world = game.world();
    world.gameplay_state = GameplayState::ScriptedSequence;
    CHECK(!uses_gameplay_clock(world));
}

TEST_CASE("fatal takeover cannot consume the remaining gameplay batch")
{
    Game game(std::uint16_t {7}, std::nullopt);
    while (game.world().frame_tick < 463) {
        game.tick({});
    }
    CHECK_EQ(advance_gameplay_batch(game, {}, 6, [](const WorldState&) {}), 1u);
    CHECK(game.world().player_dead);
    CHECK_EQ(game.world().frame_tick, 464u);
    CHECK_EQ(advance_gameplay_batch(game, {}, 6, [](const WorldState&) {}), 1u);
    CHECK_EQ(game.world().frame_tick, 465u);
}
