#include "niteraid/game.hpp"
#include "niteraid/game_internals.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>

using niteraid::Game;
using niteraid::InputState;
using niteraid::ObjectType;
using niteraid::internals::finalized_high_score_name;
using niteraid::internals::projectile_velocity_for_frame;

TEST_CASE("registered Control-Alt digit keys warp to levels 1 through 10")
{
    Game game(std::uint16_t {0}, std::nullopt);

    InputState warp_ten {};
    warp_ten.control_modifier = true;
    warp_ten.alt_modifier = true;
    warp_ten.level_warp_digit = 0;
    game.tick(warp_ten);
    CHECK_EQ(game.world().current_level, 9u);
    CHECK_EQ(game.world().frame_tick, 1u);

    InputState warp_four {};
    warp_four.control_modifier = true;
    warp_four.alt_modifier = true;
    warp_four.level_warp_digit = 4;
    game.tick(warp_four);
    CHECK_EQ(game.world().current_level, 3u);
    CHECK_EQ(game.world().frame_tick, 1u);
}

TEST_CASE("level warp digits require both Control and Alt")
{
    Game game(std::uint16_t {0}, std::nullopt);

    InputState input {};
    input.control_modifier = true;
    input.level_warp_digit = 7;
    game.tick(input);
    CHECK_EQ(game.world().current_level, 0u);
    CHECK_EQ(game.world().frame_tick, 2u);
}

TEST_CASE("IBCD doubles both components of player projectile velocity")
{
    Game game(std::uint16_t {0}, std::nullopt, true);
    const auto cannon = std::find_if(game.diagnostic_world().objects.begin(),
                                     game.diagnostic_world().objects.end(), [](const auto& object) {
                                         return object.type == ObjectType::PlayerCannon;
                                     });
    CHECK(cannon != game.diagnostic_world().objects.end());
    if (cannon == game.diagnostic_world().objects.end()) {
        return;
    }
    cannon->timer = 9;

    InputState fire {};
    fire.fire = true;
    game.tick(fire);

    const auto projectile = std::find_if(
        game.world().objects.begin(), game.world().objects.end(), [](const auto& object) {
            return object.active && object.type == ObjectType::PlayerProjectile;
        });
    CHECK(projectile != game.world().objects.end());
    if (projectile != game.world().objects.end()) {
        const auto normal_velocity = projectile_velocity_for_frame(6);
        CHECK_NEAR(projectile->velocity.x, normal_velocity.x * 2.0f, 0.00001f);
        CHECK_NEAR(projectile->velocity.y, normal_velocity.y * 2.0f, 0.00001f);
    }
}

TEST_CASE("IBCD forces the original high-score marker")
{
    CHECK(finalized_high_score_name("PLAYER", true) == "I cheated!");
    CHECK(finalized_high_score_name("", true) == "I cheated!");
    CHECK(finalized_high_score_name("", false) == "Unknown Soldier");
    CHECK(finalized_high_score_name("PLAYER", false) == "PLAYER");
}

TEST_CASE("IBCD applies its fallback when an empty qualifying name is submitted")
{
    const auto save_path = std::filesystem::path("reimpl") / "save" / "CONFIG.NTR";
    std::error_code ec;
    std::filesystem::remove(save_path, ec);

    Game game(std::uint16_t {0}, std::nullopt, true);
    auto& state = game.diagnostic_world();
    state.screen = niteraid::Screen::HighScoreEntry;
    state.active_high_score_index = 0;
    state.high_scores[0].name.clear();
    state.high_scores[0].highlighted = true;

    InputState submit {};
    submit.name_submit = true;
    game.tick(submit);

    CHECK_EQ(static_cast<int>(state.screen), static_cast<int>(niteraid::Screen::HighScores));
    CHECK(state.high_scores[0].name == "I cheated!");
    CHECK_EQ(state.active_high_score_index, -1);
    CHECK(!state.high_scores[0].highlighted);

    std::filesystem::remove(save_path, ec);
}

TEST_CASE("hidden launch flags are retained in world state")
{
    Game game(std::uint16_t {0}, std::nullopt, true, true);
    CHECK(game.world().fast_shots_enabled);
    CHECK(game.world().female_finale_enabled);
}
