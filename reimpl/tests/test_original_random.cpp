#include "niteraid/game.hpp"
#include "niteraid/original_random.hpp"
#include "niteraid/state_serializer.hpp"
#include "niteraid/survivor_presenter.hpp"
#include "test_harness.hpp"

#include <algorithm>

using namespace niteraid;

TEST_CASE("Borland startup palette draws reproduce the clean original RNG state")
{
    std::uint32_t seed = 1;
    CHECK_EQ(original_random_next(seed), 346);
    CHECK_EQ(seed, 22695478u);
    seed = original_palette_random_seed();
    CHECK_EQ(seed, 0xff9652c1u);
    for (const auto expected : {4143, 18309, 25351, 21779, 8857, 406, 20300}) {
        CHECK_EQ(original_random_next(seed), expected);
    }
    CHECK_EQ(seed, 3477896312u);
}

TEST_CASE("Borland bounded draws have exclusive upper bounds and consume zero-width ranges")
{
    auto seed = original_palette_random_seed();
    CHECK_EQ(original_random_bounded(seed, 400), 50);
    CHECK_EQ(original_random_bounded(seed, 100), 55);
    CHECK_EQ(original_random_bounded(seed, 100), 77);
    const auto before = seed;
    CHECK_EQ(original_random_bounded(seed, 0), 0);
    auto expected = before;
    original_random_next(expected);
    CHECK_EQ(seed, expected);
    CHECK(seed != before);
}

TEST_CASE("debris fixed-point random spread retains DOS signed multiply overflow")
{
    CHECK_EQ(original_random_fixed_value(0, 3, 0x8000), -32768);
    CHECK_EQ(original_random_fixed_value(10923, 3, 0x8000), -98304);
    CHECK_EQ(original_random_fixed_value(21845, 3, 0x8000), -32770);
    CHECK_EQ(original_random_fixed_value(21846, 3, 0x8000), -32764);
    CHECK_EQ(original_random_fixed_value(32767, 1, -0x4000), 81920);
}

TEST_CASE("live waves consume the shared RNG and preserve it in Tier 3 dumps")
{
    Game game(std::uint16_t {0});
    CHECK_EQ(game.world().gameplay_rng_seed, 2419060726u);
    for (int tick = 1; tick <= 506; ++tick) {
        game.tick(InputState {});
        if (tick == 51) {
            CHECK_EQ(game.world().wave_banks[0].live_next_trigger_tick, 506u);
            CHECK_EQ(game.world().gameplay_rng_seed, 3347423727u);
        }
    }
    CHECK_EQ(game.world().wave_banks[0].live_next_trigger_tick, 983u);
    CHECK_EQ(game.world().gameplay_rng_seed, 3808909436u);
    const auto bytes = serialize_world_state_dat2730(game.world());
    const auto seed = static_cast<std::uint32_t>(bytes[0x1ee8]) |
        (static_cast<std::uint32_t>(bytes[0x1ee9]) << 8u) |
        (static_cast<std::uint32_t>(bytes[0x1eea]) << 16u) |
        (static_cast<std::uint32_t>(bytes[0x1eeb]) << 24u);
    CHECK_EQ(seed, game.world().gameplay_rng_seed);
}

TEST_CASE("hitting the finale controller halves its timer without consuming RNG")
{
    Game game(std::uint16_t {12});
    auto& world = game.diagnostic_world();
    world.objects.clear();
    Object controller {};
    controller.active = true;
    controller.type = ObjectType::FinaleController;
    controller.position = {100, 50};
    controller.extent = {14, 8};
    controller.timer = 200;
    world.objects.push_back(controller);
    Object shot {};
    shot.active = true;
    shot.type = ObjectType::PlayerProjectile;
    shot.position = {102, 52};
    shot.extent = {1, 1};
    world.objects.push_back(shot);
    const auto seed = world.gameplay_rng_seed;
    game.tick(InputState {});
    CHECK_EQ(world.objects[0].timer, 100u);
    CHECK_EQ(world.gameplay_rng_seed, seed);
    CHECK(std::any_of(world.objects.begin(), world.objects.end(), [](const auto& actor) {
        return actor.active && actor.type == ObjectType::Paratrooper && actor.finale_drop &&
               actor.assault_stage == 1 && actor.finale_hits_remaining == 0;
    }));
}

TEST_CASE("small aircraft enters at negative sprite width without a capture-specific offset")
{
    Game game(std::uint16_t {7});
    while (game.world().frame_tick < 324) {
        game.tick(InputState {});
    }
    CHECK_EQ(game.world().gameplay_rng_seed, 1372546996u);
    CHECK_EQ(game.world().wave_banks[1].live_next_trigger_tick, 576u);
    const auto& actors = game.world().objects;
    const auto aircraft = std::find_if(actors.begin(), actors.end(), [](const auto& actor) {
        return actor.active && !actor.pending_destroy && actor.type == ObjectType::Aircraft &&
               actor.aircraft_variant == AircraftVariant::A && actor.velocity.x > 0;
    });
    CHECK(aircraft != actors.end());
    if (aircraft != actors.end()) CHECK_EQ(aircraft->position.x, 22.0f);
}

TEST_CASE("aircraft breakup consumes fourteen shared random draws including sprite selection")
{
    for (const auto variant : {AircraftVariant::D, AircraftVariant::A}) {
        Game game(std::uint16_t {0});
        auto& world = game.diagnostic_world();
        world.objects.clear();
        Object shell {};
        shell.active = true;
        shell.type = ObjectType::AircraftDebris;
        shell.aircraft_variant = variant;
        shell.position = {100, 30};
        shell.velocity = {1, 0};
        shell.direction = 1;
        shell.timer = 10;
        shell.assault_stage = variant == AircraftVariant::D ? 9 : 5;
        world.objects.push_back(shell);
        auto expected = world.gameplay_rng_seed;
        const auto dx = original_random_fixed(expected, 3, 0x8000) / 65536.0f;
        const auto dy = original_random_fixed(expected, 1, -0x4000) / 65536.0f;
        for (int draw = 2; draw < 14; ++draw) original_random_next(expected);
        game.tick(InputState {});
        CHECK_EQ(world.gameplay_rng_seed, expected);
        CHECK_EQ(std::count_if(world.objects.begin(), world.objects.end(), [](const auto& actor) {
            return actor.active && !actor.pending_destroy && actor.type == ObjectType::AircraftDebris;
        }), 4);
        const auto first = std::find_if(world.objects.begin(), world.objects.end(), [](const auto& actor) {
            return actor.active && !actor.pending_destroy && actor.type == ObjectType::AircraftDebris;
        });
        if (first != world.objects.end()) {
            // Native +1 is original direction flag 0, not flag 1.
            const float offset = variant == AircraftVariant::D ? 44.0f : 10.0f;
            CHECK_NEAR(first->position.x, 102.0f + offset + dx * 6, 0.0001f);
            CHECK_NEAR(first->velocity.x, 1 + dx, 0.0001f);
            CHECK_NEAR(first->velocity.y, dy + 0.03125f, 0.0001f);
        }
    }
}

TEST_CASE("survivor presenter inherits and returns the shared RNG without an extra draw")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    Object trooper {};
    trooper.active = true;
    trooper.type = ObjectType::LandedInvader;
    trooper.position = {120, 172};
    world.objects.push_back(trooper);
    world.object_highwater = world.objects.size();
    world.gameplay_state = GameplayState::LevelComplete;
    const auto entry_seed = world.gameplay_rng_seed;
    game.tick(InputState {});
    CHECK_EQ(world.survivor_intermission_rng_seed, entry_seed);
    for (int tick = 0; tick < 200; ++tick) {
        game.tick(InputState {});
        game.prepare_gameplay_page();
    }
    CHECK_EQ(world.gameplay_rng_seed, original_survivor_presenter_frame(world).random_seed);
    CHECK(world.gameplay_rng_seed != entry_seed);
}
