// Tier 3 serializer: WorldState → bytes laid out in the DAT_2730_*
// scheme that the Python schema (tools/compare/dat2730_schema.py)
// reads. These tests confirm the C++ side writes the same offsets the
// Python side expects to read.

#include "niteraid/game.hpp"
#include "niteraid/state_serializer.hpp"
#include "test_harness.hpp"

#include <cstdint>

using niteraid::Game;
using niteraid::InputState;
using niteraid::kStateDumpSize;
using niteraid::serialize_world_state_dat2730;

namespace {

std::uint16_t read_u16(const std::vector<std::uint8_t>& b, std::size_t off)
{
    return static_cast<std::uint16_t>(b[off] | (b[off + 1] << 8));
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& b, std::size_t off)
{
    return static_cast<std::uint32_t>(b[off])
         | (static_cast<std::uint32_t>(b[off + 1]) << 8)
         | (static_cast<std::uint32_t>(b[off + 2]) << 16)
         | (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

}  // namespace

TEST_CASE("serializer buffer is at least kStateDumpSize bytes")
{
    Game game(std::uint16_t{0}, std::nullopt);
    game.tick(InputState{});
    const auto bytes = serialize_world_state_dat2730(game.world());
    CHECK_EQ(bytes.size(), kStateDumpSize);
}

TEST_CASE("level 1 first update: HUD counters all zero at the recovered offsets")
{
    Game game(std::uint16_t{0}, std::nullopt);
    game.tick(InputState{});
    const auto bytes = serialize_world_state_dat2730(game.world());
    CHECK_EQ(read_u32(bytes, 0x2056), 0u);  // score
    CHECK_EQ(read_u16(bytes, 0x205a), 0u);  // grounded resolutions
    CHECK_EQ(read_u16(bytes, 0x205c), 0u);  // enemy kills
    CHECK_EQ(read_u16(bytes, 0x205e), 0u);  // current_level
}

TEST_CASE("level 1 first update: gameplay_state = 0 (Active)")
{
    Game game(std::uint16_t{0}, std::nullopt);
    game.tick(InputState{});
    const auto bytes = serialize_world_state_dat2730(game.world());
    CHECK_EQ(read_u16(bytes, 0x4dc4), 0u);
    CHECK_EQ(read_u32(bytes, 0x4dc6), 2u);
}

TEST_CASE("IBCD flag serializes at the original DAT_2730_4de4 offset")
{
    Game game(std::uint16_t {0}, std::nullopt, true);
    const auto bytes = serialize_world_state_dat2730(game.world());
    CHECK_EQ(read_u16(bytes, 0x4de4), 1u);
}

TEST_CASE("intermission projects the counter advanced by the original outer round owner")
{
    niteraid::WorldState world {};
    for (const auto screen : {niteraid::Screen::Gameplay, niteraid::Screen::Intermission,
                             niteraid::Screen::GameOver, niteraid::Screen::Finale}) {
        for (const std::uint16_t level : {0, 3, 7, 12}) {
            world.screen = screen;
            world.current_level = level;
            const auto bytes = serialize_world_state_dat2730(world);
            CHECK_EQ(read_u16(bytes, 0x205e), level + (screen == niteraid::Screen::Intermission ? 1 : 0));
            CHECK_EQ(world.current_level, level);
        }
    }
}

TEST_CASE("level 1 first update: recovered audio defaults match original active dump")
{
    Game game(std::uint16_t{0}, std::nullopt);
    game.tick(InputState{});
    const auto bytes = serialize_world_state_dat2730(game.world());
    CHECK_EQ(read_u16(bytes, 0x1ff8), 3u);  // sound mode
    CHECK_EQ(read_u16(bytes, 0x1ffa), 2u);  // voice count
    CHECK_EQ(read_u16(bytes, 0x1ffc), 1u);  // music toggle
    CHECK_EQ(read_u16(bytes, 0x1ffe), 1u);  // sound device pref
}

TEST_CASE("menu state fields mirror original control-panel globals")
{
    Game title_game;
    title_game.tick(InputState {});
    InputState escape {};
    escape.escape = true;
    title_game.tick(escape);
    auto bytes = serialize_world_state_dat2730(title_game.world());
    CHECK_EQ(read_u16(bytes, 0x1ff2), 0u);  // title-side context
    CHECK_EQ(read_u16(bytes, 0x1ff4), 0u);  // no prompt yet
    CHECK_EQ(read_u16(bytes, 0x2002), 0u);
    CHECK_EQ(read_u16(bytes, 0x2006), 5u);  // Escape selects QUIT TO DOS

    Game gameplay_game(std::uint16_t {0}, std::nullopt);
    gameplay_game.tick(escape);
    bytes = serialize_world_state_dat2730(gameplay_game.world());
    CHECK_EQ(read_u16(bytes, 0x1ff2), 1u);  // gameplay context
    CHECK_EQ(read_u16(bytes, 0x2006), 3u);  // STOP GAME

    InputState neutral {};
    gameplay_game.tick(neutral);
    InputState enter {};
    enter.start = true;
    enter.accept = true;
    gameplay_game.tick(enter);
    gameplay_game.tick(neutral);
    for (int tick = 0;
         tick < 20 && gameplay_game.world().control_panel_pending_action_row >= 0;
         ++tick) {
        gameplay_game.tick(neutral);
    }
    bytes = serialize_world_state_dat2730(gameplay_game.world());
    CHECK_EQ(read_u16(bytes, 0x1ff4), 1u);
    CHECK_EQ(read_u16(bytes, 0x2002), 0x0643u);  // STOP YOUR CURRENT GAME?
}

TEST_CASE("serializer mirrors live CONFIG hardware separately from saved preferences")
{
    const Game game;
    const auto bytes = serialize_world_state_dat2730(game.world());
    CHECK(bytes.size() >= 0xe1d2);
    if (bytes.size() < 0xe1d2) {
        return;
    }
    CHECK_EQ(read_u16(bytes, 0xdea4), 1u);
    CHECK_EQ(read_u16(bytes, 0xdea2), 0u);
    CHECK_EQ(read_u16(bytes, 0xdea6), 1u);
    CHECK_EQ(read_u16(bytes, 0xe1d0), 1u);
}

TEST_CASE("level 1 first update: bank A has 10 remaining spawns and 400/500 delay window")
{
    Game game(std::uint16_t{0}, std::nullopt);
    game.tick(InputState{});
    const auto bytes = serialize_world_state_dat2730(game.world());
    // Bank A base = 0x4d3e
    CHECK_EQ(read_u16(bytes, 0x4d3e + 0x08), 10u);  // remaining spawns
    CHECK_EQ(read_u16(bytes, 0x4d3e + 0x0a), 400u); // min delay
    CHECK_EQ(read_u16(bytes, 0x4d3e + 0x0c), 500u); // max delay
    CHECK_EQ(read_u16(bytes, 0x4d3e + 0x0e), 80u);  // paratrooper cadence
}

TEST_CASE("level 1 first update: banks B..E have zero remaining spawns")
{
    Game game(std::uint16_t{0}, std::nullopt);
    game.tick(InputState{});
    const auto bytes = serialize_world_state_dat2730(game.world());
    CHECK_EQ(read_u16(bytes, 0x4d58 + 0x08), 0u);  // bank B
    CHECK_EQ(read_u16(bytes, 0x4d72 + 0x08), 0u);  // bank C
    CHECK_EQ(read_u16(bytes, 0x4d8c + 0x08), 0u);  // bank D
    CHECK_EQ(read_u16(bytes, 0x4da6 + 0x08), 0u);  // bank E (smart bomb)
}

TEST_CASE("finale level: budget remaining 9, total 9 written at 0x4df0/0x4df2")
{
    Game game(std::uint16_t{12}, std::nullopt);
    game.tick(InputState{});
    const auto bytes = serialize_world_state_dat2730(game.world());
    CHECK_EQ(read_u16(bytes, 0x4df0), 9u);
    CHECK_EQ(read_u16(bytes, 0x4df2), 9u);
}

TEST_CASE("pre-finale serialization retains the successful pickup accumulator")
{
    Game game(std::uint16_t {3});
    game.diagnostic_world().finale_budget_seed = 7;
    const auto bytes = serialize_world_state_dat2730(game.world());
    CHECK_EQ(read_u16(bytes, 0x4df0), 7u);
    CHECK_EQ(read_u16(bytes, 0x4df2), 0u);
}
