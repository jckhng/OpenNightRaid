#include "niteraid/app.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "niteraid/audio.hpp"
#include "niteraid/game.hpp"
#include "niteraid/game_internals.hpp"
#include "niteraid/gameplay_clock.hpp"
#include "niteraid/input_schedule.hpp"
#include "niteraid/ntr_assets.hpp"
#include "niteraid/presenter_timing.hpp"
#if defined(NITERAID_REMAKE)
#include "niteraid/presentation.hpp"
#endif
#include "niteraid/renderer.hpp"
#include "niteraid/state_serializer.hpp"
#include "niteraid/survivor_presenter.hpp"
#include "niteraid/finale_particles.hpp"
#include "niteraid/finale_presenter.hpp"
#include "niteraid/pizza_presenter.hpp"
#include "niteraid/helicopter_presenter.hpp"

namespace niteraid {

namespace {

void write_scripted_presenter_actors(std::ostream& output,
                                    const std::vector<ScriptedPresenterActor>& actors)
{
    output << "[";
    bool first = true;
    for (const auto& actor : actors) {
        if (!first) output << ",";
        first = false;
        output << "{\"slot\": " << actor.slot << ", \"type_id\": " << actor.type_id
            << ", \"x_fixed\": " << actor.x_fixed << ", \"y_fixed\": " << actor.y_fixed
            << ", \"vx_fixed\": " << actor.vx_fixed << ", \"vy_fixed\": " << actor.vy_fixed
            << ", \"sprite\": " << actor.sprite << ", \"counter\": " << actor.counter
            << ", \"animation\": " << actor.animation << ", \"timer\": " << actor.timer
            << ", \"update_callback\": " << actor.update_callback
            << ", \"draw_callback\": " << actor.draw_callback << "}";
    }
    output << "]";
}

#if defined(NITERAID_REMAKE)
constexpr bool kEnhancedInputDebounce = true;
#else
constexpr bool kEnhancedInputDebounce = false;
#endif

bool extracted_asset_bundle_available(const std::filesystem::path& assets_dir)
{
    std::error_code error;
    const bool has_bunker = std::filesystem::is_regular_file(
        assets_dir / "sprites" / "sprite_1df.bmp", error);
    error.clear();
    const bool has_music = std::filesystem::is_regular_file(
        assets_dir / "audio" / "sprite_318.bin", error);
    error.clear();
    const bool has_fullscreen = std::filesystem::is_directory(
        assets_dir / "fullscreen", error);
    return has_bunker && has_music && has_fullscreen;
}

bool is_deterministic_capture_run(const LaunchOptions& options);
bool requires_public_asset_resources(const LaunchOptions& options);

bool legacy_asset_bundle_available(const LaunchOptions& options)
{
    const auto working_directory_assets = std::filesystem::current_path() / "assets";
    if (extracted_asset_bundle_available(working_directory_assets)) {
        return true;
    }
    if (options.executable_path.empty()) {
        return false;
    }
    const auto executable = std::filesystem::absolute(options.executable_path);
    return extracted_asset_bundle_available(executable.parent_path() / "assets");
}

bool configure_adjacent_original_assets(const LaunchOptions& options)
{
    if (!options.auto_load_original_assets) {
        return legacy_asset_bundle_available(options);
    }

    if (!options.original_graphics_path.empty()) {
        const auto graphics = std::filesystem::absolute(options.original_graphics_path);
        if (!configure_original_asset_archive(graphics)) {
            throw std::runtime_error("unsupported or invalid --graphics-archive file");
        }
        return true;
    }
    if (options.executable_path.empty()) {
        return legacy_asset_bundle_available(options);
    }

    const auto executable = std::filesystem::absolute(options.executable_path);
    const auto scope = requires_public_asset_resources(options)
        ? ArchiveSearchScope::AdjacentOnly : ArchiveSearchScope::Development;
    const auto graphics = find_original_asset_archive(executable, std::filesystem::current_path(), scope);
    if (graphics.empty()) {
        return legacy_asset_bundle_available(options);
    }
    if (!configure_original_asset_archive(graphics)) {
        throw std::runtime_error("unsupported or invalid Night Raid graphics archive: " + graphics.string());
    }
    return true;
}

bool requires_public_asset_resources(const LaunchOptions& options)
{
    // Diagnostic and bounded capture modes intentionally exercise fallback logic
    // without requiring the user's original archive.
    return options.diagnostic_screen.empty() && options.render_enabled &&
           !options.max_frames.has_value() && !is_deterministic_capture_run(options) &&
           options.inject_input.empty() && options.survivor_update_schedule_path.empty() &&
           options.finale_hardware_schedule_path.empty();
}

void ensure_parent_directory(const std::filesystem::path& path)
{
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
}

InputState with_injected_input(InputState input, const LaunchOptions& options, std::uint32_t next_tick)
{
    if (!options.inject_input_at_tick.has_value() ||
        next_tick < *options.inject_input_at_tick ||
        next_tick > options.inject_input_until_tick.value_or(*options.inject_input_at_tick)) {
        return input;
    }

    if (options.inject_input == "autoplay") {
        const auto first_tick = *options.inject_input_at_tick;
        const std::uint32_t block = (next_tick - first_tick) / 18;
        std::uint32_t random = options.injected_input_seed ^ (block * 0x9e3779b9U);
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        switch (random % 5) {
        case 0:
        case 1:
            input.move_left = true;
            break;
        case 2:
        case 3:
            input.move_right = true;
            break;
        default:
            break;
        }
        input.fire = ((random >> 8) & 3U) != 0;
    } else if (options.inject_input == "escape" || options.inject_input == "esc") {
        input.escape = true;
    } else if (options.inject_input == "space" || options.inject_input == "start") {
        input.start = true;
    } else if (options.inject_input == "ctrl" || options.inject_input == "fire") {
        input.fire = true;
    } else if (options.inject_input == "menu") {
        input.menu = true;
    }
    return input;
}

bool is_deterministic_capture_run(const LaunchOptions& options)
{
    return !options.dump_state_path.empty() ||
           !options.dump_object_summary_path.empty() ||
           !options.dump_gameplay_trace_path.empty() ||
           !options.dump_frame_path.empty() ||
           !options.dump_frame_sequence_dir.empty() ||
           !options.dump_mixed_audio_path.empty();
}

void retain_or_seed_diagnostic_player(WorldState& world)
{
    world.objects.erase(
        std::remove_if(world.objects.begin(), world.objects.end(), [](const Object& object) {
            return object.type != ObjectType::PlayerCannon;
        }),
        world.objects.end());
    if (!world.objects.empty()) {
        return;
    }

    Object player {};
    player.active = true;
    player.type = ObjectType::PlayerCannon;
    player.position = {160.0f, 180.0f};
    player.extent = {13.0f, 6.0f};
    player.frame = 6;
    world.objects.push_back(player);
}

void apply_diagnostic_screen_override(WorldState& world, const LaunchOptions& options)
{
    if (options.diagnostic_screen.empty()) {
        return;
    }

    if (options.diagnostic_screen == "title") {
        world.screen = Screen::Title;
    } else if (options.diagnostic_screen == "credits") {
        world.screen = Screen::Credits;
    } else if (options.diagnostic_screen == "attract-interlude") {
        world.screen = Screen::AttractInterlude;
    } else if (options.diagnostic_screen == "control-panel") {
        world.screen = Screen::ControlPanel;
        // Verified menu references were captured with SoundBlaster selected.
        // CONFIG.NTR is mutable runtime state, so pin only the diagnostic.
        world.audio_config_words[3] = 3;
        world.control_panel_row = static_cast<std::uint8_t>(
            options.diagnostic_control_panel_row.value_or(world.control_panel_row));
        world.control_panel_from_gameplay = false;
        world.control_panel_dirty = false;
        world.control_panel_initial_hint = false;
    } else if (options.diagnostic_screen == "gameplay-control-panel") {
        world.screen = Screen::ControlPanel;
        world.audio_config_words[3] = 3;
        world.audio_config_words[4] = 2;
        world.audio_config_words[5] = 1;
        world.control_panel_row = static_cast<std::uint8_t>(
            options.diagnostic_control_panel_row.value_or(world.control_panel_row));
        world.control_panel_from_gameplay = true;
        world.control_panel_dirty = false;
        world.control_panel_initial_hint = true;
    } else if (options.diagnostic_screen == "control-panel-quit-prompt") {
        world.screen = Screen::ControlPanel;
        world.audio_config_words[3] = 3;
        world.audio_config_words[4] = 2;
        world.audio_config_words[5] = 1;
        world.control_panel_row = 5;
        world.control_panel_from_gameplay = false;
        world.control_panel_dirty = false;
        world.control_panel_initial_hint = false;
        world.confirmation_prompt = ConfirmationPromptAction::QuitToDos;
    } else if (options.diagnostic_screen == "gameplay-stop-prompt") {
        world.screen = Screen::ControlPanel;
        world.audio_config_words[3] = 3;
        world.audio_config_words[4] = 2;
        world.audio_config_words[5] = 1;
        world.control_panel_row = 3;
        world.control_panel_from_gameplay = true;
        world.control_panel_dirty = false;
        world.control_panel_initial_hint = false;
        world.confirmation_prompt = ConfirmationPromptAction::ReturnToTitle;
    } else if (options.diagnostic_screen == "high-scores") {
        world.screen = Screen::HighScores;
    } else if (options.diagnostic_screen == "high-score-entry" ||
               options.diagnostic_screen == "high-score-entry-typed") {
        const std::string entered_name =
            options.diagnostic_screen == "high-score-entry-typed" ? "d" : "";
        world.screen = Screen::HighScoreEntry;
        world.high_scores = {{
            HighScoreEntry {entered_name, 1000, 10, 0, 0, true},
            HighScoreEntry {"Argo Games - 1993", 100, 0, 0, 0, false},
            HighScoreEntry {"", 100, 0, 0, 0, false},
            HighScoreEntry {"Jason Blochowiak", 100, 0, 0, 0, false},
            HighScoreEntry {"Don Glassford", 100, 0, 0, 0, false},
            HighScoreEntry {"Dan Linton", 100, 0, 0, 0, false},
        }};
        world.active_high_score_index = 0;
        world.high_score_checked = true;
        world.high_score_frames_remaining = 900;
    } else if (options.diagnostic_screen == "high-score-submitted-empty") {
        world.screen = Screen::HighScores;
        world.high_scores = {{
            HighScoreEntry {"Unknown Soldier", 1000, 10, 0, 0, false},
            HighScoreEntry {"Argo Games - 1993", 100, 0, 0, 0, false},
            HighScoreEntry {"", 100, 0, 0, 0, false},
            HighScoreEntry {"Jason Blochowiak", 100, 0, 0, 0, false},
            HighScoreEntry {"Don Glassford", 100, 0, 0, 0, false},
            HighScoreEntry {"Dan Linton", 100, 0, 0, 0, false},
        }};
        world.active_high_score_index = -1;
        world.high_score_checked = true;
        world.high_score_frames_remaining = 420;
    } else if (options.diagnostic_screen == "finale" || options.diagnostic_screen == "finale-particles") {
        world.screen = Screen::Finale;
        world.gameplay_state = GameplayState::FinaleComplete;
        world.transition_freeze = true;
        world.finale_presenter_frame = 0;
        world.finale_presenter_timer_origin = 3951;
        if (!options.finale_hardware_schedule_path.empty()) {
            std::ifstream schedule(options.finale_hardware_schedule_path);
            if (!schedule) throw std::runtime_error("cannot open finale hardware clock schedule");
            std::uint64_t tick = 0;
            while (schedule >> tick) {
                if (tick > 0xffffffffu) throw std::runtime_error("hardware clock tick exceeds uint32");
                world.finale_hardware_tick_schedule.push_back(static_cast<std::uint32_t>(tick));
            }
            if (!schedule.eof() || world.finale_hardware_tick_schedule.empty()) {
                throw std::runtime_error("invalid or empty finale hardware clock schedule");
            }
        }
        world.finale_particle_rng_seed = options.finale_particle_seed.value_or(0xff9652c1u);
        world.game_over_frames_remaining = 1'000'000;
        world.current_level = WorldState::kFinalGameplayLevel;
        world.scores.score = 0;
        world.scores.grounded_invader_resolutions = 0;
        retain_or_seed_diagnostic_player(world);
        Object presenter {};
        presenter.active = true;
        presenter.type = ObjectType::Presenter;
        presenter.sound_id = 0x34f;
        world.objects.push_back(presenter);
    } else if (options.diagnostic_screen == "shareware-ending") {
        world.screen = Screen::SharewareEnding;
        world.screen_fade_frames_remaining = 0;
        world.shareware_ending_tick = 0;
        world.shareware_ending_frame = 0;
        world.shareware_ending_audio_cue = 0;
        world.shareware_ending_waiting_for_input = false;
        world.shareware_ending_exit_after = false;
    } else if (options.diagnostic_screen == "intermission") {
        world.screen = Screen::Intermission;
        world.transition_freeze = true;
    } else if (options.diagnostic_screen == "pizza-all-draws" ||
               options.diagnostic_screen == "helicopter-all-draws" ||
               options.diagnostic_screen == "milestone-level4-audio" ||
               options.diagnostic_screen == "milestone-level8-audio") {
        world.screen = Screen::Intermission;
        world.transition_freeze = true;
        world.intermission_frames_remaining = 1'000'000;
        world.survivor_intermission_active = false;
        world.no_survivor_intermission_active = false;
        world.milestone_intermission =
            options.diagnostic_screen != "milestone-level8-audio" &&
            options.diagnostic_screen != "helicopter-all-draws"
                ? MilestoneIntermission::Level4Pizza
                : MilestoneIntermission::Level8Helicopter;
        world.current_level =
            world.milestone_intermission == MilestoneIntermission::Level4Pizza ? 3 : 7;
        world.pizza_all_draws_diagnostic = options.diagnostic_screen == "pizza-all-draws";
        world.milestone_timer_origin = options.diagnostic_screen == "helicopter-all-draws" ? 267 : world.frame_tick;
        retain_or_seed_diagnostic_player(world);
        Object presenter {};
        presenter.active = true;
        presenter.type = ObjectType::Presenter;
        presenter.sound_id =
            world.milestone_intermission == MilestoneIntermission::Level4Pizza ? 0x343 : 0x349;
        world.objects.push_back(presenter);
    } else if (options.diagnostic_screen == "survivor-intermission" ||
               options.diagnostic_screen == "survivor-intermission-rare" ||
               options.diagnostic_screen == "survivor-native" ||
               options.diagnostic_screen == "survivor-native-rare") {
        world.screen = Screen::Intermission;
        world.transition_freeze = true;
        world.intermission_frames_remaining = 1'000'000;
        world.survivor_intermission_active = true;
        world.no_survivor_intermission_active = false;
        world.survivor_intermission_failure_mask =
            (options.diagnostic_screen == "survivor-intermission-rare" ||
             options.diagnostic_screen == "survivor-native-rare") ? 0xffff : 0;
        world.survivor_native_presenter = true;
        world.survivor_intermission_rng_seed = 0xd130ed6d;
        if (!options.survivor_update_schedule_path.empty()) {
            if (!world.survivor_native_presenter) {
                throw std::runtime_error("survivor update schedule requires a native presenter diagnostic");
            }
            std::ifstream schedule(options.survivor_update_schedule_path);
            if (!schedule) throw std::runtime_error("cannot open survivor update schedule");
            int batch = 0;
            while (schedule >> batch) {
                if (batch < 1 || batch > 6) throw std::runtime_error("invalid survivor update batch");
                world.survivor_presenter_update_batches.push_back(static_cast<std::uint8_t>(batch));
            }
            if (!schedule.eof() || world.survivor_presenter_update_batches.empty()) {
                throw std::runtime_error("invalid or empty survivor update schedule");
            }
        }
        world.survivor_intermission_timer_origin = 2572;
        world.scores.score = 20;
        world.scores.grounded_invader_resolutions = 10;
        world.finale_spawn_budget_remaining = 0;
        world.finale_spawn_budget_total = 0;
        retain_or_seed_diagnostic_player(world);
        for (const float x : {152.0f, 194.0f}) {
            Object survivor {};
            survivor.active = true;
            survivor.type = ObjectType::LandedInvader;
            survivor.position = {x, 172.0f};
            survivor.extent = {2.0f, 3.5f};
            survivor.sprite_id = 0x2ca;
            if (world.survivor_native_presenter && x == 152.0f) {
                survivor.position.x = 133.0f;
                survivor.assaulting = true;
                survivor.frame = 8;
                survivor.timer = 4;
                survivor.velocity = {1, 161};
            }
            world.objects.push_back(survivor);
        }
    } else if (options.diagnostic_screen == "no-survivor-intermission") {
        world.screen = Screen::Intermission;
        world.transition_freeze = true;
        world.intermission_frames_remaining = 1'000'000;
        world.survivor_intermission_active = false;
        world.no_survivor_intermission_active = true;
        world.survivor_intermission_rng_seed = 0xd130ed6d;
        world.survivor_intermission_failure_mask = 0;
        world.scores.score = 20;
        world.scores.grounded_invader_resolutions = 10;
        world.finale_spawn_budget_remaining = 0;
        world.finale_spawn_budget_total = 0;
        retain_or_seed_diagnostic_player(world);
        Object presenter {};
        presenter.active = true;
        presenter.type = ObjectType::Presenter;
        presenter.sound_id = 0x357;
        world.objects.push_back(presenter);
    } else if (options.diagnostic_screen == "game-over") {
        world.screen = Screen::GameOver;
    } else if (options.diagnostic_screen == "remake-effects") {
        world.screen = Screen::Gameplay;
        world.gameplay_state = GameplayState::Active;
        world.transition_freeze = true;
        world.level_banner_frames_remaining = 0;
        world.muzzle_flash_frames_remaining = 2;
        world.muzzle_flash_position = {205.0f, 122.0f};
        retain_or_seed_diagnostic_player(world);

        Object death {};
        death.active = true;
        death.type = ObjectType::EnemyDeath;
        death.position = {112.0f, 105.0f};
        death.extent = {8.0f, 8.0f};
        death.direction = 1;
        death.frame = 1;
        death.timer = 0;
        world.objects.push_back(death);
    } else if (options.diagnostic_screen == "remake-debris") {
        world.screen = Screen::Gameplay;
        world.gameplay_state = GameplayState::Active;
        world.transition_freeze = true;
        world.level_banner_frames_remaining = 0;
        retain_or_seed_diagnostic_player(world);

        for (int index = 0; index < 6; ++index) {
            Object fragment {};
            fragment.active = true;
            fragment.type = ObjectType::AircraftDebris;
            fragment.position = {
                48.0f + static_cast<float>(index) * 43.0f,
                165.0f + static_cast<float>(index % 2),
            };
            fragment.velocity = {0.25f, 1.5f};
            fragment.extent = {3.0f, 3.0f};
            fragment.sprite_id = 0x129 + (index % 4) * 8;
            fragment.has_dropped_payload = true;
            world.objects.push_back(fragment);
        }
    } else if (options.diagnostic_screen == "overrun-terminal-presenter" ||
               options.diagnostic_screen == "overrun-terminal-audio") {
        world.screen = Screen::GameOver;
        world.gameplay_state = GameplayState::GameOver;
        world.player_dead = true;
        world.transition_freeze = false;
        world.transition_armed = false;
        world.transition_started_tick = 0;
        world.bunker_assault_entries = 3;
        world.bunker_white_flag = true;
        world.bunker_terminal_explosion_frames_total = 0;
        world.scores.score = 20;
        world.scores.grounded_invader_resolutions = 10;
        world.game_over_frames_remaining = 720;
    }
    world.screen_fade_frames_remaining = 0;
}

void apply_audio_overrides(WorldState& world, const LaunchOptions& options)
{
    if (options.audio_sound_mode_override) {
        world.audio_config_words[3] = *options.audio_sound_mode_override;
    }
    if (options.audio_voice_count_override) {
        world.audio_config_words[4] = *options.audio_voice_count_override;
    }
    if (options.audio_music_override) {
        world.audio_config_words[5] = *options.audio_music_override;
    }
}

void apply_survivor_intermission_diagnostic_frame(
    WorldState& world,
    const LaunchOptions& options,
    std::uint32_t rendered_frames)
{
    if (options.diagnostic_screen != "survivor-intermission" &&
        options.diagnostic_screen != "survivor-intermission-rare" &&
        options.diagnostic_screen != "survivor-native" &&
        options.diagnostic_screen != "survivor-native-rare" &&
        options.diagnostic_screen != "no-survivor-intermission") {
        return;
    }
    world.screen = Screen::Intermission;
    world.transition_freeze = true;
    world.intermission_frames_remaining = 1'000'000;
    world.survivor_intermission_active =
        options.diagnostic_screen != "no-survivor-intermission";
    world.no_survivor_intermission_active =
        options.diagnostic_screen == "no-survivor-intermission";
    world.survivor_intermission_failure_mask =
        (options.diagnostic_screen == "survivor-intermission-rare" ||
         options.diagnostic_screen == "survivor-native-rare") ? 0xffff : 0;
    world.survivor_intermission_frame = rendered_frames;
    world.survivor_intermission_timer_origin = 2583;
    world.scores.score = 20;
    world.scores.grounded_invader_resolutions = 10;
    world.finale_spawn_budget_remaining = 0;
    world.finale_spawn_budget_total = 0;
}

void apply_finale_diagnostic_frame(
    WorldState& world,
    const LaunchOptions& options,
    std::uint32_t rendered_frames)
{
    if (options.diagnostic_screen != "finale" && options.diagnostic_screen != "finale-particles") {
        return;
    }
    const bool particles_only = options.diagnostic_screen == "finale-particles";
    if (!particles_only &&
        rendered_frames >= presenter_timing::finale_presenter_frames(world)) {
        return;
    }
    world.screen = Screen::Finale;
    world.gameplay_state = GameplayState::FinaleComplete;
    world.transition_freeze = true;
    if (particles_only) {
        world.game_over_frames_remaining = 1'000'000;
    }
    world.finale_presenter_frame = rendered_frames + (particles_only
        ? presenter_timing::finale_particle_start(world) : 0);
    if (!world.finale_hardware_tick_schedule.empty() &&
        world.finale_presenter_frame >= world.finale_hardware_tick_schedule.size()) {
        throw std::runtime_error("finale hardware clock schedule exhausted");
    }
}

void apply_overrun_terminal_presenter_diagnostic_frame(
    WorldState& world,
    const LaunchOptions& options,
    std::uint32_t rendered_frames)
{
    if (options.diagnostic_screen != "overrun-terminal-presenter" || rendered_frames != 0) {
        return;
    }
    // Warp once to the first strip; subsequent draws use normal Game::tick.
    world.screen = Screen::GameOver;
    world.gameplay_state = GameplayState::GameOver;
    world.player_dead = true;
    world.transition_freeze = false;
    world.transition_armed = false;
    world.transition_started_tick = 0;
    world.bunker_assault_active = false;
    world.bunker_assault_entries = 3;
    world.bunker_special_effect_frames_remaining = 0;
    world.bunker_explosion_frames_remaining = 0;
    world.bunker_terminal_explosion_frames_total = 0;
    world.bunker_white_flag = true;
    world.scores.score = 20;
    world.scores.grounded_invader_resolutions = 10;
    world.frame_tick = presenter_timing::terminal_audio_timing(world).retained;
    world.game_over_frames_remaining = 720;
}

WorldPosition diagnostic_original_space_position(const Object& object)
{
    switch (object.type) {
    case ObjectType::PlayerCannon:
        return {148.0f, 166.0f};
    default:
        break;
    }
    return object.position;
}

WorldPosition diagnostic_collision_center(const Object& object)
{
    return internals::collision_center_for_object(object);
}

int diagnostic_original_type_id(const Object& object)
{
    if (object.type == ObjectType::GroundedTransition) {
        return static_cast<int>(ObjectType::Paratrooper);
    }
    return static_cast<int>(object.type);
}

void write_object_summary_json(const WorldState& world, const std::filesystem::path& path)
{
    ensure_parent_directory(path);
    std::ofstream out(path, std::ios::trunc);
    out << "{\n";
    out << "  \"frame_tick\": " << world.frame_tick << ",\n";
    out << "  \"current_level\": " << world.current_level << ",\n";
    out << "  \"screen\": \"" << to_string(world.screen) << "\",\n";
    out << "  \"gameplay_state\": \"" << to_string(world.gameplay_state) << "\",\n";
    out << "  \"presentation\": {\n";
    out << "    \"transition_freeze\": " << (world.transition_freeze ? "true" : "false") << ",\n";
    out << "    \"transition_armed\": " << (world.transition_armed ? "true" : "false") << ",\n";
    out << "    \"bunker_overrun_pending\": " << (world.bunker_overrun_pending ? "true" : "false") << ",\n";
    out << "    \"bunker_assault_active\": " << (world.bunker_assault_active ? "true" : "false") << ",\n";
    out << "    \"overrun_interrupt_residue\": "
        << (world.overrun_interrupt_residue ? "true" : "false") << ",\n";
    out << "    \"survivor_intermission_active\": "
        << (world.survivor_intermission_active ? "true" : "false") << ",\n";
    out << "    \"no_survivor_intermission_active\": "
        << (world.no_survivor_intermission_active ? "true" : "false") << ",\n";
    out << "    \"survivor_intermission_failure_mask\": "
        << world.survivor_intermission_failure_mask << ",\n";
    out << "    \"milestone_intermission\": "
        << static_cast<int>(world.milestone_intermission) << ",\n";
    out << "    \"milestone_intermission_frame\": "
        << world.milestone_intermission_frame << ",\n";
    out << "    \"bunker_assault_entries\": " << world.bunker_assault_entries << ",\n";
    out << "    \"bunker_special_effect_frames_remaining\": "
        << world.bunker_special_effect_frames_remaining << ",\n";
    out << "    \"bunker_door_opening_frames_remaining\": "
        << world.bunker_door_opening_frames_remaining << ",\n";
    out << "    \"bunker_native_choreography\": " << (world.bunker_native_choreography ? "true" : "false") << ",\n";
    out << "    \"bunker_explosion_frames_remaining\": "
        << world.bunker_explosion_frames_remaining << ",\n";
    out << "    \"bunker_terminal_explosion_frames_total\": "
        << world.bunker_terminal_explosion_frames_total << ",\n";
    out << "    \"bunker_white_flag\": " << (world.bunker_white_flag ? "true" : "false") << ",\n";
    out << "    \"intermission_frames_remaining\": " << world.intermission_frames_remaining << ",\n";
    out << "    \"game_over_frames_remaining\": " << world.game_over_frames_remaining << "\n";
    out << "  },\n";
    out << "  \"sound_events\": [";
    for (std::size_t index = 0; index < world.sound_events.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        out << world.sound_events[index];
    }
    out << "],\n";
    out << "  \"objects\": [\n";
    bool first = true;
    for (std::size_t index = 0; index < world.objects.size(); ++index) {
        const auto& object = world.objects[index];
        if (!object.active || object.pending_destroy) {
            continue;
        }
        if (!first) {
            out << ",\n";
        }
        first = false;
        out << "    {";
        out << "\"index\": " << index;
        out << ", \"type_id\": " << diagnostic_original_type_id(object);
        out << ", \"type\": \"" << to_string(object.type) << "\"";
        out << ", \"aircraft_variant\": \"" << to_string(object.aircraft_variant) << "\"";
        out << ", \"x\": " << std::fixed << std::setprecision(3) << object.position.x;
        out << ", \"y\": " << std::fixed << std::setprecision(3) << object.position.y;
        const auto original_space = diagnostic_original_space_position(object);
        const auto collision_center = diagnostic_collision_center(object);
        const auto collision_bounds = internals::collision_bounds_for_object(object);
        out << ", \"original_x\": " << std::fixed << std::setprecision(3) << original_space.x;
        out << ", \"original_y\": " << std::fixed << std::setprecision(3) << original_space.y;
        out << ", \"collision_x\": " << std::fixed << std::setprecision(3) << collision_center.x;
        out << ", \"collision_y\": " << std::fixed << std::setprecision(3) << collision_center.y;
        out << ", \"collision_left\": " << std::fixed << std::setprecision(3) << collision_bounds.left;
        out << ", \"collision_top\": " << std::fixed << std::setprecision(3) << collision_bounds.top;
        out << ", \"collision_right\": " << std::fixed << std::setprecision(3) << collision_bounds.right;
        out << ", \"collision_bottom\": " << std::fixed << std::setprecision(3) << collision_bounds.bottom;
        out << ", \"vx\": " << std::fixed << std::setprecision(3) << object.velocity.x;
        out << ", \"vy\": " << std::fixed << std::setprecision(3) << object.velocity.y;
        out << ", \"extent_x\": " << std::fixed << std::setprecision(3) << object.extent.x;
        out << ", \"extent_y\": " << std::fixed << std::setprecision(3) << object.extent.y;
        out << ", \"frame\": " << object.frame;
        out << ", \"timer\": " << object.timer;
        out << ", \"finale_drop\": " << (object.finale_drop ? "true" : "false");
        out << ", \"finale_hits_remaining\": " << object.finale_hits_remaining;
        out << ", \"sound_id\": " << object.sound_id;
        out << ", \"wave_bank\": " << object.wave_bank;
        out << ", \"direction\": " << object.direction;
        out << "}";
    }
    if (world.bunker_special_effect_frames_remaining != 0) {
        if (!first) {
            out << ",\n";
        }
        const int frame = world.bunker_native_choreography
            ? (world.bunker_special_effect_frames_remaining > 0
                ? (15 - world.bunker_special_effect_frames_remaining) / 5
                : world.bunker_door_opening_frames_remaining > 0
                    ? (15 - world.bunker_door_opening_frames_remaining) / 5 : 3)
            : world.bunker_special_effect_frames_remaining > 0
                              ? std::clamp((12 - world.bunker_special_effect_frames_remaining) / 4, 0, 2)
                              : 3;
        out << "    {";
        out << "\"index\": -1";
        out << ", \"type_id\": 1";
        out << ", \"type\": \"BunkerSpecialActor\"";
        out << ", \"aircraft_variant\": \"VariantD\"";
        out << ", \"x\": 157.000";
        out << ", \"y\": 172.000";
        out << ", \"original_x\": 157.000";
        out << ", \"original_y\": 172.000";
        out << ", \"collision_x\": 157.000";
        out << ", \"collision_y\": 172.000";
        out << ", \"collision_left\": 157.000";
        out << ", \"collision_top\": 172.000";
        out << ", \"collision_right\": 157.000";
        out << ", \"collision_bottom\": 172.000";
        out << ", \"vx\": 0.000";
        out << ", \"vy\": 0.000";
        out << ", \"extent_x\": 0.000";
        out << ", \"extent_y\": 0.000";
        out << ", \"frame\": " << frame;
        out << ", \"timer\": 0";
        out << ", \"wave_bank\": 0";
        out << ", \"direction\": 1";
        out << "}";
    }
    out << "\n  ],\n";
    out << "  \"wave_banks\": [\n";
    for (std::size_t bank = 0; bank < world.wave_banks.size(); ++bank) {
        const auto& wave = world.wave_banks[bank];
        out << "    {";
        out << "\"bank\": " << bank;
        out << ", \"remaining_spawns\": " << wave.remaining_spawns;
        out << ", \"start_tick\": " << wave.start_tick;
        out << ", \"next_trigger_tick\": " << wave.next_trigger_tick;
        out << ", \"live_next_trigger_tick\": " << wave.live_next_trigger_tick;
        out << ", \"last_paratrooper_tick\": " << wave.last_paratrooper_tick;
        out << "}";
        if (bank + 1 < world.wave_banks.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

int run_faithful_presentation_loop(Game& game, Renderer& renderer, Audio& audio)
{
    using Clock = std::chrono::steady_clock;
    const auto epoch = Clock::now();
    const auto hardware_tick = [&] {
        const auto elapsed = std::chrono::duration<double>(Clock::now() - epoch).count();
        return static_cast<std::uint64_t>(elapsed * kSimulationRateHz);
    };
    const auto at_tick = [&](std::uint64_t tick) {
        return epoch + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(static_cast<double>(tick) / kSimulationRateHz));
    };
    const auto frame_step = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / kSimulationRateHz));
    GameplayClock clock;
    auto last_presented = epoch - frame_step;
    while (renderer.is_running()) {
        renderer.pump_events();
        if (!renderer.is_running()) break;
        const auto started = Clock::now();
        const auto input = renderer.input_state();
        const bool gameplay = uses_gameplay_clock(game.world(), input);
        const auto level = game.world().current_level;
        if (!gameplay) clock.reset();
        clock.begin(hardware_tick());
        advance_gameplay_batch(game, input, clock.update_budget(),
                               [&](const WorldState& world) { audio.play_events(world); });
        if (game.world().quit_requested) break;
        const auto present = [&](const WorldState& world) {
            const auto now = Clock::now();
            renderer.render(world, std::clamp(
                std::chrono::duration<float>(now - last_presented).count(), 0.0f, 0.1f));
            renderer.present();
            last_presented = now;
        };
        if (game.before_owner_page()) {
            present(*game.before_owner_page());
            std::this_thread::sleep_until(Clock::now() + frame_step);
        }
        game.prepare_gameplay_page();
        present(game.world());
        if (gameplay && uses_gameplay_clock(game.world(), input) &&
            game.world().current_level == level && !game.before_owner_page()) {
            // Recheck the tick after sleeping; an early wake must not shorten the wait.
            while (hardware_tick() < clock.deadline()) {
                std::this_thread::sleep_until(at_tick(clock.deadline()));
            }
            clock.finish(hardware_tick());
        } else {
            clock.reset();
            std::this_thread::sleep_until(started + frame_step * (game.before_owner_page() ? 2 : 1));
        }
    }
    audio.stop_all();
#if !defined(NITERAID_REMAKE)
    if (game.world().quit_requested && renderer.is_running()) {
        renderer.show_faithful_exit_card();
    }
#endif
    return 0;
}

#if defined(NITERAID_REMAKE)
int run_remake_presentation_loop(Game& game, Renderer& renderer, Audio& audio)
{
    using Clock = std::chrono::steady_clock;
    const auto simulation_step =
        std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / kSimulationRateHz));
    const auto presentation_step =
        std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / kEnhancedPresentationRateHz));
    constexpr int kMaximumCatchUpSteps = 4;

    WorldState previous_world = game.world();
    auto last_simulation_time = Clock::now();
    auto next_simulation_time = last_simulation_time;
    auto next_presentation_time = last_simulation_time;
    auto last_presentation_time = last_simulation_time - presentation_step;

    while (renderer.is_running()) {
        auto now = Clock::now();
        if (now >= next_simulation_time) {
            renderer.pump_events();
            if (!renderer.is_running()) {
                break;
            }

            int catch_up_steps = 0;
            auto input = renderer.input_state();
            do {
                previous_world = game.world();
                game.tick(input);
                consume_menu_key_events(input);
                game.prepare_gameplay_page();
                audio.play_events(game.world());
                if (game.before_owner_page()) {
                    const float elapsed = std::clamp(
                        std::chrono::duration<float>(Clock::now() - last_presentation_time).count(),
                        0.0f, 0.1f);
                    renderer.render(*game.before_owner_page(), elapsed);
                    renderer.present();
                    last_presentation_time = Clock::now();
                    const auto hold_start = Clock::now();
                    std::this_thread::sleep_until(hold_start + simulation_step);
                    next_simulation_time += Clock::now() - hold_start;
                    previous_world = game.world();
                }
                last_simulation_time = next_simulation_time;
                next_simulation_time += simulation_step;
                ++catch_up_steps;
            } while (now >= next_simulation_time &&
                     catch_up_steps < kMaximumCatchUpSteps);

            if (now >= next_simulation_time) {
                last_simulation_time = now;
                next_simulation_time = now + simulation_step;
            }
            if (game.world().quit_requested) {
                break;
            }
        }

        now = Clock::now();
        if (now >= next_presentation_time) {
            const float presentation_delta_seconds = std::clamp(
                std::chrono::duration<float>(now - last_presentation_time).count(),
                0.0f, 0.1f);
            if (renderer.modern_presentation_enabled()) {
                const auto elapsed = now - last_simulation_time;
                const float alpha =
                    std::chrono::duration<float>(elapsed).count() /
                    std::chrono::duration<float>(simulation_step).count();
                const auto presented =
                    make_presentation_world(previous_world, game.world(), alpha);
                renderer.render(presented, presentation_delta_seconds);
            } else {
                renderer.render(game.world(), presentation_delta_seconds);
            }
            renderer.present();
            last_presentation_time = now;

            next_presentation_time += presentation_step;
            if (now >= next_presentation_time + presentation_step) {
                next_presentation_time = now + presentation_step;
            }
        }

        const auto wake_time = std::min(next_simulation_time, next_presentation_time);
        if (wake_time > Clock::now()) {
            std::this_thread::sleep_until(wake_time);
        }
    }

    return 0;
}
#endif

}  // namespace

App::App(LaunchOptions options)
    : options_(options)
{
}

int App::run()
{
    if (!options_.input_schedule_path.empty() &&
        (options_.start_level != std::optional<std::uint16_t> {0} ||
         !options_.max_frames.has_value() || options_.dump_gameplay_trace_path.empty() ||
         !options_.inject_input.empty())) {
        throw std::runtime_error(
            "--input-schedule requires --level=1, --max-frames, a gameplay trace, and no --inject-input");
    }
    std::optional<InputSchedule> input_schedule;
    if (!options_.input_schedule_path.empty()) {
        input_schedule = InputSchedule::load(options_.input_schedule_path);
    }
    if (options_.pause_replay_after_update &&
        (!input_schedule || *options_.pause_replay_after_update >= input_schedule->count() - 1)) {
        throw std::runtime_error(
            "--pause-replay-after-update requires an input schedule and a later gameplay update");
    }
    const bool player_launch = requires_public_asset_resources(options_);
    set_original_archive_only(options_.ntr_only_assets || player_launch);
    configure_adjacent_original_assets(options_);

    if (player_launch && !original_asset_archive_available()) {
        const auto executable_name = options_.executable_path.empty()
            ? std::string("the executable")
            : std::filesystem::path(options_.executable_path).filename().string();
        throw std::runtime_error(
            "Night Raid startup requires a supported GRAPHICS.NTR or GRAPHICS.NRD archive "
            "beside " + executable_name +
            ", or an explicit --graphics-archive=<path>");
    }

    if (options_.ntr_only_assets && !original_asset_archive_available()) {
        throw std::runtime_error(
            "--ntr-only-assets requires a supported GRAPHICS.NTR or GRAPHICS.NRD archive");
    }
    if (original_asset_edition() == OriginalAssetEdition::Shareware &&
        options_.start_level.value_or(0) >= 4) {
        throw std::runtime_error("the shareware GRAPHICS.NRD supports levels 1 through 4 only");
    }

    std::filesystem::path config_path;
    if (!options_.save_directory.empty()) {
        config_path = std::filesystem::absolute(options_.save_directory) / "CONFIG.NTR";
    } else if (player_launch) {
        const auto directory = options_.executable_path.empty()
            ? std::filesystem::current_path()
            : std::filesystem::absolute(options_.executable_path).parent_path();
        config_path = directory / "CONFIG.NTR";
    }

    Game game(options_.start_level,
              options_.finale_budget_override,
              options_.fast_shots,
              options_.female_finale,
              kEnhancedInputDebounce,
              original_asset_edition() == OriginalAssetEdition::Shareware,
              options_.gameplay_seed_override,
              config_path);
    apply_diagnostic_screen_override(game.diagnostic_world(), options_);
    apply_audio_overrides(game.diagnostic_world(), options_);
    if (options_.diagnostic_screen == "finale") {
        // The loop ticks before rendering frame zero, then leaves Finale on the
        // first frame after the original completed-draw prefix.
        game.diagnostic_world().game_over_frames_remaining =
            static_cast<int>(presenter_timing::finale_presenter_frames(game.world())) + 1;
    }
    game.diagnostic_world().overrun_cleanup_rng_seed = options_.overrun_cleanup_seed;
    if (!options_.dump_audio_path.empty()) {
        Audio::write_diagnostic_wav(
            game.world(), options_.dump_audio_path.c_str(), kSimulationRateHz * 6);
        return 0;
    }
    const bool deterministic_capture = is_deterministic_capture_run(options_);
    Renderer renderer(options_.debug_hitboxes,
                      options_.modern_presentation &&
                          (!deterministic_capture || options_.modern_capture),
                      options_.crt_filter);
    const bool mixed_audio_capture = !options_.dump_mixed_audio_path.empty();
    Audio audio(options_.audio_enabled || mixed_audio_capture, mixed_audio_capture);
    if (options_.diagnostic_screen == "startup-card") {
        audio.stop_all();
        renderer.show_faithful_startup_card(
            options_.dump_frame_path.empty()
                ? nullptr
                : options_.dump_frame_path.c_str());
        return 0;
    }
    if (options_.diagnostic_screen == "exit-card") {
        audio.stop_all();
        renderer.show_faithful_exit_card(
            false, options_.dump_frame_path.empty()
                       ? nullptr
                       : options_.dump_frame_path.c_str());
        return 0;
    }
#if !defined(NITERAID_REMAKE)
    if (renderer.is_interactive() && options_.render_enabled &&
        options_.diagnostic_screen.empty() &&
        !options_.max_frames.has_value() && !deterministic_capture) {
        renderer.show_faithful_startup_card();
        if (!renderer.is_running()) {
            return 0;
        }
    }
#endif
    std::uint32_t rendered_frames = 0;
    std::uint32_t scheduled_updates = 0;
    std::uint32_t pause_replay_frame = 0;
    bool pause_replay_started = false;
    bool pause_replay_complete = false;
#if defined(NITERAID_REMAKE)
    if (renderer.is_interactive() &&
        options_.render_enabled &&
        options_.modern_presentation &&
        !options_.max_frames.has_value() &&
        !deterministic_capture) {
        return run_remake_presentation_loop(game, renderer, audio);
    }
#endif
    if (renderer.is_interactive() && options_.render_enabled &&
        !options_.max_frames.has_value() && !deterministic_capture &&
        options_.diagnostic_screen.empty() && options_.inject_input.empty() &&
        options_.survivor_update_schedule_path.empty() &&
        options_.finale_hardware_schedule_path.empty()) {
        return run_faithful_presentation_loop(game, renderer, audio);
    }
    std::ofstream frame_sequence_manifest;
    std::ofstream gameplay_trace;
    if (!options_.dump_gameplay_trace_path.empty()) {
        const auto path = std::filesystem::path(options_.dump_gameplay_trace_path);
        ensure_parent_directory(path);
        gameplay_trace.open(path, std::ios::trunc);
        if (!gameplay_trace) {
            throw std::runtime_error("cannot open gameplay trace: " + path.string());
        }
    }
    std::uint64_t presentation_frames = 0;
    std::uint64_t capture_start_presentation = 0;
    bool first_frame_sequence_row = true;
    if (!options_.dump_frame_sequence_dir.empty()) {
        const auto manifest_path =
            std::filesystem::path(options_.dump_frame_sequence_dir) / "frame_sequence.json";
        ensure_parent_directory(manifest_path);
        frame_sequence_manifest.open(manifest_path, std::ios::trunc);
        frame_sequence_manifest
            << "{\n  \"simulation_fps\": " << kSimulationRateHz << ",\n"
            << "  \"timeline\": \"" << (options_.capture_presentation_timeline ? "presentation" : "simulation") << "\",\n"
            << "  \"presentation_fps\": " << kSimulationRateHz << ",\n"
            << "  \"capture_stride\": " << options_.dump_frame_sequence_stride << ",\n"
            << "  \"fps\": " << std::fixed << std::setprecision(6)
            << (static_cast<double>(kSimulationRateHz) /
                options_.dump_frame_sequence_stride)
            << ",\n  \"frames\": [\n";
    }
    constexpr auto kVideoFrameDuration =
        std::chrono::nanoseconds(1'000'000'000 / kSimulationRateHz);
    auto next_video_frame = std::chrono::steady_clock::now();

    while (renderer.is_running()) {
        if (input_schedule && scheduled_updates == input_schedule->count()) {
            break;
        }
        if (options_.max_frames.has_value() && rendered_frames >= *options_.max_frames) {
            break;
        }
        renderer.pump_events();
        if (!renderer.is_running()) {
            break;
        }
        const auto next_tick = game.world().frame_tick + 1;
        const auto base_input =
            deterministic_capture ? InputState {} : renderer.input_state();
        auto input = with_injected_input(base_input, options_, next_tick);
        std::optional<std::uint32_t> input_update_index;
        std::optional<std::uint32_t> pause_replay_step;
        if (pause_replay_started && game.world().screen == Screen::Gameplay) {
            pause_replay_complete = true;
        }
        if (options_.pause_replay_after_update && !pause_replay_complete &&
            (pause_replay_started ||
             scheduled_updates == *options_.pause_replay_after_update + 1)) {
            pause_replay_started = true;
            pause_replay_step = pause_replay_frame++;
            input = {};
            switch (*pause_replay_step) {
            case 0: input.escape = true; break;
            case 2: input.menu = true; break;
            case 4: input.move_down = true; break;
            case 6: input.accept = true; input.start = true; break;
            default: break;
            }
        }
        if (input_schedule) {
            if (!pause_replay_step && game.world().screen == Screen::Gameplay) {
                input_update_index = scheduled_updates++;
                input_schedule->advance(*input_update_index);
            }
            if (!pause_replay_step) {
                input = input_schedule->apply(input);
            }
        }
        game.tick(input);
        if (gameplay_trace) {
            const auto& world = game.world();
            gameplay_trace << "{\"frame_tick\":" << world.frame_tick
                           << ",\"gameplay_rng_seed\":" << world.gameplay_rng_seed
                           << ",\"current_level\":" << world.current_level
                           << ",\"score\":" << world.scores.score
                           << ",\"enemy_kills\":" << world.scores.enemy_kills;
            if (input_schedule) {
                gameplay_trace << ",\"input_update_index\":";
                if (input_update_index) {
                    gameplay_trace << *input_update_index;
                } else {
                    gameplay_trace << "null";
                }
            }
            if (options_.pause_replay_after_update) {
                gameplay_trace << ",\"pause_replay_step\":";
                if (pause_replay_step) {
                    gameplay_trace << *pause_replay_step;
                } else {
                    gameplay_trace << "null";
                }
                gameplay_trace << ",\"screen\":" << static_cast<int>(world.screen)
                               << ",\"control_panel_row\":" << world.control_panel_row;
            }
            gameplay_trace << ",\"objects\":[";
            bool first = true;
            for (std::size_t index = 0; index < world.objects.size(); ++index) {
                const auto& object = world.objects[index];
                if (!object.active || object.pending_destroy) {
                    continue;
                }
                if (!first) {
                    gameplay_trace << ',';
                }
                first = false;
                const auto position = diagnostic_original_space_position(object);
                gameplay_trace << "{\"slot\":" << index
                               << ",\"type_id\":" << diagnostic_original_type_id(object)
                               << ",\"phase\":\"" << to_string(object.type) << '"'
                               << ",\"x\":" << std::fixed << std::setprecision(6) << position.x
                               << ",\"y\":" << position.y
                               << ",\"frame\":" << object.frame
                               << ",\"timer\":" << object.timer << '}';
            }
            gameplay_trace << "],\"wave_banks\":[";
            for (std::size_t bank = 0; bank < world.wave_banks.size(); ++bank) {
                if (bank != 0) {
                    gameplay_trace << ',';
                }
                const auto& wave = world.wave_banks[bank];
                gameplay_trace << "{\"bank\":" << bank
                               << ",\"start_tick\":" << wave.start_tick
                               << ",\"stop_tick\":" << wave.stop_tick
                               << ",\"remaining_spawns\":" << wave.remaining_spawns
                               << ",\"min_delay\":" << wave.min_delay
                               << ",\"max_delay\":" << wave.max_delay
                               << ",\"paratrooper_cadence\":" << wave.paratrooper_cadence
                               << ",\"scheduler_flag\":" << (wave.import_flag ? 1 : 0)
                               << ",\"next_trigger_tick\":" << wave.next_trigger_tick
                               << ",\"live_next_trigger_tick\":" << wave.live_next_trigger_tick
                               << ",\"last_paratrooper_tick\":" << wave.last_paratrooper_tick << '}';
            }
            gameplay_trace << "]}\n";
        }
        apply_survivor_intermission_diagnostic_frame(
            game.diagnostic_world(), options_, rendered_frames);
        apply_finale_diagnostic_frame(
            game.diagnostic_world(), options_, rendered_frames);
        if (options_.diagnostic_screen == "pizza-all-draws" ||
            options_.diagnostic_screen == "helicopter-all-draws") {
            game.diagnostic_world().milestone_intermission_frame = rendered_frames;
        }
        apply_overrun_terminal_presenter_diagnostic_frame(
            game.diagnostic_world(), options_, rendered_frames);
        if (game.world().quit_requested) {
#if !defined(NITERAID_REMAKE)
            audio.stop_all();
            if (options_.render_enabled && renderer.is_interactive()) {
                renderer.show_faithful_exit_card();
            }
#endif
            break;
        }

        const bool should_dump_state =
            options_.dump_state_at_tick.has_value() &&
            game.world().frame_tick == *options_.dump_state_at_tick;
        const bool should_dump_render_frame =
            options_.dump_frame_at_render.has_value() &&
            rendered_frames == *options_.dump_frame_at_render;
        if (should_dump_state && !options_.dump_state_path.empty()) {
            const auto bytes = serialize_world_state_dat2730(game.world());
            const auto path = std::filesystem::path(options_.dump_state_path);
            ensure_parent_directory(path);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
        if ((should_dump_state || should_dump_render_frame) &&
            !options_.dump_object_summary_path.empty()) {
            write_object_summary_json(game.world(), options_.dump_object_summary_path);
        }

        const bool extra_capture_hold = options_.capture_presentation_timeline && game.before_owner_page().has_value();
        if (rendered_frames == options_.dump_frame_sequence_start) {
            capture_start_presentation = presentation_frames;
        }
        if (mixed_audio_capture) {
            audio.capture_frame(
                game.world(), rendered_frames >= options_.dump_mixed_audio_start_frame);
            if (extra_capture_hold) {
                // Continue the mixer through the extra picture; never replay this tick's events.
                audio.capture_hold(game.world(), rendered_frames >= options_.dump_mixed_audio_start_frame);
            }
        } else {
            audio.play_events(game.world());
        }
        if (options_.render_enabled) {
            game.prepare_gameplay_page();
            std::string before_owner_file;
            if (game.before_owner_page()) {
                renderer.render(*game.before_owner_page());
                if (!options_.dump_frame_sequence_dir.empty() &&
                    rendered_frames >= options_.dump_frame_sequence_start &&
                    (rendered_frames - options_.dump_frame_sequence_start) % options_.dump_frame_sequence_stride == 0) {
                    std::ostringstream name;
                    name << "frame_" << std::setfill('0') << std::setw(6) << rendered_frames << "_before_owner.bmp";
                    before_owner_file = name.str();
                    const auto path = std::filesystem::path(options_.dump_frame_sequence_dir) / before_owner_file;
                    ensure_parent_directory(path);
                    renderer.save_frame_bmp(path.string().c_str());
                }
                renderer.present();
                if (!options_.max_frames.has_value() && renderer.is_interactive()) {
                    const auto hold_start = std::chrono::steady_clock::now();
                    std::this_thread::sleep_until(hold_start + kVideoFrameDuration);
                    next_video_frame += std::chrono::steady_clock::now() - hold_start;
                }
            }
            renderer.render(game.world());
            if ((should_dump_state || should_dump_render_frame) && !options_.dump_frame_path.empty()) {
                renderer.save_frame_bmp(options_.dump_frame_path.c_str());
            }
            if (!options_.dump_frame_sequence_dir.empty() &&
                rendered_frames >= options_.dump_frame_sequence_start &&
                (rendered_frames - options_.dump_frame_sequence_start) %
                        options_.dump_frame_sequence_stride ==
                    0) {
                std::ostringstream name;
                name << "frame_" << std::setfill('0') << std::setw(6) << rendered_frames << ".bmp";
                const auto path = std::filesystem::path(options_.dump_frame_sequence_dir) / name.str();
                ensure_parent_directory(path);
                renderer.save_frame_bmp(path.string().c_str());
                if (!first_frame_sequence_row) {
                    frame_sequence_manifest << ",\n";
                }
                first_frame_sequence_row = false;
                frame_sequence_manifest
                    << "    {\"render_frame\": " << rendered_frames
                    << ", \"elapsed_seconds\": " << std::fixed << std::setprecision(6)
                    << (static_cast<double>(options_.capture_presentation_timeline
                            ? presentation_frames + extra_capture_hold - capture_start_presentation
                            : rendered_frames - options_.dump_frame_sequence_start) /
                        static_cast<double>(kSimulationRateHz))
                    << ", \"presentation_frame\": " << presentation_frames + extra_capture_hold
                    << ", \"frame_tick\": " << game.world().frame_tick
                    << ", \"gameplay_rng_seed\": " << game.world().gameplay_rng_seed
                    << ", \"death_flash_frames_remaining\": " << game.world().death_flash_frames_remaining
                    << ", \"death_palette_restored\": " << (game.world().death_palette_restored ? "true" : "false")
                    << ", \"screen\": \"" << to_string(game.world().screen) << "\""
                    << ", \"screen_fade_frames_remaining\": "
                    << game.world().screen_fade_frames_remaining
                    << ", \"title_frames_remaining\": "
                    << game.world().title_frames_remaining
                    << ", \"credits_frames_remaining\": "
                    << game.world().credits_frames_remaining
                    << ", \"attract_interlude_frames_remaining\": "
                    << game.world().attract_interlude_frames_remaining
                    << ", \"high_score_frames_remaining\": "
                    << game.world().high_score_frames_remaining;
                if (const auto terminal_frame = presenter_timing::terminal_presenter_frame(game.world())) {
                    frame_sequence_manifest << ", \"terminal_frame\": " << *terminal_frame
                        << ", \"terminal_draw\": " << presenter_timing::terminal_presenter_draw(*terminal_frame);
                }
                if (!before_owner_file.empty()) {
                    const auto& before = *game.before_owner_page();
                    frame_sequence_manifest << ", \"before_owner_presentation\": {\"file\": \""
                        << before_owner_file << "\", \"frame_tick\": " << before.frame_tick
                        << ", \"presentation_frame\": " << presentation_frames
                        << ", \"gameplay_rng_seed\": " << before.gameplay_rng_seed << "}";
                }
                if (game.world().player_dead) {
                    frame_sequence_manifest << ", \"fatal_retained_page\": "
                        << (game.world().fatal_retained_page ? "true" : "false")
                        << ", \"fatal_live_actor_types\": [";
                    bool first_actor = true;
                    for (const auto& object : game.world().objects) {
                        if (!object.active || object.pending_destroy) continue;
                        if (!first_actor) frame_sequence_manifest << ",";
                        first_actor = false;
                        frame_sequence_manifest << diagnostic_original_type_id(object);
                    }
                    frame_sequence_manifest << "], \"fatal_live_sound_ids\": [";
                    bool first_sound = true;
                    for (const auto& object : game.world().objects) {
                        if (!object.active || object.pending_destroy || object.sound_id == 0) continue;
                        if (!first_sound) frame_sequence_manifest << ",";
                        first_sound = false;
                        frame_sequence_manifest << object.sound_id;
                    }
                    frame_sequence_manifest << "]";
                }
                if (game.world().survivor_native_presenter || game.world().no_survivor_intermission_active) {
                    const auto frame = game.world().no_survivor_intermission_active
                        ? original_no_survivor_presenter_frame(game.world()) : original_survivor_presenter_frame(game.world());
                    frame_sequence_manifest
                        << (game.world().no_survivor_intermission_active ? ", \"no_survivor_presenter\": " : ", \"survivor_presenter\": ")
                        << "{\"active\": " << (frame.active ? "true" : "false")
                        << ", \"timer_tick\": " << frame.timer_tick
                        << ", \"random_seed\": " << frame.random_seed
                        << ", \"x_fixed\": " << frame.x_fixed
                        << ", \"y_fixed\": " << frame.y_fixed
                        << ", \"vx_fixed\": " << frame.vx_fixed
                        << ", \"vy_fixed\": " << frame.vy_fixed
                        << ", \"sprite\": " << frame.sprite
                        << ", \"counter\": " << frame.counter
                        << ", \"animation_timer\": " << frame.animation_timer
                        << ", \"update_callback\": " << frame.update_callback
                        << ", \"draw_callback\": " << frame.draw_callback << "}";
                }
                if (game.world().screen == Screen::Finale) {
                    const auto frame = original_finale_presenter_frame(game.world());
                    frame_sequence_manifest << ", \"finale_presenter\": {\"hardware_tick\": " << frame.hardware_tick
                        << ", \"actors\": ";
                    write_scripted_presenter_actors(frame_sequence_manifest, frame.actors);
                    frame_sequence_manifest << "}";
                }
                if (game.world().milestone_intermission == MilestoneIntermission::Level4Pizza) {
                    const auto frame = original_pizza_presenter_frame(game.world());
                    frame_sequence_manifest << ", \"pizza_presenter\": {\"simulation_updates\": " << frame.simulation_updates
                        << ", \"draw_index\": " << frame.draw_index
                        << ", \"active\": " << (frame.active ? "true" : "false") << ", \"actors\": ";
                    write_scripted_presenter_actors(frame_sequence_manifest, frame.actors);
                    frame_sequence_manifest << "}";
                }
                if (game.world().milestone_intermission == MilestoneIntermission::Level8Helicopter) {
                    const auto frame = original_helicopter_presenter_frame(game.world());
                    frame_sequence_manifest << ", \"helicopter_presenter\": {\"simulation_updates\": " << frame.simulation_updates
                        << ", \"draw_index\": " << frame.draw_index
                        << ", \"active\": " << (frame.active ? "true" : "false") << ", \"actors\": ";
                    write_scripted_presenter_actors(frame_sequence_manifest, frame.actors);
                    frame_sequence_manifest << "}";
                }
                if (game.world().screen == Screen::Finale &&
                    game.world().finale_presenter_frame >= presenter_timing::finale_particle_start(game.world())) {
                    const auto elapsed = game.world().finale_presenter_frame - presenter_timing::finale_particle_start(game.world());
                    const auto tick = elapsed;
                    const auto particles = original_finale_particle_frame(game.world().finale_particle_rng_seed, tick);
                    frame_sequence_manifest << ", \"finale_particles\": {\"tick\": " << tick
                        << ", \"random_seed\": " << particles.random_seed << ", \"spawned\": " << particles.spawned
                        << ", \"actors\": [";
                    bool first = true;
                    for (std::size_t slot = 0; slot < particles.actors.size(); ++slot) {
                        const auto& actor = particles.actors[slot];
                        if (!actor.active) continue;
                        if (!first) frame_sequence_manifest << ",";
                        first = false;
                        frame_sequence_manifest << "{\"slot\": " << slot << ", \"type_id\": "
                            << (actor.kind == FinaleParticleKind::Star ? 15 : 16)
                            << ", \"x_fixed\": " << actor.x << ", \"y_fixed\": " << actor.y
                            << ", \"vx_fixed\": " << actor.vx << ", \"vy_fixed\": " << actor.vy
                            << ", \"lifetime\": " << actor.lifetime << ", \"fade\": " << actor.fade
                            << ", \"color\": " << static_cast<unsigned>(actor.base_color) << "}";
                    }
                    frame_sequence_manifest << "]}";
                }
                frame_sequence_manifest << "}";
            }
            renderer.present();
        }
        presentation_frames += 1 + extra_capture_hold;
        ++rendered_frames;
        if ((should_dump_state || should_dump_render_frame) &&
            (!options_.dump_state_path.empty() || !options_.dump_frame_path.empty())) {
            break;  // dump-and-exit
        }
        if (options_.render_enabled && !options_.max_frames.has_value()) {
            next_video_frame += kVideoFrameDuration;
            const auto now = std::chrono::steady_clock::now();
            if (now > next_video_frame + kVideoFrameDuration) {
                next_video_frame = now;
            } else {
                std::this_thread::sleep_until(next_video_frame);
            }
        }
    }

    if (input_schedule && scheduled_updates != input_schedule->count()) {
        throw std::runtime_error("native input replay stopped before all captured updates");
    }
    if (options_.pause_replay_after_update && !pause_replay_complete) {
        throw std::runtime_error("native input replay did not complete the pause sequence");
    }

    if (frame_sequence_manifest.is_open()) {
        frame_sequence_manifest << "\n  ]\n}\n";
    }
    if (mixed_audio_capture) {
        ensure_parent_directory(options_.dump_mixed_audio_path);
        audio.write_capture_wav(options_.dump_mixed_audio_path.c_str());
        const auto manifest_path = options_.dump_mixed_audio_path + ".json";
        audio.write_capture_manifest(manifest_path.c_str());
    }
    return 0;
}

}  // namespace niteraid
