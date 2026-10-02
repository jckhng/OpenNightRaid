#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "niteraid/game.hpp"
#include "niteraid/renderer.hpp"
#include "test_harness.hpp"
#include "ui_high_score_scenarios.hpp"
#include "ui_terminal_scenarios.hpp"

#include <utility>

namespace {

void key(SDL_Keycode code, bool down, bool repeat = false)
{
    SDL_Event event {};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.sym = code;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = repeat ? 1 : 0;
    CHECK_EQ(SDL_PushEvent(&event), 1);
}

void begin_entry(niteraid::Game& game)
{
    auto& world = game.diagnostic_world();
    world.screen = niteraid::Screen::HighScoreEntry;
    world.active_high_score_index = 0;
    world.high_scores[0].name = "Jack";
    world.high_scores[0].highlighted = true;
}

} // namespace

TEST_CASE("Faithful menu consumes held arrow repeat makes and clamps at the top")
{
    for (const auto direction : {SDLK_UP, SDLK_LEFT}) {
        niteraid::Renderer renderer;
        CHECK(renderer.is_interactive());
        if (!renderer.is_interactive()) {
            return;
        }
        niteraid::Game game;
        const auto step = [&]() {
            renderer.pump_events();
            game.tick(renderer.input_state());
        };
        key(SDLK_RETURN, true);
        step();
        key(SDLK_RETURN, false);
        step();
        CHECK(game.world().screen == niteraid::Screen::ControlPanel);
        for (int tick = 0; tick < niteraid::kScreenFadeFrames; ++tick) {
            step();
        }
        key(direction, true);
        step();
        CHECK_EQ(game.world().control_panel_row, 2);
        key(direction, true, true);
        step();
        CHECK_EQ(game.world().control_panel_row, 1);
        key(direction, true, true);
        step();
        CHECK_EQ(game.world().control_panel_row, 0);
        key(direction, true, true);
        step();
        CHECK_EQ(game.world().control_panel_row, 0);
        step();
        CHECK_EQ(game.world().control_panel_row, 0);
        key(direction, false);
        step();
        CHECK_EQ(game.world().control_panel_row, 0);
    }
}

TEST_CASE("SDL menu retains rapid released arrow taps in both input modes")
{
    for (const bool enhanced_input : {false, true}) {
        niteraid::Renderer renderer;
        CHECK(renderer.is_interactive());
        if (!renderer.is_interactive()) {
            return;
        }
        niteraid::Game game(std::nullopt, std::nullopt, false, false, enhanced_input);
        key(SDLK_RETURN, true);
        renderer.pump_events();
        game.tick(renderer.input_state());
        key(SDLK_RETURN, false);
        renderer.pump_events();
        game.tick(renderer.input_state());
        CHECK(game.world().screen == niteraid::Screen::ControlPanel);
        CHECK_EQ(game.world().control_panel_row, 3);
        for (const auto [code, expected_row] : {
                std::pair {SDLK_UP, 2}, std::pair {SDLK_UP, 1},
                std::pair {SDLK_DOWN, 2}, std::pair {SDLK_DOWN, 3},
                std::pair {SDLK_LEFT, 2}, std::pair {SDLK_RIGHT, 3}}) {
            key(code, true);
            key(code, false);
            renderer.pump_events();
            game.tick(renderer.input_state());
            CHECK_EQ(game.world().control_panel_row, expected_row);
            renderer.pump_events();
            game.tick(renderer.input_state());
            CHECK_EQ(game.world().control_panel_row, expected_row);
        }
    }
}

TEST_CASE("SDL pointer motion selects the faithful menu top and bottom rows")
{
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    niteraid::Game game;
    key(SDLK_RETURN, true);
    renderer.pump_events();
    game.tick(renderer.input_state());
    key(SDLK_RETURN, false);
    renderer.pump_events();
    game.tick(renderer.input_state());
    for (const int y : {0, 9999}) {
        SDL_Event event {};
        event.type = SDL_MOUSEMOTION;
        event.motion.x = 0;
        event.motion.y = y;
        CHECK_EQ(SDL_PushEvent(&event), 1);
        renderer.pump_events();
        game.tick(renderer.input_state());
        CHECK_EQ(game.world().control_panel_row, y == 0 ? 0 : 5);
        renderer.pump_events();
        game.tick(renderer.input_state());
        CHECK_EQ(game.world().control_panel_row, y == 0 ? 0 : 5);
    }
}

TEST_CASE("SDL menu accepts all three mouse buttons and commits only after release")
{
    for (const auto button : {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE}) {
        niteraid::Renderer renderer;
        CHECK(renderer.is_interactive());
        if (!renderer.is_interactive()) {
            return;
        }
        niteraid::Game game;
        const auto step = [&]() {
            renderer.pump_events();
            game.tick(renderer.input_state());
        };
        key(SDLK_RETURN, true);
        step();
        key(SDLK_RETURN, false);
        step();
        for (int row = 3; row > 0; --row) {
            key(SDLK_UP, true);
            step();
            key(SDLK_UP, false);
            step();
        }
        SDL_Event event {};
        event.type = SDL_MOUSEBUTTONDOWN;
        event.button.button = static_cast<Uint8>(button);
        CHECK_EQ(SDL_PushEvent(&event), 1);
        step();
        CHECK_EQ(game.world().audio_config_words[3], 3);
        step();
        CHECK_EQ(game.world().audio_config_words[3], 3);
        event.type = SDL_MOUSEBUTTONUP;
        CHECK_EQ(SDL_PushEvent(&event), 1);
        step();
        CHECK_EQ(game.world().audio_config_words[3], 0);
    }
}

TEST_CASE("natural loss Enter tap reaches the nonqualifying table then title without input leakage")
{
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    namespace terminal_ui = niteraid::testing::terminal_ui;
    niteraid::Game game(std::uint16_t {0}, std::nullopt, false, false, false, false,
                       terminal_ui::kPrelevelSeed);
    std::vector<terminal_ui::Sample> samples;
    terminal_ui::replay(renderer, game, [&](const auto& sample) { samples.push_back(sample); });
    CHECK_EQ(samples.size(), 7u);
    if (samples.size() != 7) {
        return;
    }
    CHECK(samples[0].screen == niteraid::Screen::Gameplay);
    CHECK(samples[1].screen == niteraid::Screen::GameOver);
    CHECK_EQ(samples[1].score, 20);
    CHECK_EQ(samples[1].terminal_draw, 20);
    CHECK(samples[2].screen == niteraid::Screen::HighScores);
    CHECK(samples[2].name_submit);
    CHECK(!samples[2].start);
    CHECK_EQ(samples[2].remaining, niteraid::kHighScoreAttractScreenFrames);
    CHECK(samples[3].screen == niteraid::Screen::HighScores);
    CHECK(!samples[3].name_submit);
    CHECK(!samples[3].start);
    CHECK_EQ(samples[3].active_index, -1);
    CHECK(samples[4].screen == niteraid::Screen::HighScores);
    CHECK_EQ(samples[4].remaining, 1);
    CHECK(samples[5].screen == niteraid::Screen::Title);
    CHECK_EQ(samples[5].tick - samples[2].tick, niteraid::kHighScoreAttractScreenFrames);
    CHECK(!samples[5].name_submit);
    CHECK(!samples[5].start);
    CHECK(samples[6].screen == niteraid::Screen::Title);
}

TEST_CASE("natural loss restart preserves shared RNG but clears score and level clock")
{
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    namespace terminal_ui = niteraid::testing::terminal_ui;
    niteraid::Game game(std::uint16_t {0}, std::nullopt, false, false, false, false,
                       terminal_ui::kPrelevelSeed);
    std::vector<terminal_ui::Sample> samples;
    terminal_ui::replay(renderer, game, [&](const auto& sample) { samples.push_back(sample); },
                        terminal_ui::Destination::Restart);
    CHECK_EQ(samples.size(), 13u);
    if (samples.size() != 13) {
        return;
    }
    CHECK_EQ(samples[1].rng_seed, 3509644653u);
    CHECK_EQ(samples[8].panel_row, 3);
    CHECK_EQ(samples[8].pending_action, -1);
    CHECK(!samples[8].start);
    CHECK_EQ(samples[9].pending_action, 3);
    CHECK(!samples[10].start);
    CHECK_EQ(samples[11].score, 0);
    CHECK_EQ(samples[11].frame_tick, 1u);
    CHECK_EQ(samples[11].rng_seed, 483024274u);
    CHECK_EQ(samples[11].first_spawn_deadline, 90u);
    CHECK_EQ(samples[12].frame_tick, 2u);
    CHECK_EQ(samples[12].rng_seed, 483024274u);
    CHECK_EQ(samples[12].first_spawn_deadline, 90u);
}

TEST_CASE("SDL name deletion preserves taps and waits for keyboard repeat")
{
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return;
    }
    namespace keys = niteraid::testing::high_score_ui;
    for (const auto code : {SDLK_BACKSPACE, SDLK_LEFT}) {
        niteraid::Game game;
        begin_entry(game);
        const auto step = [&](keys::KeyEvent action) {
            keys::queue_key(code, action);
            renderer.pump_events();
            game.tick(renderer.input_state());
        };
        step(keys::KeyEvent::Tap);
        CHECK(game.world().high_scores[0].name == "Jac");
        step(keys::KeyEvent::None);
        CHECK(game.world().high_scores[0].name == "Jac");
        step(keys::KeyEvent::Press);
        CHECK(game.world().high_scores[0].name == "Ja");
        step(keys::KeyEvent::None);
        CHECK(game.world().high_scores[0].name == "Ja");
        step(keys::KeyEvent::Repeat);
        CHECK(game.world().high_scores[0].name == "J");
        step(keys::KeyEvent::Release);
        CHECK(game.world().high_scores[0].name == "J");
    }
}

TEST_CASE("SDL Enter tap survives a press and release in one event batch")
{
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) return;
    for (const auto code : {SDLK_RETURN, SDLK_KP_ENTER}) {
        niteraid::Game game;
        begin_entry(game);
        key(code, true);
        key(code, false);
        renderer.pump_events();
        game.tick(renderer.input_state());
        CHECK(game.world().screen == niteraid::Screen::HighScores);
        CHECK(game.world().high_scores[0].name == "Jack");
        CHECK_EQ(game.world().active_high_score_index, -1);

        // No later keypress should be needed to leave the completed table.
        for (int tick = 0; tick < 420; ++tick) {
            renderer.pump_events();
            game.tick(renderer.input_state());
        }
        CHECK(game.world().screen == niteraid::Screen::Title);
    }
}

TEST_CASE("SDL letter Y is name text not a submission action")
{
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) return;
    niteraid::Game game;
    begin_entry(game);
    key(SDLK_y, true);
    SDL_Event text {};
    text.type = SDL_TEXTINPUT;
    SDL_strlcpy(text.text.text, "y", sizeof(text.text.text));
    CHECK_EQ(SDL_PushEvent(&text), 1);
    renderer.pump_events();
    game.tick(renderer.input_state());
    CHECK(game.world().screen == niteraid::Screen::HighScoreEntry);
    CHECK(game.world().high_scores[0].name == "Jacky");
    key(SDLK_y, false);
    renderer.pump_events();
}

TEST_CASE("SDL held Enter submits once and does not dismiss the completed table")
{
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) return;
    niteraid::Game game;
    begin_entry(game);
    key(SDLK_RETURN, true);
    renderer.pump_events();
    game.tick(renderer.input_state());
    CHECK(game.world().screen == niteraid::Screen::HighScores);
    for (int tick = 0; tick < 20; ++tick) {
        renderer.pump_events();
        game.tick(renderer.input_state());
    }
    CHECK(game.world().screen == niteraid::Screen::HighScores);
    CHECK_EQ(game.world().high_score_frames_remaining, 400);
    key(SDLK_RETURN, false);
    for (int tick = 0; tick < 400; ++tick) {
        renderer.pump_events();
        game.tick(renderer.input_state());
    }
    CHECK(game.world().screen == niteraid::Screen::Title);
}

TEST_CASE("SDL repeated Enter from attract scores selects the panel action")
{
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) return;
    niteraid::Game game;
    auto& world = game.diagnostic_world();
    world.screen = niteraid::Screen::HighScores;
    world.high_score_frames_remaining = 420;
    world.active_high_score_index = -1;

    key(SDLK_RETURN, true);
    renderer.pump_events();
    game.tick(renderer.input_state());
    CHECK(world.screen == niteraid::Screen::ControlPanel);
    CHECK_EQ(world.control_panel_row, 3);
    CHECK_EQ(world.control_panel_pending_action_row, -1);

    key(SDLK_RETURN, true, true);
    renderer.pump_events();
    game.tick(renderer.input_state());
    CHECK_EQ(world.control_panel_pending_action_row, 3);

    key(SDLK_RETURN, false);
    renderer.pump_events();
    game.tick(renderer.input_state());
}

TEST_CASE("SDL Enter tap from attract scores leaves the panel idle")
{
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) return;
    niteraid::Game game;
    auto& world = game.diagnostic_world();
    world.screen = niteraid::Screen::HighScores;
    world.high_score_frames_remaining = 420;
    world.active_high_score_index = -1;

    key(SDLK_RETURN, true);
    renderer.pump_events();
    game.tick(renderer.input_state());
    key(SDLK_RETURN, false);
    renderer.pump_events();
    game.tick(renderer.input_state());
    CHECK(world.screen == niteraid::Screen::ControlPanel);
    CHECK_EQ(world.control_panel_pending_action_row, -1);
}

TEST_CASE("the Enter that leaves Game Over cannot also submit during catch-up")
{
    niteraid::Game game;
    auto& world = game.diagnostic_world();
    world.screen = niteraid::Screen::GameOver;
    world.scores.score = 1000000;
    niteraid::InputState enter {};
    enter.start = true;
    enter.name_submit = true;
    game.tick(enter);
    CHECK(world.screen == niteraid::Screen::HighScoreEntry);
    game.tick(enter);
    CHECK(world.screen == niteraid::Screen::HighScoreEntry);
    game.tick(niteraid::InputState {});
    game.tick(enter);
    CHECK(world.screen == niteraid::Screen::HighScores);
}

namespace {

namespace high_score_ui = niteraid::testing::high_score_ui;

std::vector<high_score_ui::Sample> replay_high_score(std::string_view id)
{
    niteraid::Renderer renderer;
    CHECK(renderer.is_interactive());
    if (!renderer.is_interactive()) {
        return {};
    }
    niteraid::Game game;
    std::vector<high_score_ui::Sample> samples;
    high_score_ui::replay(id, renderer, game, [&](const auto& sample) {
        samples.push_back(sample);
    });
    return samples;
}

void check_completed_entry(const high_score_ui::Sample& sample)
{
    CHECK(sample.name == "Jack");
    CHECK_EQ(sample.active_index, -1);
    CHECK(!sample.highlighted);
}

} // namespace

TEST_CASE("UI replay main Enter same-batch tap latches submission for one pump")
{
    const auto samples = replay_high_score("high_score_main_enter_tap");
    CHECK_EQ(samples.size(), 3u);
    if (samples.size() != 3) {
        return;
    }
    CHECK(samples[0].screen == niteraid::Screen::HighScoreEntry);
    CHECK(samples[1].event == high_score_ui::KeyEvent::Tap);
    CHECK_EQ(samples[1].key, SDLK_RETURN);
    CHECK(samples[1].name_submit);
    CHECK(!samples[1].start);
    CHECK(samples[1].screen == niteraid::Screen::HighScores);
    CHECK_EQ(samples[1].remaining, high_score_ui::kCompletedTableTicks);
    check_completed_entry(samples[1]);
    CHECK(!samples[2].name_submit);
    CHECK(samples[2].screen == niteraid::Screen::HighScores);
    CHECK_EQ(samples[2].remaining, high_score_ui::kCompletedTableTicks - 1);
}

TEST_CASE("UI replay Enter repeat cannot dismiss until release and a new press")
{
    const auto samples = replay_high_score("high_score_main_enter_repeat");
    CHECK_EQ(samples.size(), 7u);
    if (samples.size() != 7) {
        return;
    }
    CHECK(samples[1].name_submit);
    for (int tick = 1; tick <= 4; ++tick) {
        CHECK(samples[tick].screen == niteraid::Screen::HighScores);
        CHECK_EQ(samples[tick].remaining, high_score_ui::kCompletedTableTicks - tick + 1);
        check_completed_entry(samples[tick]);
    }
    for (int tick = 2; tick <= 3; ++tick) {
        CHECK(samples[tick].event == high_score_ui::KeyEvent::Repeat);
        CHECK_EQ(samples[tick].key, SDLK_RETURN);
        CHECK(samples[tick].start);
        CHECK(!samples[tick].name_submit);
    }
    CHECK(samples[4].event == high_score_ui::KeyEvent::Release);
    CHECK(!samples[4].start);
    CHECK(!samples[4].name_submit);
    CHECK(samples[5].name_submit);
    CHECK(samples[5].screen == niteraid::Screen::ControlPanel);
    CHECK(samples[6].screen == niteraid::Screen::ControlPanel);
    CHECK(!samples[6].start);
    CHECK(!samples[6].name_submit);
}

TEST_CASE("UI replay attract Enter repeat selects the control-panel action")
{
    const auto samples = replay_high_score("high_score_attract_held_enter");
    CHECK_EQ(samples.size(), 5u);
    if (samples.size() != 5) {
        return;
    }
    CHECK(samples[0].screen == niteraid::Screen::HighScores);
    CHECK_EQ(samples[0].active_index, -1);
    CHECK(samples[1].screen == niteraid::Screen::ControlPanel);
    CHECK_EQ(samples[1].control_panel_row, 3);
    CHECK_EQ(samples[1].control_panel_pending_action_row, -1);
    for (int tick = 2; tick <= 3; ++tick) {
        CHECK(samples[tick].event == high_score_ui::KeyEvent::Repeat);
        CHECK(samples[tick].start_repeat);
        CHECK_EQ(samples[tick].control_panel_pending_action_row, 3);
    }
    CHECK(samples[4].event == high_score_ui::KeyEvent::Release);
    CHECK(!samples[4].start);
    CHECK_EQ(samples[4].control_panel_pending_action_row, 3);
}

TEST_CASE("UI replay completed table remains until the exact native dwell boundary")
{
    const auto samples = replay_high_score("high_score_completed_table_dwell");
    const auto expected_size = static_cast<std::size_t>(high_score_ui::kCompletedTableTicks + 2);
    CHECK_EQ(samples.size(), expected_size);
    if (samples.size() != expected_size) {
        return;
    }
    for (int tick = 1; tick <= high_score_ui::kCompletedTableTicks; ++tick) {
        CHECK_EQ(samples[tick].tick, tick);
        CHECK(samples[tick].screen == niteraid::Screen::HighScores);
        CHECK_EQ(samples[tick].remaining, high_score_ui::kCompletedTableTicks - tick + 1);
        check_completed_entry(samples[tick]);
    }
    CHECK(samples.back().event == high_score_ui::KeyEvent::None);
    CHECK(samples.back().screen == niteraid::Screen::Title);
}

TEST_CASE("UI replay completed table dismissal records destination after key release")
{
    for (const auto id : {"high_score_dismiss_enter", "high_score_dismiss_space", "high_score_dismiss_tab"}) {
        const auto samples = replay_high_score(id);
        CHECK_EQ(samples.size(), 5u);
        if (samples.size() != 5) {
            continue;
        }
        CHECK(samples[1].screen == niteraid::Screen::HighScores);
        CHECK(samples[2].event == high_score_ui::KeyEvent::Release);
        CHECK(samples[2].screen == niteraid::Screen::HighScores);
        CHECK_EQ(samples[2].remaining, high_score_ui::kCompletedTableTicks - 1);
        const bool tab = std::string_view(id) == "high_score_dismiss_tab";
        const auto destination = tab ? niteraid::Screen::Title : niteraid::Screen::ControlPanel;
        const auto key = tab ? SDLK_TAB :
            std::string_view(id) == "high_score_dismiss_enter" ? SDLK_RETURN : SDLK_SPACE;
        CHECK(samples[3].event == high_score_ui::KeyEvent::Press);
        CHECK(samples[4].event == high_score_ui::KeyEvent::Release);
        CHECK_EQ(samples[3].key, key);
        CHECK(samples[3].screen == destination);
        CHECK(samples[4].screen == destination);
        check_completed_entry(samples[4]);
    }
}
