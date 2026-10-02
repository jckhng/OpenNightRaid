#define SDL_MAIN_HANDLED

#include <SDL.h>

#include "niteraid/game.hpp"
#include "niteraid/renderer.hpp"
#include "niteraid/state_serializer.hpp"
#include "ui_high_score_scenarios.hpp"
#include "ui_terminal_scenarios.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace high_score_ui = niteraid::testing::high_score_ui;
namespace terminal_ui = niteraid::testing::terminal_ui;

constexpr int kInterActionMilliseconds = 150;
constexpr std::string_view kConfigStartupCase = "config_startup";
constexpr std::string_view kConfigImportCase = "config_startup_import";
constexpr std::string_view kMenuConfigCase = "menu_config_save";
constexpr std::string_view kMenuCycleCase = "menu_config_cycle";
constexpr std::string_view kMenuRapidCycleCase = "menu_config_cycle_rapid";
constexpr std::string_view kMenuReadyRapidCycleCase = "menu_config_cycle_ready_rapid";
constexpr std::string_view kMenuHeldCase = "menu_config_held_navigation";
constexpr std::string_view kMenuPointerCase = "menu_config_pointer_navigation";
constexpr std::string_view kMenuButtonsCase = "menu_config_buttons";
constexpr std::string_view kMenuVoicesOffCase = "menu_config_options_voices_off";
constexpr std::string_view kMenuVoicesFourCase = "menu_config_options_voices_four";
constexpr std::string_view kMenuVoicesOneCase = "menu_config_options_voices_one";
constexpr int kHeldMenuRepeats = 3;
constexpr int kMenuConfigActionGapTicks = 40;

void write_state(const std::filesystem::path& path,
                 const niteraid::WorldState& world);

bool option_matrix_case(std::string_view id)
{
    return id == kMenuVoicesOffCase || id == kMenuVoicesFourCase || id == kMenuVoicesOneCase;
}

constexpr int capture_duration_ticks(int deciseconds)
{
    return (static_cast<int>(niteraid::kSimulationRateHz) * deciseconds + 5) / 10;
}

constexpr int inter_action_ticks()
{
    return (static_cast<int>(niteraid::kSimulationRateHz) *
            kInterActionMilliseconds + 500) / 1000;
}

enum class KeyDelivery { Initial, Repeat };

void push_key(SDL_Keycode key, bool down, KeyDelivery delivery = KeyDelivery::Initial)
{
    SDL_Event event {};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.sym = key;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = delivery == KeyDelivery::Repeat ? 1 : 0;
    if (SDL_PushEvent(&event) != 1) {
        throw std::runtime_error("SDL_PushEvent failed");
    }
}

void step(niteraid::Renderer& renderer, niteraid::Game& game)
{
    renderer.pump_events();
    game.tick(renderer.input_state());
    game.prepare_gameplay_page();
    renderer.render(game.world());
    renderer.present();
}

void idle(niteraid::Renderer& renderer, niteraid::Game& game, int ticks)
{
    for (int index = 0; index < ticks; ++index) {
        step(renderer, game);
    }
}

void tap_for_deciseconds(niteraid::Renderer& renderer,
                         niteraid::Game& game,
                         SDL_Keycode key,
                         int deciseconds)
{
    const int held_ticks = capture_duration_ticks(deciseconds);
    push_key(key, true);
    step(renderer, game);
    idle(renderer, game, held_ticks - 1);
    push_key(key, false);
    step(renderer, game);
    idle(renderer, game, inter_action_ticks());
}

void hold(niteraid::Renderer& renderer, niteraid::Game& game,
          SDL_Keycode key, int ticks)
{
    push_key(key, true);
    step(renderer, game);
    idle(renderer, game, ticks - 1);
    push_key(key, false);
    step(renderer, game);
}

void write_control_panel_progression(
    const std::filesystem::path& path,
    const std::vector<std::pair<const char*, int>>& samples)
{
    std::ofstream output(path);
    output << "[\n";
    for (std::size_t index = 0; index < samples.size(); ++index) {
        output << "  {\"step\": \"" << samples[index].first
               << "\", \"control_panel_row\": " << samples[index].second << "}"
               << (index + 1 == samples.size() ? "\n" : ",\n");
    }
    output << "]\n";
    if (!output) {
        throw std::runtime_error("failed to write control-panel progression");
    }
}

void replay_high_score(const std::string& id,
                       niteraid::Renderer& renderer,
                       niteraid::Game& game,
                       const std::filesystem::path& output_dir)
{
    std::ofstream output(output_dir / "high_score_replay.json");
    output << std::boolalpha
           << "{\n  \"format_version\": 1,\n"
           << "  \"case\": " << std::quoted(id) << ",\n"
           << "  \"entry_kind\": "
           << std::quoted(id == "high_score_attract_held_enter" ?
                          "controlled_attract_table" : "controlled_name_entry") << ",\n"
           << "  \"input_source\": \"SDL_PushEvent\",\n"
           << "  \"natural_claim\": false,\n"
           << "  \"original_proof\": "
           << std::quoted(id == "high_score_attract_held_enter" ?
                          "separate_fresh_capture_not_paired" : "absent") << ",\n"
           << "  \"original_comparison\": \"not_performed\",\n"
           << "  \"promotion_eligible\": false,\n"
           << "  \"config_isolated\": true,\n"
           << "  \"native_dwell_contract_ticks\": " << high_score_ui::kCompletedTableTicks << ",\n"
           << "  \"timing_scope\": \"simulation_ticks_not_original_hardware_time\",\n"
           << "  \"samples\": [\n";
    high_score_ui::replay(id, renderer, game, [&](const high_score_ui::Sample& sample) {
        renderer.render(game.world());
        renderer.present();
        if (sample.tick != 0) {
            output << ",\n";
        }
        output << "    {\"tick\": " << sample.tick
               << ", \"event\": " << std::quoted(high_score_ui::event_name(sample.event))
               << ", \"sdl_keycode\": " << sample.key
               << ", \"screen\": " << std::quoted(niteraid::to_string(sample.screen))
               << ", \"high_score_frames_remaining\": " << sample.remaining
               << ", \"active_high_score_index\": " << sample.active_index
               << ", \"highlighted\": " << sample.highlighted
               << ", \"name_submit\": " << sample.name_submit
               << ", \"start\": " << sample.start
               << ", \"start_repeat\": " << sample.start_repeat
               << ", \"control_panel_row\": " << sample.control_panel_row
               << ", \"control_panel_pending_action_row\": "
               << sample.control_panel_pending_action_row
               << ", \"name\": " << std::quoted(sample.name) << "}";
    });
    output << "\n  ]\n}\n";
    output.close();
    if (!output) {
        throw std::runtime_error("failed to write high-score replay");
    }
}

void replay_case(const std::string& id,
                 niteraid::Renderer& renderer,
                 niteraid::Game& game,
                 const std::filesystem::path& output_dir)
{
    const bool qualification = id == terminal_ui::kQualificationCase || id == terminal_ui::kQualificationSaveCase;
    if (id == kMenuButtonsCase) {
        std::ofstream output(output_dir / "menu_buttons.json");
        output << std::boolalpha << "{\"schema\":\"niteraid/native-menu-buttons/v1\","
                  "\"input_source\":\"SDL_PushEvent\",\"config_isolated\":true,"
                  "\"state_imported\":false,\"acceptance_promotable\":false,\"samples\":[";
        bool first = true;
        const auto sample = [&](const std::string& event) {
            const auto& world = game.world();
            if (!first) {
                output << ',';
            }
            first = false;
            output << "{\"event\":" << std::quoted(event)
                   << ",\"panel_row\":" << world.control_panel_row
                   << ",\"prompt\":" << (world.confirmation_prompt != niteraid::ConfirmationPromptAction::None)
                   << ",\"quit_requested\":" << world.quit_requested
                   << ",\"pending_option_row\":" << world.control_panel_pending_option_row
                   << ",\"displayed_sound\":" << (world.control_panel_pending_option_row == 0 ?
                       world.control_panel_pending_option_value : world.audio_config_words[3])
                   << ",\"config_words\":[";
            for (std::size_t word = 0; word < world.audio_config_words.size(); ++word) {
                if (word != 0) {
                    output << ',';
                }
                output << world.audio_config_words[word];
            }
            output << "]}";
            write_state(output_dir / ("buttons_state_" + event + ".bin"), world);
            if (!renderer.save_frame_bmp((output_dir / ("buttons_frame_" + event + ".bmp")).string().c_str())) {
                throw std::runtime_error("failed to capture native button page");
            }
        };
        const auto key = [&](SDL_Keycode code) {
            push_key(code, true);
            step(renderer, game);
            push_key(code, false);
            step(renderer, game);
            idle(renderer, game, kMenuConfigActionGapTicks);
        };
        const auto button = [&](Uint8 code, bool down) {
            SDL_Event event {};
            event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            event.button.button = code;
            if (SDL_PushEvent(&event) != 1) {
                throw std::runtime_error("failed to queue native menu button");
            }
            step(renderer, game);
        };
        key(SDLK_RETURN);
        sample("title_poll_0");
        for (const auto& label : {"up_music_poll_0", "up_voice_poll_0", "up_sound_poll_0"}) {
            key(SDLK_UP);
            sample(label);
        }
        const std::pair<const char*, Uint8> buttons[] {
            {"right", SDL_BUTTON_RIGHT}, {"middle", SDL_BUTTON_MIDDLE}, {"left", SDL_BUTTON_LEFT},
        };
        constexpr int kHoldObservations = 3;
        for (const auto& [label, code] : buttons) {
            button(code, true);
            for (int hold = 0; hold < kHoldObservations; ++hold) {
                if (hold != 0) {
                    step(renderer, game);
                }
                sample(std::string(label) + "_held_" + std::to_string(hold));
            }
            button(code, false);
            step(renderer, game);
            sample(std::string(label) + "_released_poll_0");
        }
        for (const auto& [label, code] : {std::pair {"cancel", SDL_BUTTON_RIGHT}, std::pair {"confirm", SDL_BUTTON_LEFT}}) {
            key(SDLK_ESCAPE);
            sample(std::string(label) + "_prompt_ready");
            button(static_cast<Uint8>(code), true);
            for (int hold = 0; hold < kHoldObservations; ++hold) {
                if (hold != 0) {
                    step(renderer, game);
                }
                sample(std::string(label) + "_prompt_held_" + std::to_string(hold));
            }
            button(static_cast<Uint8>(code), false);
            step(renderer, game);
            sample(std::string_view(label) == "cancel" ? "cancel_return_poll_0" : "save_return");
        }
        output << "]}\n";
        if (!output) {
            throw std::runtime_error("failed to write native button replay");
        }
        return;
    }
    if (id == kMenuConfigCase || id == kMenuCycleCase || id == kMenuRapidCycleCase ||
        id == kMenuReadyRapidCycleCase || id == kMenuHeldCase || id == kMenuPointerCase || option_matrix_case(id)) {
        struct Action {
            const char* label;
            SDL_Keycode key;
            int pointer_y = -1;
        };
        std::vector<Action> actions = id == kMenuConfigCase ? std::vector<Action> {
            {"title", SDLK_RETURN}, {"music_row", SDLK_UP}, {"music", SDLK_RETURN},
            {"voice_row", SDLK_UP}, {"voice", SDLK_RETURN}, {"sound_row", SDLK_UP},
            {"sound", SDLK_RETURN}, {"quit", SDLK_ESCAPE}, {"confirm", SDLK_RETURN},
        } : id == kMenuPointerCase ? std::vector<Action> {
            {"title", SDLK_RETURN}, {"pointer_top", SDLK_UNKNOWN, 0}, {"sound_off", SDLK_RETURN},
            {"pointer_bottom", SDLK_UNKNOWN, 9999}, {"pointer_top_again", SDLK_UNKNOWN, 0},
            {"sound_speaker", SDLK_RETURN}, {"pointer_bottom_again", SDLK_UNKNOWN, 9999},
            {"quit", SDLK_ESCAPE}, {"confirm", SDLK_RETURN},
        } : id == kMenuHeldCase ? std::vector<Action> {
            {"title", SDLK_RETURN}, {"held_up", SDLK_UP}, {"sound", SDLK_RETURN},
            {"quit", SDLK_ESCAPE}, {"confirm", SDLK_RETURN},
        } : std::vector<Action> {
            {"title", SDLK_RETURN}, {"up_one", SDLK_UP}, {"up_two", SDLK_UP}, {"up_three", SDLK_UP},
            {"sound_off", SDLK_RETURN}, {"sound_speaker", SDLK_RETURN}, {"sound_next", SDLK_RETURN},
            {"sound_wrap", SDLK_RETURN}, {"down_skipped", SDLK_DOWN}, {"up_skipped", SDLK_UP},
            {"sound_restore_one", SDLK_RETURN}, {"sound_restore_two", SDLK_RETURN},
            {"down_enabled", SDLK_DOWN}, {"voice", SDLK_RETURN}, {"music_row", SDLK_DOWN},
            {"music", SDLK_RETURN}, {"quit", SDLK_ESCAPE}, {"confirm", SDLK_RETURN},
        };
        if (option_matrix_case(id)) {
            actions = {{"title", SDLK_RETURN}, {"up_music", SDLK_UP}, {"up_voice", SDLK_UP}};
            if (id != kMenuVoicesOneCase) {
                actions.push_back({"voice_step_1", SDLK_RETURN});
            }
            if (id == kMenuVoicesFourCase) {
                actions.push_back({"voice_step_2", SDLK_RETURN});
            }
            const std::vector<Action> tail {
                {"up_sound", SDLK_UP}, {"blaster_down_voice", SDLK_DOWN},
                {"blaster_down_music", SDLK_DOWN}, {"blaster_music_flip", SDLK_RETURN},
                {"blaster_up_voice", SDLK_UP}, {"blaster_up_sound", SDLK_UP},
                {"sound_off", SDLK_RETURN}, {"off_down_music", SDLK_DOWN},
                {"off_music_flip", SDLK_RETURN}, {"off_up_sound", SDLK_UP},
                {"sound_speaker", SDLK_RETURN}, {"speaker_down_music", SDLK_DOWN},
                {"speaker_music_flip", SDLK_RETURN}, {"speaker_up_sound", SDLK_UP},
                {"sound_restore", SDLK_RETURN}, {"restore_down_voice", SDLK_DOWN},
                {"restore_down_music", SDLK_DOWN}, {"restore_music_flip", SDLK_RETURN},
                {"final_music_flip", SDLK_RETURN}, {"quit", SDLK_ESCAPE}, {"confirm", SDLK_RETURN},
            };
            actions.insert(actions.end(), tail.begin(), tail.end());
        }
        std::ofstream output(output_dir / "menu_config.json");
        output << std::boolalpha
               << "{\"schema\":\"niteraid/native-menu-config/v1\",\"case\":" << std::quoted(id) << ','
               << "\"config_isolated\":true,\"state_imported\":false,\"promotion_eligible\":false,"
               << "\"input_source\":\"SDL_PushEvent\",\"samples\":[";
        int entry_ready_idle_ticks = 0;
        for (std::size_t index = 0; index < std::size(actions); ++index) {
            const auto& action = actions[index];
            if (action.pointer_y >= 0) {
                SDL_Event motion {};
                motion.type = SDL_MOUSEMOTION;
                motion.motion.y = action.pointer_y;
                if (SDL_PushEvent(&motion) != 1) {
                    throw std::runtime_error("failed to queue menu pointer motion");
                }
                step(renderer, game);
            } else {
                push_key(action.key, true);
                step(renderer, game);
            }
            if (std::string_view(action.label) == "held_up") {
                for (int repeat = 0; repeat < kHeldMenuRepeats; ++repeat) {
                    push_key(action.key, true, KeyDelivery::Repeat);
                    step(renderer, game);
                }
            }
            if (action.pointer_y < 0) {
                push_key(action.key, false);
            }
            step(renderer, game);
            if (id == kMenuReadyRapidCycleCase && index == 0) {
                while (game.world().screen_fade_frames_remaining > 0 && entry_ready_idle_ticks < niteraid::kScreenFadeFrames) {
                    step(renderer, game);
                    ++entry_ready_idle_ticks;
                }
                if (game.world().screen != niteraid::Screen::ControlPanel || game.world().screen_fade_frames_remaining != 0) {
                    throw std::runtime_error("menu entry did not reach bounded readiness");
                }
            } else {
                const bool rapid = id == kMenuRapidCycleCase || id == kMenuReadyRapidCycleCase;
                idle(renderer, game, rapid && action.key != SDLK_ESCAPE ? 0 : kMenuConfigActionGapTicks);
            }
            write_state(output_dir / (std::string("menu_state_") + action.label + ".bin"), game.world());
            const auto frame_path = output_dir / (std::string("menu_frame_") + action.label + ".bmp");
            if (!renderer.save_frame_bmp(frame_path.string().c_str())) {
                throw std::runtime_error("failed to write menu frame capture");
            }
            if (index != 0) {
                output << ',';
            }
            output << "{\"event\":" << std::quoted(std::string("after_") + action.label)
                   << ",\"screen\":" << std::quoted(niteraid::to_string(game.world().screen))
                   << ",\"panel_row\":" << game.world().control_panel_row
                   << ",\"fade_remaining\":" << game.world().screen_fade_frames_remaining
                   << ",\"quit_prompt\":" << (game.world().confirmation_prompt == niteraid::ConfirmationPromptAction::QuitToDos)
                   << ",\"quit_requested\":" << game.world().quit_requested
                   << ",\"config_written\":" << std::filesystem::exists("reimpl/save/CONFIG.NTR")
                   << ",\"config_words\":[";
            for (std::size_t word = 0; word < game.world().audio_config_words.size(); ++word) {
                if (word != 0) {
                    output << ',';
                }
                output << game.world().audio_config_words[word];
            }
            output << "]}";
        }
        niteraid::Game reloaded;
        write_state(output_dir / "menu_reloaded_state.bin", reloaded.world());
        output << "],\"entry_ready_idle_ticks\":" << entry_ready_idle_ticks << ",\"reloaded_config_words\":[";
        for (std::size_t word = 0; word < reloaded.world().audio_config_words.size(); ++word) {
            if (word != 0) {
                output << ',';
            }
            output << reloaded.world().audio_config_words[word];
        }
        output << "]}\n";
        if (!output) {
            throw std::runtime_error("failed to write menu config replay");
        }
        return;
    }
    if (id == kConfigStartupCase || id == kConfigImportCase) {
        std::ofstream output(output_dir / "config_startup.json");
        output << std::boolalpha
               << "{\"schema\":\"niteraid/native-config-startup/v1\","
               << "\"case\":" << std::quoted(id) << ",\"config_source\":"
               << std::quoted(id == kConfigImportCase ? "original-import" : "native-save")
               << ",\"config_isolated\":true,"
               << "\"state_imported\":false,\"promotion_eligible\":false,"
               << "\"input_source\":\"none\",\"screen\":" << std::quoted(niteraid::to_string(game.world().screen))
               << ",\"config_words\":[";
        for (std::size_t index = 0; index < game.world().audio_config_words.size(); ++index) {
            if (index != 0) {
                output << ',';
            }
            output << game.world().audio_config_words[index];
        }
        output << "],\"table\":[";
        for (std::size_t index = 0; index < game.world().high_scores.size(); ++index) {
            const auto& entry = game.world().high_scores[index];
            if (index != 0) {
                output << ',';
            }
            output << "{\"name\":" << std::quoted(entry.name) << ",\"score\":" << entry.score
                   << ",\"stats\":[" << entry.stat_a << ',' << entry.stat_b << ',' << entry.stat_c
                   << "],\"highlight\":" << entry.highlighted << '}';
        }
        output << "]}\n";
        if (!output) {
            throw std::runtime_error("failed to write config startup state");
        }
        return;
    }
    if (id == terminal_ui::kCase || id == terminal_ui::kRestartCase || qualification) {
        std::ofstream output(output_dir / "terminal_replay.json");
        output << std::boolalpha
               << "{\n  \"format_version\": 1,\n"
               << "  \"case\": " << std::quoted(id) << ",\n"
               << "  \"entry_kind\": " << std::quoted(id == terminal_ui::kRestartCase ?
                    "native_natural_loss_to_restart" : id == terminal_ui::kQualificationSaveCase ?
                    "native_loss_to_qualified_entry_then_quit" : qualification ?
                    "native_loss_to_qualified_entry_then_title" : "native_natural_loss_to_title") << ",\n"
               << "  \"input_source\": \"SDL_PushEvent\",\n"
               << "  \"state_imported\": false,\n"
               << "  \"prelevel_seed\": " << terminal_ui::kPrelevelSeed << ",\n"
               << "  \"config_isolated\": true,\n"
               << "  \"original_comparison\": \"not_performed\",\n"
               << "  \"promotion_eligible\": false,\n"
               << "  \"timing_scope\": \"native_pump_ticks_not_original_hardware_time\",\n"
               << "  \"samples\": [\n";
        bool first = true;
        terminal_ui::replay(renderer, game, [&](const terminal_ui::Sample& sample) {
            if (!first) {
                output << ",\n";
            }
            first = false;
            output << "    {\"boundary\": " << std::quoted(sample.boundary)
                   << ", \"tick\": " << sample.tick
                   << ", \"frame_tick\": " << sample.frame_tick
                   << ", \"screen\": " << std::quoted(niteraid::to_string(sample.screen))
                   << ", \"score\": " << sample.score
                   << ", \"rng_seed\": " << sample.rng_seed
                   << ", \"high_score_frames_remaining\": " << sample.remaining
                   << ", \"active_high_score_index\": " << sample.active_index
                   << ", \"terminal_draw\": " << sample.terminal_draw
                   << ", \"name_submit\": " << sample.name_submit
                   << ", \"start\": " << sample.start
                   << ", \"control_panel_row\": " << sample.panel_row
                   << ", \"control_panel_pending_action_row\": " << sample.pending_action
                   << ", \"first_spawn_deadline\": " << sample.first_spawn_deadline;
            if (qualification) {
                output << ", \"config_saved\": " << std::filesystem::exists("reimpl/save/CONFIG.NTR")
                       << ", \"quit_requested\": " << game.world().quit_requested
                       << ", \"confirmation_prompt_id\": "
                       << (game.world().confirmation_prompt == niteraid::ConfirmationPromptAction::QuitToDos ? 0x65B : 0);
                output << ", \"table\": [";
                for (std::size_t index = 0; index < game.world().high_scores.size(); ++index) {
                    const auto& entry = game.world().high_scores[index];
                    if (index != 0) {
                        output << ',';
                    }
                    output << "{\"name\":" << std::quoted(entry.name) << ",\"score\":" << entry.score
                           << ",\"stats\":[" << entry.stat_a << ',' << entry.stat_b << ',' << entry.stat_c
                           << "],\"highlight\":" << entry.highlighted << '}';
                }
                output << ']';
            }
            output << '}';
        }, id == terminal_ui::kRestartCase ? terminal_ui::Destination::Restart :
           id == terminal_ui::kQualificationSaveCase ? terminal_ui::Destination::QualificationSave :
           qualification ? terminal_ui::Destination::Qualification : terminal_ui::Destination::Title);
        output << "\n  ]";
        if (id == terminal_ui::kQualificationSaveCase) {
            niteraid::Game reloaded;
            output << ",\n  \"reloaded_table\": [";
            for (std::size_t index = 0; index < reloaded.world().high_scores.size(); ++index) {
                const auto& entry = reloaded.world().high_scores[index];
                if (index != 0) {
                    output << ',';
                }
                output << "{\"name\":" << std::quoted(entry.name) << ",\"score\":" << entry.score
                       << ",\"stats\":[" << entry.stat_a << ',' << entry.stat_b << ',' << entry.stat_c
                       << "],\"highlight\":" << entry.highlighted << '}';
            }
            output << "] ,\n  \"reloaded_config_words\": [";
            for (std::size_t index = 0; index < reloaded.world().audio_config_words.size(); ++index) {
                if (index != 0) {
                    output << ',';
                }
                output << reloaded.world().audio_config_words[index];
            }
            output << ']';
        }
        output << "\n}\n";
        output.close();
        if (!output) {
            throw std::runtime_error("failed to write natural terminal replay");
        }
        return;
    }
    if (high_score_ui::supports(id)) {
        replay_high_score(id, renderer, game, output_dir);
        return;
    }
    idle(renderer, game, 20);
    if (id == "prompt_cancel_n_release") {
        hold(renderer, game, SDLK_ESCAPE, 60);
        idle(renderer, game, 20);
        tap_for_deciseconds(renderer, game, SDLK_ESCAPE, 2);
        tap_for_deciseconds(renderer, game, SDLK_n, 2);
        idle(renderer, game, 48);
        return;
    }
    if (id == "option_cancel_release") {
        std::vector<std::pair<const char*, int>> progression;
        const auto record = [&]() {
            progression.emplace_back("control_panel_ready",
                                     game.world().control_panel_row);
        };
        hold(renderer, game, SDLK_SPACE, 60);
        idle(renderer, game, 20);
        record();
        tap_for_deciseconds(renderer, game, SDLK_UP, 1);
        progression.emplace_back("after_up1", game.world().control_panel_row);
        tap_for_deciseconds(renderer, game, SDLK_UP, 1);
        progression.emplace_back("after_up2", game.world().control_panel_row);
        tap_for_deciseconds(renderer, game, SDLK_UP, 1);
        progression.emplace_back("after_up3", game.world().control_panel_row);
        tap_for_deciseconds(renderer, game, SDLK_LEFT, 2);
        progression.emplace_back("after_left", game.world().control_panel_row);
        tap_for_deciseconds(renderer, game, SDLK_ESCAPE, 2);
        tap_for_deciseconds(renderer, game, SDLK_n, 2);
        progression.emplace_back("after_cancel", game.world().control_panel_row);
        write_control_panel_progression(output_dir / "control_panel_progression.json", progression);
        idle(renderer, game, 48);
        return;
    }
    throw std::runtime_error("unknown UI replay case: " + id);
}

void write_state(const std::filesystem::path& path,
                 const niteraid::WorldState& world)
{
    auto bytes = niteraid::serialize_world_state_dat2730(world);
    constexpr std::size_t kScoreTableBase = 0x2062;
    constexpr std::size_t kScoreRecordBytes = 0x4c;
    constexpr std::size_t kScoreNameBytes = 0x40;
    for (std::size_t index = 0; index < world.high_scores.size(); ++index) {
        const auto record = kScoreTableBase + index * kScoreRecordBytes;
        const auto& entry = world.high_scores[index];
        const auto name_size = std::min(entry.name.size(), kScoreNameBytes - 1);
        std::copy_n(entry.name.begin(), name_size, bytes.begin() + static_cast<std::ptrdiff_t>(record));
        const auto write_word = [&](std::size_t offset, std::uint16_t value) {
            bytes.at(record + offset) = static_cast<std::uint8_t>(value);
            bytes.at(record + offset + 1) = static_cast<std::uint8_t>(value >> 8);
        };
        const auto score = static_cast<std::uint32_t>(entry.score);
        write_word(0x40, static_cast<std::uint16_t>(score));
        write_word(0x42, static_cast<std::uint16_t>(score >> 16));
        write_word(0x44, entry.stat_a);
        write_word(0x46, entry.stat_b);
        write_word(0x48, entry.stat_c);
        write_word(0x4a, entry.highlighted ? 1 : 0);
    }
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("failed to write state dump");
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: niteraid_ui_replay_probe CASE OUTPUT_DIR\n";
        std::cerr << "native-only high-score cases (original proof absent):\n";
        for (const auto id : high_score_ui::kCases) {
            std::cerr << "  " << id << '\n';
        }
        std::cerr << "  " << terminal_ui::kCase << " (native natural loss)\n";
        std::cerr << "  " << terminal_ui::kRestartCase << " (native natural loss/restart)\n";
        std::cerr << "  " << terminal_ui::kQualificationCase << " (requires CONFIG argument)\n";
        std::cerr << "  " << terminal_ui::kQualificationSaveCase << " (requires CONFIG argument)\n";
        std::cerr << "  " << kConfigStartupCase << " (requires CONFIG argument)\n";
        std::cerr << "  " << kConfigImportCase << " (requires original CONFIG argument)\n";
        return 2;
    }

    try {
        const std::string id = argv[1];
        const bool qualification = id == terminal_ui::kQualificationCase || id == terminal_ui::kQualificationSaveCase;
        const bool config_startup = id == kConfigStartupCase || id == kConfigImportCase ||
            id == kMenuConfigCase || id == kMenuCycleCase || id == kMenuRapidCycleCase ||
            id == kMenuReadyRapidCycleCase || id == kMenuHeldCase || id == kMenuPointerCase || id == kMenuButtonsCase ||
            id == "prompt_cancel_n_release" || id == "option_cancel_release" || option_matrix_case(id);
        if ((argc == 4) != (qualification || config_startup)) {
            throw std::runtime_error("qualification/config startup requires a declared pre-start CONFIG argument");
        }
        const auto output_dir = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(output_dir);
        niteraid::Renderer renderer(false, false, false);
        if (!renderer.is_interactive()) {
            throw std::runtime_error("SDL renderer is not interactive");
        }
        const bool natural_terminal = id == terminal_ui::kCase || id == terminal_ui::kRestartCase || qualification;
        if (high_score_ui::supports(id) || natural_terminal || config_startup) {
            // Persistence is relative to cwd. Never use the caller's save.
            const auto save_dir = output_dir / "isolated-save";
            if (!std::filesystem::create_directory(save_dir)) {
                throw std::runtime_error("high-score replay requires a fresh output directory");
            }
            if (natural_terminal || config_startup) {
                const auto staged_config = qualification || config_startup ? std::filesystem::absolute(argv[3]) :
                    std::filesystem::current_path() / "original/NITERAID/CONFIG.NTR";
                const auto isolated_config = save_dir / (id == kConfigStartupCase ? "reimpl/save/CONFIG.NTR" : "original/NITERAID/CONFIG.NTR");
                std::filesystem::create_directories(isolated_config.parent_path());
                std::filesystem::copy_file(staged_config, isolated_config);
            }
            std::filesystem::current_path(save_dir);
        }
        niteraid::Game game(natural_terminal ? std::optional<std::uint16_t> {0} : std::nullopt,
                           std::nullopt, false, false, false, false,
                           natural_terminal ? std::optional<std::uint32_t> {terminal_ui::kPrelevelSeed} : std::nullopt);
        replay_case(id, renderer, game, output_dir);
        renderer.render(game.world());
        renderer.present();
        write_state(output_dir / "current_state.bin", game.world());
        if (!renderer.save_frame_bmp((output_dir / "current_frame.bmp").string().c_str())) {
            throw std::runtime_error("failed to write frame capture");
        }
        std::ofstream metadata(output_dir / "current_state.json");
        metadata << "{\n"
                 << "  \"screen\": \"" << niteraid::to_string(game.world().screen) << "\",\n"
                 << "  \"control_panel_row\": " << static_cast<int>(game.world().control_panel_row) << ",\n"
                 << "  \"confirmation_prompt_active\": "
                 << (game.world().confirmation_prompt == niteraid::ConfirmationPromptAction::None ? 0 : 1)
                 << ",\n  \"confirmation_prompt_string_id\": "
                 << (game.world().confirmation_prompt == niteraid::ConfirmationPromptAction::ReturnToTitle ? 0x0643 :
                     game.world().confirmation_prompt == niteraid::ConfirmationPromptAction::QuitToDos ? 0x065b : 0)
                 << "\n}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ui replay probe error: " << error.what() << "\n";
        return 1;
    }
}
