#pragma once

#include <SDL.h>

#include "niteraid/game.hpp"
#include "niteraid/renderer.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace niteraid::testing::high_score_ui {

// This is the native regression contract, not an original-runtime measurement.
constexpr int kCompletedTableTicks = 420;
constexpr std::array<std::string_view, 7> kCases {{
    "high_score_main_enter_tap",
    "high_score_main_enter_repeat",
    "high_score_attract_held_enter",
    "high_score_completed_table_dwell",
    "high_score_dismiss_enter",
    "high_score_dismiss_space",
    "high_score_dismiss_tab",
}};

enum class KeyEvent { None, Press, Release, Repeat, Tap };

struct Sample {
    int tick;
    KeyEvent event;
    SDL_Keycode key;
    Screen screen;
    int remaining;
    int active_index;
    bool highlighted;
    bool name_submit;
    bool start;
    bool start_repeat;
    int control_panel_row;
    int control_panel_pending_action_row;
    std::string name;
};

inline bool supports(std::string_view id)
{
    for (const auto candidate : kCases) {
        if (candidate == id) {
            return true;
        }
    }
    return false;
}

inline const char* event_name(KeyEvent event)
{
    switch (event) {
    case KeyEvent::None: return "none";
    case KeyEvent::Press: return "keydown";
    case KeyEvent::Release: return "keyup";
    case KeyEvent::Repeat: return "keydown_repeat";
    case KeyEvent::Tap: return "keydown_keyup_same_batch";
    }
    throw std::runtime_error("unknown UI key event");
}

inline void queue_key(SDL_Keycode key, KeyEvent action)
{
    if (action == KeyEvent::None) {
        return;
    }
    SDL_Event event {};
    event.type = action == KeyEvent::Release ? SDL_KEYUP : SDL_KEYDOWN;
    event.key.keysym.sym = key;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    event.key.state = action == KeyEvent::Release ? SDL_RELEASED : SDL_PRESSED;
    event.key.repeat = action == KeyEvent::Repeat ? 1 : 0;
    if (SDL_PushEvent(&event) != 1) {
        throw std::runtime_error("SDL_PushEvent failed");
    }
    if (action == KeyEvent::Tap) {
        queue_key(key, KeyEvent::Release);
    }
}

template <typename Observe>
void replay(std::string_view id, Renderer& renderer, Game& game, Observe observe)
{
    if (!supports(id)) {
        throw std::runtime_error("unknown high-score UI case");
    }

    // Controlled entry setup only. All subsequent input uses the SDL queue.
    auto& world = game.diagnostic_world();
    const bool attract = id == "high_score_attract_held_enter";
    world.screen = attract ? Screen::HighScores : Screen::HighScoreEntry;
    world.active_high_score_index = attract ? -1 : 0;
    world.high_scores[0].name = "Jack";
    world.high_scores[0].highlighted = !attract;
    world.high_score_frames_remaining = attract ? kCompletedTableTicks : 0;
    world.fast_shots_enabled = false;

    int tick = 0;
    const auto record = [&](KeyEvent event, SDL_Keycode key) {
        const auto input = renderer.input_state();
        observe(Sample {tick, event, key, world.screen,
                        world.high_score_frames_remaining, world.active_high_score_index,
                        world.high_scores[0].highlighted, input.name_submit,
                        input.start, input.start_repeat, world.control_panel_row,
                        world.control_panel_pending_action_row, world.high_scores[0].name});
    };
    const auto step = [&](KeyEvent event, SDL_Keycode key = SDLK_RETURN) {
        queue_key(key, event);
        renderer.pump_events();
        game.tick(renderer.input_state());
        ++tick;
        record(event, event == KeyEvent::None ? SDLK_UNKNOWN : key);
    };
    record(KeyEvent::None, SDLK_UNKNOWN);

    if (attract) {
        step(KeyEvent::Press);
        step(KeyEvent::Repeat);
        step(KeyEvent::Repeat);
        step(KeyEvent::Release);
        return;
    }

    if (id == "high_score_main_enter_repeat") {
        step(KeyEvent::Press);
        step(KeyEvent::Repeat);
        step(KeyEvent::Repeat);
        step(KeyEvent::Release);
        step(KeyEvent::Press);
        step(KeyEvent::Release);
        return;
    }

    if (id == "high_score_main_enter_tap" || id == "high_score_completed_table_dwell") {
        step(KeyEvent::Tap);
        const int ticks = id == "high_score_completed_table_dwell" ? kCompletedTableTicks : 1;
        for (int index = 0; index < ticks; ++index) {
            step(KeyEvent::None);
        }
        return;
    }

    step(KeyEvent::Press);
    step(KeyEvent::Release);
    const auto key = id == "high_score_dismiss_enter" ? SDLK_RETURN :
        id == "high_score_dismiss_space" ? SDLK_SPACE : SDLK_TAB;
    step(KeyEvent::Press, key);
    step(KeyEvent::Release, key);
}

} // namespace niteraid::testing::high_score_ui
