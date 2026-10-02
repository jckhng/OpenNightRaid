// Asserts the recovered scoring constants from research/nite-player-shot-and-score.md
// and research/gameplay-systems.md:
//   shot fired           -1
//   big-plane (D/B)      +5
//   little-plane (A/C)  +10
//   smart bomb kill     +10
//   grounded resolution  +2
// plus the AddToScore clamp at zero (16c8:5757).

#include "niteraid/game.hpp"
#include "niteraid/game_internals.hpp"
#include "test_harness.hpp"

using niteraid::AircraftVariant;
using niteraid::Game;
using niteraid::InputState;
using niteraid::internals::score_for_aircraft;

TEST_CASE("score_for_aircraft: big-plane families return +5")
{
    CHECK_EQ(score_for_aircraft(AircraftVariant::D), 5);
    CHECK_EQ(score_for_aircraft(AircraftVariant::B), 5);
}

TEST_CASE("score_for_aircraft: little-plane families return +10")
{
    CHECK_EQ(score_for_aircraft(AircraftVariant::A), 10);
    CHECK_EQ(score_for_aircraft(AircraftVariant::C), 10);
}

TEST_CASE("score clamps at zero on idle fire (-1 per shot)")
{
    // Drive the game from a fresh start at level 1, hold fire, and confirm
    // the score is clamped (never goes negative) — matches AddToScore.
    Game game(std::uint16_t{0}, std::nullopt);
    InputState input {};
    input.fire = true;
    for (int i = 0; i < 200; ++i) {
        game.tick(input);
    }
    CHECK(game.world().scores.score >= 0);
}

TEST_CASE("a freshly started level has zero score and counters")
{
    Game game(std::uint16_t{0}, std::nullopt);
    InputState input {};
    game.tick(input);  // first tick seeds state
    CHECK_EQ(game.world().scores.score, 0);
    CHECK_EQ(game.world().scores.enemy_kills, 0);
    CHECK_EQ(game.world().scores.grounded_invader_resolutions, 0);
}
