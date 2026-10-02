#include "niteraid/game.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

using niteraid::Game;
using niteraid::InputState;

namespace {

struct ExpectedWave {
    std::uint32_t start_tick = 0;
    std::uint32_t next_trigger_tick = 0;
    std::uint16_t remaining = 0;
    std::uint16_t min_delay = 0;
    std::uint16_t max_delay = 0;
    std::uint16_t cadence = 0;
};

using LevelWaves = std::array<ExpectedWave, 5>;

constexpr std::array<LevelWaves, 12> kExpected {{
    LevelWaves {{ExpectedWave {1, 51, 10, 400, 500, 80}, {}, {}, {}, {}}},
    LevelWaves {{ExpectedWave {501, 551, 7, 400, 500, 80}, ExpectedWave {1, 280, 5, 500, 1000, 70}, {}, {}, {}}},
    LevelWaves {{ExpectedWave {1, 51, 10, 400, 500, 80}, ExpectedWave {1, 168, 5, 300, 300, 60}, {}, {}, {}}},
    LevelWaves {{ExpectedWave {601, 651, 15, 400, 500, 70}, ExpectedWave {601, 740, 10, 250, 300, 50}, {}, {}, ExpectedWave {1, 310, 3, 400, 500, 0}}},
    LevelWaves {{{}, ExpectedWave {1, 32, 10, 250, 300, 45}, ExpectedWave {1, 196, 16, 350, 500, 70}, ExpectedWave {1, 194, 10, 250, 300, 45}, ExpectedWave {1, 266, 4, 400, 500, 0}}},
    LevelWaves {{{}, ExpectedWave {1, 32, 10, 250, 300, 40}, ExpectedWave {1, 168, 17, 300, 500, 70}, ExpectedWave {1, 194, 10, 250, 300, 40}, ExpectedWave {1, 266, 5, 400, 500, 0}}},
    LevelWaves {{{}, ExpectedWave {1, 32, 12, 250, 300, 30}, ExpectedWave {1, 168, 10, 300, 500, 60}, ExpectedWave {1, 194, 12, 250, 300, 30}, ExpectedWave {1, 266, 7, 400, 1000, 0}}},
    LevelWaves {{{}, ExpectedWave {1, 32, 13, 250, 300, 30}, ExpectedWave {1, 168, 10, 300, 500, 60}, ExpectedWave {1, 194, 13, 250, 300, 30}, ExpectedWave {1, 100, 9, 150, 900, 0}}},
    LevelWaves {{ExpectedWave {1, 76, 5, 600, 800, 40}, ExpectedWave {1, 112, 15, 200, 300, 20}, ExpectedWave {1, 465, 5, 600, 800, 40}, ExpectedWave {1, 133, 15, 200, 300, 20}, ExpectedWave {1, 28, 20, 100, 400, 0}}},
    LevelWaves {{ExpectedWave {1, 76, 6, 600, 800, 40}, ExpectedWave {1, 112, 15, 200, 300, 20}, ExpectedWave {1, 465, 6, 600, 800, 40}, ExpectedWave {1, 133, 15, 200, 300, 20}, ExpectedWave {1, 22, 20, 80, 400, 0}}},
    LevelWaves {{ExpectedWave {1, 76, 7, 600, 800, 40}, ExpectedWave {1, 112, 15, 200, 300, 20}, ExpectedWave {1, 465, 7, 600, 800, 40}, ExpectedWave {1, 133, 15, 200, 300, 20}, ExpectedWave {1, 11, 20, 40, 200, 0}}},
    LevelWaves {{ExpectedWave {1, 76, 8, 600, 800, 40}, ExpectedWave {1, 112, 15, 200, 300, 20}, ExpectedWave {1, 465, 8, 600, 800, 40}, ExpectedWave {1, 133, 15, 200, 300, 20}, ExpectedWave {1, 9, 20, 30, 150, 0}}},
}};

}  // namespace

TEST_CASE("normal levels import the recovered five-bank wave table")
{
    for (std::uint16_t level = 0; level < kExpected.size(); ++level) {
        Game game(level);
        game.tick(InputState {});
        const auto& banks = game.world().wave_banks;
        for (std::size_t bank = 0; bank < banks.size(); ++bank) {
            const auto& actual = banks[bank];
            const auto& expected = kExpected[level][bank];
            CHECK_EQ(actual.start_tick, expected.start_tick);
            CHECK_EQ(actual.next_trigger_tick, expected.next_trigger_tick);
            CHECK_EQ(actual.live_next_trigger_tick, expected.next_trigger_tick);
            CHECK_EQ(actual.remaining_spawns, expected.remaining);
            CHECK_EQ(actual.min_delay, expected.min_delay);
            CHECK_EQ(actual.max_delay, expected.max_delay);
            CHECK_EQ(actual.paratrooper_cadence, expected.cadence);
        }
    }
}

TEST_CASE("diagnostic pre-level seed reproduces natural original first wave")
{
    constexpr std::uint32_t kOriginalPreLevelSeed = 1086628953u;
    Game game(std::uint16_t {0}, std::nullopt, false, false, false, false,
              kOriginalPreLevelSeed);

    CHECK_EQ(game.world().gameplay_rng_seed, 1930634350u);
    CHECK_EQ(game.world().wave_banks[0].next_trigger_tick, 360u);

    while (game.world().frame_tick < 359) {
        game.tick(InputState {});
    }
    const auto aircraft_count = [&game]() {
        return std::count_if(game.world().objects.begin(), game.world().objects.end(),
                             [](const auto& object) {
                                 return object.active &&
                                        object.type == niteraid::ObjectType::Aircraft;
                             });
    };
    CHECK_EQ(aircraft_count(), 0);

    game.tick(InputState {});
    CHECK_EQ(aircraft_count(), 1);
}

TEST_CASE("captured first aircraft follows DOS level-entry update index")
{
    constexpr std::uint32_t kCapturedPreLevelSeed = 4288041665u;
    Game game(std::uint16_t {0}, std::nullopt, false, false, false, false,
              kCapturedPreLevelSeed);
    CHECK_EQ(game.world().frame_tick, 1u);
    CHECK_EQ(game.world().wave_banks[0].next_trigger_tick, 51u);

    const auto aircraft_count = [&game]() {
        return std::count_if(game.world().objects.begin(), game.world().objects.end(),
                             [](const auto& object) {
                                 return object.active &&
                                        object.type == niteraid::ObjectType::Aircraft;
                             });
    };
    for (std::uint32_t update = 0; update < 49; ++update) {
        game.tick(InputState {});
    }
    CHECK_EQ(aircraft_count(), 0);
    game.tick(InputState {});
    CHECK_EQ(aircraft_count(), 1);
    CHECK_EQ(game.world().frame_tick, 51u);
    CHECK_EQ(game.world().gameplay_rng_seed, 3347423727u);
}

TEST_CASE("final special level skips normal wave banks")
{
    Game game(std::uint16_t {12});
    game.tick(InputState {});
    for (const auto& bank : game.world().wave_banks) {
        CHECK_EQ(bank.remaining_spawns, 0u);
        CHECK_EQ(bank.min_delay, 0u);
        CHECK_EQ(bank.max_delay, 0u);
        CHECK_EQ(bank.paratrooper_cadence, 0u);
    }
    CHECK_EQ(game.world().finale_spawn_budget_remaining, 9u);
    CHECK_EQ(game.world().finale_spawn_budget_total, 9u);
}

TEST_CASE("level import allocates only nonempty wave controllers in bank order")
{
    for (std::uint16_t level = 0; level < kExpected.size(); ++level) {
        Game game(level);
        const auto& world = game.world();
        std::size_t slot = 1;
        for (std::size_t bank = 0; bank < kExpected[level].size(); ++bank) {
            if (kExpected[level][bank].remaining == 0) {
                continue;
            }
            CHECK(slot < world.objects.size());
            if (slot < world.objects.size()) {
                CHECK(world.objects[slot].active);
                CHECK(world.objects[slot].type == niteraid::ObjectType::WaveController);
                CHECK_EQ(world.objects[slot].wave_bank, bank);
            }
            ++slot;
        }
        CHECK_EQ(world.objects.size(), slot);
        CHECK_EQ(world.object_highwater.value(), slot);
    }
    Game finale(std::uint16_t {12});
    CHECK_EQ(finale.world().objects.size(), 2u);
    CHECK(finale.world().objects[1].type == niteraid::ObjectType::FinaleController);
}

TEST_CASE("wave window retirement preserves budget and reuses the tail slot")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.wave_banks[0].stop_tick = 3;
    world.wave_banks[0].live_next_trigger_tick = 100;
    const auto remaining = world.wave_banks[0].remaining_spawns;
    const auto seed = world.gameplay_rng_seed;
    game.tick(InputState {});
    CHECK(world.objects[1].active);
    game.tick(InputState {});
    CHECK(!world.objects[1].active);
    CHECK_EQ(world.object_highwater.value(), 1u);
    CHECK_EQ(world.wave_banks[0].remaining_spawns, remaining);
    CHECK_EQ(world.gameplay_rng_seed, seed);

    world.objects[0].timer = 9;
    InputState fire {};
    fire.fire = true;
    game.tick(fire);
    CHECK(world.objects[1].active);
    CHECK(world.objects[1].type == niteraid::ObjectType::PlayerProjectile);
    CHECK_EQ(world.object_highwater.value(), 2u);
}

TEST_CASE("depleted bank controller reschedules until global wave exhaustion")
{
    Game game(std::uint16_t {1});
    auto& world = game.diagnostic_world();
    world.wave_banks[0].remaining_spawns = 0;
    world.wave_banks[0].start_tick = 1;
    world.wave_banks[0].live_next_trigger_tick = 2;
    CHECK(!world.waves_exhausted);
    game.tick(InputState {});
    CHECK(world.objects[1].active);
    CHECK_EQ(world.wave_banks[0].remaining_spawns, 0u);
    CHECK(world.wave_banks[0].live_next_trigger_tick > world.frame_tick);
    CHECK(std::any_of(world.objects.begin(), world.objects.end(), [](const auto& object) {
        return object.active && object.type == niteraid::ObjectType::Aircraft;
    }));
}

TEST_CASE("global wave exhaustion retires controllers on the next update")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.wave_banks[0].remaining_spawns = 1;
    world.wave_banks[0].live_next_trigger_tick = 2;
    game.tick(InputState {});
    CHECK(world.waves_exhausted);
    CHECK(world.objects[1].active);
    const auto seed = world.gameplay_rng_seed;
    game.tick(InputState {});
    CHECK(!world.objects[1].active);
    CHECK_EQ(world.gameplay_rng_seed, seed);
}

TEST_CASE("right-edge smart bomb moves into view on its birth update")
{
    constexpr std::uint32_t kCapturedPreSpawnSeed = 132058594u;
    Game game(std::uint16_t {3});
    auto& world = game.diagnostic_world();
    world.gameplay_rng_seed = kCapturedPreSpawnSeed;
    world.wave_banks[4].start_tick = 0;
    world.wave_banks[4].live_next_trigger_tick = world.frame_tick + 1;

    game.tick(InputState {});

    const auto bomb = std::find_if(world.objects.begin(), world.objects.end(), [](const auto& object) {
        return object.active && object.type == niteraid::ObjectType::SmartBomb;
    });
    CHECK(bomb != world.objects.end());
    if (bomb == world.objects.end()) {
        return;
    }
    CHECK_EQ(bomb->position.x, 319.0f);
    CHECK_EQ(bomb->timer, 185u);
    CHECK_EQ(world.object_highwater.value(), 5u);
}

TEST_CASE("right-edge smart bomb moving outward retires before countdown")
{
    Game game(std::uint16_t {3});
    auto& world = game.diagnostic_world();
    world.objects.clear();
    world.object_highwater.reset();
    world.wave_banks = {};
    niteraid::Object bomb {};
    bomb.active = true;
    bomb.type = niteraid::ObjectType::SmartBomb;
    bomb.position = {319.0f, 49.0f};
    bomb.velocity = {1.0f, 0.0f};
    bomb.extent = {8.5f, 2.0f};
    bomb.timer = 186;
    world.objects.push_back(bomb);

    game.tick(InputState {});

    CHECK(!world.objects[0].active);
    CHECK_EQ(world.objects[0].position.x, 320.0f);
    CHECK_EQ(world.objects[0].timer, 186u);
}

TEST_CASE("natural landed pressure advances the live aircraft deadline")
{
    constexpr std::uint32_t kCapturedPreLevelSeed = 4288041665u;
    Game game(std::uint16_t {0}, std::nullopt, false, false, false, false,
              kCapturedPreLevelSeed);
    while (game.world().frame_tick < 1053u) {
        game.tick(InputState {});
    }
    CHECK_EQ(game.world().wave_banks[0].live_next_trigger_tick, 1429u);
    const auto seed = game.world().gameplay_rng_seed;
    const auto remaining = game.world().wave_banks[0].remaining_spawns;
    for (std::uint32_t held = 1; held <= 3; ++held) {
        game.tick(InputState {});
        CHECK_EQ(game.world().wave_banks[0].live_next_trigger_tick, 1429u + held);
        CHECK_EQ(game.world().wave_banks[0].remaining_spawns, remaining);
        CHECK_EQ(game.world().gameplay_rng_seed, seed);
    }
}

TEST_CASE("landed pressure pauses all active bank deadlines without spending budgets or RNG")
{
    Game game(std::uint16_t {7});
    auto& world = game.diagnostic_world();
    niteraid::Object aircraft {};
    aircraft.active = true;
    aircraft.type = niteraid::ObjectType::Aircraft;
    aircraft.aircraft_variant = niteraid::AircraftVariant::B;
    aircraft.wave_bank = 2;
    aircraft.position = {100.0f, 40.0f};
    world.objects.push_back(aircraft);
    const auto first_invader_slot = world.objects.size();
    for (float x : {40.0f, 80.0f, 240.0f}) {
        niteraid::Object invader {};
        invader.active = true;
        invader.type = niteraid::ObjectType::LandedInvader;
        invader.position = {x, 172.0f};
        invader.extent = {2.0f, 3.5f};
        world.objects.push_back(invader);
    }
    const auto before = world.wave_banks;
    const auto seed = world.gameplay_rng_seed;
    game.tick(InputState {});
    for (std::size_t bank = 0; bank < before.size(); ++bank) {
        const auto& actual = world.wave_banks[bank];
        CHECK_EQ(actual.remaining_spawns, before[bank].remaining_spawns);
        CHECK_EQ(actual.next_trigger_tick, before[bank].next_trigger_tick);
        CHECK_EQ(actual.live_next_trigger_tick, before[bank].live_next_trigger_tick +
                 (before[bank].remaining_spawns != 0 ? 1u : 0u));
    }
    CHECK_EQ(world.gameplay_rng_seed, seed);
    world.objects[first_invader_slot].active = false;
    const auto held = world.wave_banks;
    game.tick(InputState {});
    for (std::size_t bank = 0; bank < held.size(); ++bank) {
        CHECK_EQ(world.wave_banks[bank].live_next_trigger_tick, held[bank].live_next_trigger_tick);
    }
    CHECK_EQ(world.gameplay_rng_seed, seed);
}
