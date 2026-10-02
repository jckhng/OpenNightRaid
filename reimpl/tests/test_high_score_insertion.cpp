// 15c3:0f06 accepts a higher score or any signed secondary-stat improvement
// on an equal score. Secondary statistics are not lexicographic tie-breaks.

#include "niteraid/game.hpp"
#include "niteraid/game_internals.hpp"
#include "niteraid/game_types.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using niteraid::HighScoreEntry;
using niteraid::Game;
using niteraid::InputState;
using niteraid::internals::append_high_score_name_character;
using niteraid::internals::cycle_gamepad_high_score_character;
using niteraid::internals::high_score_name_width;

namespace {

bool beats(const HighScoreEntry& left, const HighScoreEntry& right)
{
    Game game;
    auto& state = game.diagnostic_world();
    state.screen = niteraid::Screen::GameOver;
    state.gameplay_state = niteraid::GameplayState::GameOver;
    state.player_dead = false;
    state.high_scores.fill(right);
    state.scores.score = left.score;
    state.scores.grounded_invader_resolutions = left.stat_a;
    state.scores.enemy_kills = left.stat_b;
    state.current_level = static_cast<int>(left.stat_c);

    InputState acknowledge {};
    acknowledge.fire = true;
    game.tick(acknowledge);
    return state.screen == niteraid::Screen::HighScoreEntry &&
           state.active_high_score_index == 0;
}

}  // namespace

TEST_CASE("higher score beats lower score regardless of stats")
{
    HighScoreEntry a {.score = 200, .stat_a = 0, .stat_b = 0, .stat_c = 0};
    HighScoreEntry b {.score = 100, .stat_a = 99, .stat_b = 99, .stat_c = 99};
    CHECK(beats(a, b));
    CHECK(!beats(b, a));
}

TEST_CASE("tied score accepts any improved secondary statistic")
{
    HighScoreEntry base {.score = 100, .stat_a = 5, .stat_b = 5, .stat_c = 5};
    HighScoreEntry winner_stat_a = base;
    winner_stat_a.stat_a = 6;
    CHECK(beats(winner_stat_a, base));

    HighScoreEntry winner_stat_b = base;
    winner_stat_b.stat_b = 6;
    CHECK(beats(winner_stat_b, base));
    CHECK(beats(winner_stat_a, winner_stat_b));
    CHECK(beats(winner_stat_b, winner_stat_a));

    HighScoreEntry winner_stat_c = base;
    winner_stat_c.stat_c = 6;
    CHECK(beats(winner_stat_c, base));
    CHECK(beats(winner_stat_b, winner_stat_c));
    CHECK(beats(winner_stat_c, winner_stat_b));
}

TEST_CASE("lower earlier statistics do not veto an equal-score improvement")
{
    const HighScoreEntry base {.score = 100, .stat_a = 5, .stat_b = 5, .stat_c = 5};
    CHECK(beats({.score = 100, .stat_a = 4, .stat_b = 6, .stat_c = 4}, base));
    CHECK(beats({.score = 100, .stat_a = 4, .stat_b = 4, .stat_c = 6}, base));
    CHECK(!beats({.score = 100, .stat_a = 4, .stat_b = 4, .stat_c = 4}, base));
}

TEST_CASE("secondary-stat comparisons retain original signed-word semantics")
{
    for (int field = 0; field < 3; ++field) {
        HighScoreEntry positive {.score = 100, .stat_a = 5, .stat_b = 5, .stat_c = 5};
        HighScoreEntry negative = positive;
        if (field == 0) {
            negative.stat_a = 0x8000;
        } else if (field == 1) {
            negative.stat_b = 0x8000;
        } else {
            negative.stat_c = 0x8000;
        }
        CHECK(!beats(negative, positive));
        CHECK(beats(positive, negative));
    }
}

TEST_CASE("identical records do not beat each other")
{
    HighScoreEntry a {.score = 100, .stat_a = 1, .stat_b = 2, .stat_c = 3};
    HighScoreEntry b = a;
    CHECK(!beats(a, b));
    CHECK(!beats(b, a));
}

TEST_CASE("default-constructed entry has score 100 (matches 1480:015a defaults)")
{
    HighScoreEntry entry {};
    CHECK_EQ(entry.score, 100);
    CHECK(entry.name.empty());
    CHECK(!entry.highlighted);
}

TEST_CASE("WorldState ships with six high-score slots")
{
    niteraid::WorldState state {};
    CHECK_EQ(state.high_scores.size(), 6u);
}

TEST_CASE("default score table uses recovered developer names")
{
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path("reimpl") / "save" / "CONFIG.NTR", ec);

    Game game {};
    CHECK(game.world().high_scores[0].name == "Argo Games - 1993");
    CHECK(game.world().high_scores[1].name == "");
    CHECK(game.world().high_scores[2].name == "Jason Blochowiak");
    CHECK(game.world().high_scores[3].name == "Don Glassford");
    CHECK(game.world().high_scores[4].name == "Dan Linton");
    CHECK(game.world().high_scores[5].name == "Robert Prince");
}

TEST_CASE("high-score name entry uses the recovered rendered-width limit")
{
    std::string name;
    for (int index = 0; index < 52; ++index) {
        CHECK(append_high_score_name_character(name, 'I'));
    }
    CHECK_EQ(high_score_name_width(name), 104);
    CHECK(!append_high_score_name_character(name, 'I'));
    CHECK_EQ(name.size(), 52u);

    std::string wide_name;
    for (int index = 0; index < 13; ++index) {
        CHECK(append_high_score_name_character(wide_name, 'W'));
    }
    CHECK_EQ(high_score_name_width(wide_name), 104);
    CHECK(!append_high_score_name_character(wide_name, 'W'));
}

TEST_CASE("high-score name entry preserves case and filters original rejected bytes")
{
    std::string name;
    CHECK(append_high_score_name_character(name, 'a'));
    CHECK(append_high_score_name_character(name, 'Z'));
    CHECK(append_high_score_name_character(name, '!'));
    CHECK(!append_high_score_name_character(name, ','));
    CHECK(!append_high_score_name_character(name, '\x1f'));
    CHECK(!append_high_score_name_character(name, '\x7f'));
    CHECK(!append_high_score_name_character(name, static_cast<char>(0x80)));
    CHECK(name == "aZ!");
}

TEST_CASE("high-score names accept punctuation except comma at the exact width boundary")
{
    const std::string punctuation = "!\"#$%&'()*+-./:;<=>?@[\\]^_`{|}~";
    for (const char character : punctuation) {
        std::string name;
        CHECK(append_high_score_name_character(name, character));
        CHECK(name.size() == 1u);
    }
    std::string comma_name;
    CHECK(!append_high_score_name_character(comma_name, ','));

    std::string exact_width;
    for (int index = 0; index < 15; ++index) {
        CHECK(append_high_score_name_character(exact_width, 'A'));
    }
    CHECK_EQ(high_score_name_width(exact_width), 105);
    CHECK(!append_high_score_name_character(exact_width, '!'));
    CHECK_EQ(exact_width.size(), 15u);
}

TEST_CASE("high-score name text input filters punctuation and controls in the owning loop")
{
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path("reimpl") / "save" / "CONFIG.NTR", ec);

    Game game;
    auto& state = game.diagnostic_world();
    state.screen = niteraid::Screen::HighScoreEntry;
    state.active_high_score_index = 0;
    state.high_scores[0].name.clear();
    state.high_scores[0].highlighted = true;

    InputState printable {};
    printable.text_input = "a!@.";
    game.tick(printable);
    CHECK(state.high_scores[0].name == "a!@.");

    InputState rejected {};
    rejected.text_input = ",\x1f\x7f";
    game.tick(rejected);
    CHECK(state.high_scores[0].name == "a!@.");

    std::filesystem::remove(std::filesystem::path("reimpl") / "save" / "CONFIG.NTR", ec);
}

TEST_CASE("gamepad high-score alphabet cycles with wraparound")
{
    CHECK(cycle_gamepad_high_score_character('A', 1) == 'B');
    CHECK(cycle_gamepad_high_score_character('A', -1) == '?');
    CHECK(cycle_gamepad_high_score_character('?', 1) == 'A');
    CHECK(cycle_gamepad_high_score_character('\0', 1) == 'B');
}

TEST_CASE("gamepad high-score entry selects deletes and submits characters")
{
    const auto save_path = std::filesystem::path("reimpl") / "save" / "CONFIG.NTR";
    std::error_code ec;
    std::filesystem::remove(save_path, ec);

    Game game;
    auto& state = game.diagnostic_world();
    state.screen = niteraid::Screen::HighScoreEntry;
    state.active_high_score_index = 0;
    state.high_scores[0].name.clear();
    state.high_scores[0].highlighted = true;

    InputState up {};
    up.gamepad_name_up = true;
    game.tick(up);
    CHECK(state.high_score_gamepad_active);
    CHECK(state.high_score_gamepad_character == 'B');

    game.tick(InputState {});
    InputState choose {};
    choose.gamepad_accept = true;
    game.tick(choose);
    CHECK(state.high_scores[0].name == "B");

    game.tick(InputState {});
    InputState down {};
    down.gamepad_name_down = true;
    game.tick(down);
    CHECK(state.high_score_gamepad_character == 'A');

    game.tick(InputState {});
    game.tick(choose);
    CHECK(state.high_scores[0].name == "BA");

    game.tick(InputState {});
    InputState back {};
    back.gamepad_back = true;
    game.tick(back);
    CHECK(state.high_scores[0].name == "B");
    CHECK(state.screen == niteraid::Screen::HighScoreEntry);

    game.tick(InputState {});
    InputState submit {};
    submit.gamepad_start = true;
    game.tick(submit);
    CHECK(state.screen == niteraid::Screen::HighScores);
    CHECK(state.high_scores[0].name == "B");
    CHECK_EQ(state.active_high_score_index, -1);
    CHECK(!state.high_score_gamepad_active);

    std::filesystem::remove(save_path, ec);
}

TEST_CASE("high-score entry accepts left-delete and Escape fallback semantics")
{
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path("reimpl") / "save" / "CONFIG.NTR", ec);

    Game game;
    auto& state = game.diagnostic_world();
    state.screen = niteraid::Screen::HighScoreEntry;
    state.active_high_score_index = 0;
    state.high_scores[0].name = "Ab";
    state.high_scores[0].highlighted = true;

    InputState erase {};
    erase.name_delete = true;
    game.tick(erase);
    CHECK(state.high_scores[0].name == "A");
    game.tick(erase);
    CHECK(state.high_scores[0].name.empty());

    game.tick(InputState {});
    InputState escape {};
    escape.escape = true;
    game.tick(escape);
    CHECK(state.screen == niteraid::Screen::HighScores);
    CHECK(state.high_scores[0].name == "Unknown Soldier");
    CHECK_EQ(state.active_high_score_index, -1);
    CHECK(!state.high_scores[0].highlighted);

    std::filesystem::remove(std::filesystem::path("reimpl") / "save" / "CONFIG.NTR", ec);
}

TEST_CASE("faithful high-score entry persists mixed case only at confirmed quit")
{
    const auto save_path = std::filesystem::path("reimpl") / "save" / "CONFIG.NTR";
    std::error_code ec;
    std::filesystem::remove(save_path, ec);

    {
        Game game;
        auto& state = game.diagnostic_world();
        state.screen = niteraid::Screen::HighScoreEntry;
        state.active_high_score_index = 0;
        state.high_scores[0].name.clear();
        state.high_scores[0].highlighted = true;

        InputState text {};
        text.text_input = "mIx";
        game.tick(text);
        CHECK(state.high_scores[0].name == "mIx");

        InputState accept {};
        accept.name_submit = true;
        game.tick(accept);
        CHECK(state.screen == niteraid::Screen::HighScores);
        CHECK(!std::filesystem::exists(save_path));

        state.screen = niteraid::Screen::ControlPanel;
        state.control_panel_row = 5;
        game.tick(InputState {});
        InputState quit {};
        quit.accept = true;
        game.tick(quit);
        for (int tick = 0; tick < 15; ++tick) {
            game.tick(InputState {});
        }
        CHECK(state.confirmation_prompt == niteraid::ConfirmationPromptAction::QuitToDos);
        CHECK(!std::filesystem::exists(save_path));
        game.tick(quit);
        CHECK(state.quit_requested);
    }

    std::ifstream file(save_path, std::ios::binary);
    const std::vector<std::uint8_t> bytes {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>(),
    };
    CHECK(bytes.size() >= 0x1de);
    if (bytes.size() >= 0x1de) {
        CHECK(bytes[0x16] == static_cast<std::uint8_t>('m'));
        CHECK(bytes[0x17] == static_cast<std::uint8_t>('I'));
        CHECK(bytes[0x18] == static_cast<std::uint8_t>('x'));
        CHECK(bytes[0x19] == 0);
    }

    Game reloaded;
    CHECK(reloaded.world().high_scores[0].name == "mIx");
    std::filesystem::remove(save_path, ec);
}

TEST_CASE("enhanced high-score entry retains immediate persistence")
{
    const auto save_path = std::filesystem::path("reimpl") / "save" / "CONFIG.NTR";
    std::error_code ec;
    std::filesystem::remove(save_path, ec);
    Game game(std::nullopt, std::nullopt, false, false, true);
    auto& state = game.diagnostic_world();
    state.screen = niteraid::Screen::HighScoreEntry;
    state.active_high_score_index = 0;
    state.high_scores[0].name = "Enhanced";
    state.high_scores[0].highlighted = true;
    InputState submit {};
    submit.name_submit = true;
    game.tick(submit);
    CHECK(std::filesystem::exists(save_path));
    Game reloaded;
    CHECK(reloaded.world().high_scores[0].name == "Enhanced");
    std::filesystem::remove(save_path, ec);
}

TEST_CASE("qualifying game over inserts the run before name entry")
{
    const auto save_path = std::filesystem::path("reimpl") / "save" / "CONFIG.NTR";
    std::error_code ec;
    std::filesystem::remove(save_path, ec);

    Game game;
    auto& state = game.diagnostic_world();
    state.screen = niteraid::Screen::GameOver;
    state.gameplay_state = niteraid::GameplayState::GameOver;
    state.player_dead = false;
    // Use the documented default scores rather than depending on CONFIG.NTR.
    for (auto& entry : state.high_scores) entry = {};
    state.high_scores[0].name = "Argo Games - 1993";
    state.scores.score = 1000;
    state.scores.enemy_kills = 17;
    state.scores.grounded_invader_resolutions = 9;

    InputState acknowledge {};
    acknowledge.fire = true;
    game.tick(acknowledge);

    CHECK_EQ(static_cast<int>(state.screen),
             static_cast<int>(niteraid::Screen::HighScoreEntry));
    CHECK_EQ(state.active_high_score_index, 0);
    CHECK(state.high_scores[0].highlighted);
    CHECK_EQ(state.high_scores[0].score, 1000);
    CHECK_EQ(state.high_scores[0].stat_a, 9);
    CHECK_EQ(state.high_scores[0].stat_b, 17);
    CHECK(state.high_scores[1].name == "Argo Games - 1993");

    std::filesystem::remove(save_path, ec);
}

TEST_CASE("qualifying game over inserts at a nonfirst row and shifts lower records")
{
    const auto save_path = std::filesystem::path("reimpl") / "save" / "CONFIG.NTR";
    std::error_code ec;
    std::filesystem::remove(save_path, ec);

    Game game;
    auto& state = game.diagnostic_world();
    state.screen = niteraid::Screen::GameOver;
    state.gameplay_state = niteraid::GameplayState::GameOver;
    state.player_dead = false;
    state.high_scores = {{
        {"row0", 5000, 0, 0, 0, false},
        {"row1", 4000, 0, 0, 0, false},
        {"row2", 3000, 5, 50, 50, false},
        {"row3", 3000, 5, 50, 50, false},
        {"row4", 2000, 0, 0, 0, false},
        {"row5", 1000, 0, 0, 0, false},
    }};
    state.scores.score = 3000;
    state.scores.grounded_invader_resolutions = 5;
    state.scores.enemy_kills = 51;

    InputState acknowledge {};
    acknowledge.fire = true;
    game.tick(acknowledge);

    CHECK_EQ(static_cast<int>(state.screen),
             static_cast<int>(niteraid::Screen::HighScoreEntry));
    CHECK_EQ(state.active_high_score_index, 2);
    CHECK(state.high_scores[0].name == "row0");
    CHECK(state.high_scores[1].name == "row1");
    CHECK(state.high_scores[2].name.empty());
    CHECK_EQ(state.high_scores[2].score, 3000);
    CHECK_EQ(state.high_scores[2].stat_a, 5);
    CHECK_EQ(state.high_scores[2].stat_b, 51);
    CHECK(state.high_scores[2].highlighted);
    CHECK(state.high_scores[3].name == "row2");
    CHECK(state.high_scores[4].name == "row3");
    CHECK(state.high_scores[5].name == "row4");

    std::filesystem::remove(save_path, ec);
}

TEST_CASE("original level-one loss inserts the raw run statistics and zero-based level")
{
    Game game;
    auto& state = game.diagnostic_world();
    state.screen = niteraid::Screen::GameOver;
    state.gameplay_state = niteraid::GameplayState::GameOver;
    state.player_dead = false;
    for (auto& entry : state.high_scores) {
        entry.score = 0;
    }
    state.scores.score = 20;
    state.scores.grounded_invader_resolutions = 10;
    state.scores.enemy_kills = 0;
    state.current_level = 0;

    InputState acknowledge {};
    acknowledge.fire = true;
    game.tick(acknowledge);

    CHECK(state.screen == niteraid::Screen::HighScoreEntry);
    CHECK_EQ(state.active_high_score_index, 0);
    CHECK_EQ(state.high_scores[0].score, 20);
    CHECK_EQ(state.high_scores[0].stat_a, 10);
    CHECK_EQ(state.high_scores[0].stat_b, 0);
    CHECK_EQ(state.high_scores[0].stat_c, 0);
}

TEST_CASE("non-qualifying game over shows scores before clearing the run")
{
    const auto save_path = std::filesystem::path("reimpl") / "save" / "CONFIG.NTR";
    std::error_code ec;
    std::filesystem::remove(save_path, ec);

    Game game;
    auto& state = game.diagnostic_world();
    state.screen = niteraid::Screen::GameOver;
    state.gameplay_state = niteraid::GameplayState::GameOver;
    state.player_dead = false;
    state.scores.score = 0;
    state.scores.enemy_kills = 11;
    state.scores.grounded_invader_resolutions = 7;

    InputState acknowledge {};
    acknowledge.start = true;
    game.tick(acknowledge);

    CHECK_EQ(static_cast<int>(state.screen), static_cast<int>(niteraid::Screen::HighScores));
    CHECK_EQ(state.scores.enemy_kills, 11);
    CHECK_EQ(state.scores.grounded_invader_resolutions, 7);

    for (int tick = 0; tick < niteraid::kHighScoreAttractScreenFrames; ++tick) {
        game.tick(InputState {});
    }

    CHECK_EQ(static_cast<int>(state.screen), static_cast<int>(niteraid::Screen::Title));
    CHECK_EQ(state.scores.score, 0);
    CHECK_EQ(state.scores.enemy_kills, 0);
    CHECK_EQ(state.scores.grounded_invader_resolutions, 0);
    CHECK(!state.high_score_checked);
    CHECK_EQ(state.active_high_score_index, -1);

    std::filesystem::remove(save_path, ec);
}
