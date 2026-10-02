#pragma once

#include "niteraid/presenter_timing.hpp"
#include "ui_high_score_scenarios.hpp"

namespace niteraid::testing::terminal_ui {

inline constexpr std::string_view kCase = "terminal_natural_loss_expiry";
inline constexpr std::string_view kRestartCase = "terminal_natural_loss_restart";
inline constexpr std::string_view kQualificationCase = "terminal_natural_loss_qualification";
inline constexpr std::string_view kQualificationSaveCase = "terminal_natural_loss_qualification_save";
enum class Destination { Title, Restart, Qualification, QualificationSave };
inline constexpr std::uint32_t kPrelevelSeed = 4288041665u;
inline constexpr int kMaximumReplaySteps = 6000;
inline constexpr std::uint32_t kReadyDraw = 20;

struct Sample {
    const char* boundary;
    int tick;
    std::uint32_t frame_tick;
    Screen screen;
    int score;
    std::uint32_t rng_seed;
    int remaining;
    int active_index;
    int terminal_draw;
    bool name_submit;
    bool start;
    int panel_row;
    int pending_action;
    std::uint32_t first_spawn_deadline;
};

template <typename Observe>
void replay(Renderer& renderer, Game& game, Observe observe, Destination destination = Destination::Title)
{
    int tick = 0;
    renderer.render(game.world());
    const auto record = [&](const char* boundary) {
        const auto& world = game.world();
        const auto input = renderer.input_state();
        const auto frame = presenter_timing::terminal_presenter_frame(world);
        observe(Sample {boundary, tick, world.frame_tick, world.screen, world.scores.score,
                        world.gameplay_rng_seed, world.high_score_frames_remaining,
                        world.active_high_score_index,
                        frame ? static_cast<int>(presenter_timing::terminal_presenter_draw(*frame)) : -1,
                        input.name_submit, input.start, world.control_panel_row,
                        world.control_panel_pending_action_row, world.wave_banks[0].live_next_trigger_tick});
    };
    const auto step = [&](high_score_ui::KeyEvent event = high_score_ui::KeyEvent::None,
                          SDL_Keycode key = SDLK_RETURN) {
        if (++tick > kMaximumReplaySteps) {
            throw std::runtime_error("natural terminal replay exceeded its bounded step limit");
        }
        high_score_ui::queue_key(key, event);
        renderer.pump_events();
        game.tick(renderer.input_state());
        game.prepare_gameplay_page();
        renderer.render(game.world());
    };
    const auto ready = [&]() {
        const auto frame = presenter_timing::terminal_presenter_frame(game.world());
        return frame && presenter_timing::terminal_presenter_draw(*frame) == kReadyDraw &&
            (*frame - presenter_timing::kTerminalExplosionVideoFrames) %
                presenter_timing::kFlagVideoFramesEach == presenter_timing::kFlagVideoFramesEach - 1;
    };

    // No diagnostic-world imports: neutral gameplay must reach the final draw's wait.
    record("natural_start");
    while (!ready()) {
        step();
    }
    record("terminal_ready");
    step(high_score_ui::KeyEvent::Tap);
    record("terminal_key_tap");
    const bool qualification = destination == Destination::Qualification || destination == Destination::QualificationSave;
    if (qualification) {
        if (game.world().screen != Screen::HighScoreEntry || game.world().active_high_score_index != 0) {
            throw std::runtime_error("natural terminal replay did not qualify at the first row");
        }
        step();
        record("name_ready");
        struct NameAction { SDL_Keycode key; char text; const char* boundary; };
        constexpr std::array<NameAction, 6> actions {{
            {SDLK_a, 'a', "name_a"}, {SDLK_COMMA, ',', "name_comma"},
            {SDLK_b, 'b', "name_b"}, {SDLK_BACKSPACE, 0, "name_delete"},
            {SDLK_c, 'c', "name_c"}, {SDLK_RETURN, 0, "name_submitted"},
        }};
        for (const auto& action : actions) {
            if (action.text != 0) {
                SDL_Event text {};
                text.type = SDL_TEXTINPUT;
                text.text.text[0] = action.text;
                if (SDL_PushEvent(&text) != 1) {
                    throw std::runtime_error("SDL_PushEvent failed for high-score text");
                }
            }
            step(high_score_ui::KeyEvent::Tap, action.key);
            record(action.boundary);
            if (action.key != SDLK_RETURN) {
                step();
            }
        }
    } else if (game.world().screen != Screen::HighScores || game.world().active_high_score_index != -1) {
        throw std::runtime_error("natural terminal replay did not reach a nonqualifying score table");
    }
    step();
    record(qualification ? "submission_key_released" : "terminal_key_released");
    while (game.world().screen == Screen::HighScores && game.world().high_score_frames_remaining > 1) {
        step();
    }
    record("table_before_expiry");
    step();
    record("title_entry");
    if (game.world().screen != Screen::Title) {
        throw std::runtime_error("natural terminal replay did not return to the title");
    }
    while (game.world().screen_fade_frames_remaining > 0) {
        step();
    }
    record("title_ready");
    if (destination == Destination::QualificationSave) {
        step(high_score_ui::KeyEvent::Press);
        record("quit_title_key_down");
        step(high_score_ui::KeyEvent::Release);
        record("quit_panel_ready");
        if (game.world().screen != Screen::ControlPanel || game.world().control_panel_row != 3) {
            throw std::runtime_error("qualified save replay did not reach the title-side panel");
        }
        step(high_score_ui::KeyEvent::Press, SDLK_ESCAPE);
        record("quit_key_down");
        step(high_score_ui::KeyEvent::Release, SDLK_ESCAPE);
        while (game.world().confirmation_prompt == ConfirmationPromptAction::None) {
            step();
        }
        if (game.world().confirmation_prompt != ConfirmationPromptAction::QuitToDos) {
            throw std::runtime_error("qualified save replay did not reach Quit to DOS confirmation");
        }
        record("quit_prompt_ready");
        step(high_score_ui::KeyEvent::Press);
        record("quit_confirmed");
        if (!game.world().quit_requested) {
            throw std::runtime_error("qualified save replay did not confirm quit");
        }
        return;
    }
    if (destination != Destination::Restart) {
        return;
    }
    step(high_score_ui::KeyEvent::Press);
    record("title_key_down");
    step(high_score_ui::KeyEvent::Release);
    record("panel_ready");
    if (game.world().screen != Screen::ControlPanel || game.world().control_panel_row != 3 ||
        game.world().control_panel_pending_action_row != -1) {
        throw std::runtime_error("natural restart did not reach an idle title-side row-3 panel");
    }
    step(high_score_ui::KeyEvent::Press);
    record("new_game_key_down");
    step(high_score_ui::KeyEvent::Release);
    record("new_game_key_released");
    while (game.world().screen == Screen::ControlPanel) {
        step();
    }
    record("new_game_ready");
    if (game.world().screen != Screen::Gameplay) {
        throw std::runtime_error("natural restart did not return to gameplay");
    }
    step();
    record("first_gameplay_update");
}

} // namespace niteraid::testing::terminal_ui
