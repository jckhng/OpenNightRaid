#include "niteraid/app.hpp"
#include "niteraid/game_types.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(NITERAID_ENABLE_SDL2)
#define SDL_MAIN_HANDLED
#include <SDL.h>
#endif

namespace {

std::string ascii_lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

niteraid::LaunchOptions parse_options(int argc, char** argv)
{
    niteraid::LaunchOptions options {};
    if (argc > 0 && argv[0] != nullptr) {
        options.executable_path = std::filesystem::absolute(argv[0]).string();
#if defined(NITERAID_ENABLE_SDL2)
        if (char* base = SDL_GetBasePath()) {
            options.executable_path = (std::filesystem::path(base) /
                std::filesystem::path(argv[0]).filename()).string();
            SDL_free(base);
        }
#endif
    }

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        const std::string normalized_arg = ascii_lower(arg);
        if (normalized_arg == "/ibcd" || normalized_arg == "--ibcd") {
            options.fast_shots = true;
            continue;
        }
        if (normalized_arg == "/female" || normalized_arg == "--female") {
            options.female_finale = true;
            continue;
        }
        if ((arg == "--level" || arg == "--wave") && index + 1 < argc) {
            const int one_based_level = std::stoi(argv[++index]);
            options.start_level = static_cast<std::uint16_t>(std::clamp(one_based_level, 1, 13) - 1);
            continue;
        }
        constexpr std::string_view prefix = "--level=";
        if (arg.rfind(prefix, 0) == 0) {
            const int one_based_level = std::stoi(arg.substr(prefix.size()));
            options.start_level = static_cast<std::uint16_t>(std::clamp(one_based_level, 1, 13) - 1);
            continue;
        }
        constexpr std::string_view finale_budget_prefix = "--finale-budget=";
        constexpr std::string_view finale_seed_prefix = "--finale-particle-seed=";
        constexpr std::string_view overrun_seed_prefix = "--overrun-cleanup-seed=";
        constexpr std::string_view gameplay_seed_prefix = "--gameplay-seed=";
        if (arg.rfind(finale_seed_prefix, 0) == 0 || arg.rfind(overrun_seed_prefix, 0) == 0 ||
            arg.rfind(gameplay_seed_prefix, 0) == 0) {
            const bool overrun = arg.rfind(overrun_seed_prefix, 0) == 0;
            const bool gameplay = arg.rfind(gameplay_seed_prefix, 0) == 0;
            auto value = std::string_view(arg).substr(
                overrun ? overrun_seed_prefix.size() :
                gameplay ? gameplay_seed_prefix.size() : finale_seed_prefix.size());
            const int base = value.starts_with("0x") ? 16 : 10;
            if (base == 16) value.remove_prefix(2);
            std::uint32_t seed = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seed, base);
            if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size()) {
                throw std::invalid_argument("invalid diagnostic seed");
            }
            if (overrun) options.overrun_cleanup_seed = seed;
            else if (gameplay) options.gameplay_seed_override = seed;
            else options.finale_particle_seed = seed;
            continue;
        }
        if (arg.rfind(finale_budget_prefix, 0) == 0) {
            const int budget = std::stoi(arg.substr(finale_budget_prefix.size()));
            options.finale_budget_override =
                static_cast<std::uint16_t>(std::clamp(budget, 0, 0xffff));
            continue;
        }
        constexpr std::string_view max_frames_prefix = "--max-frames=";
        if (arg.rfind(max_frames_prefix, 0) == 0) {
            const int max_frames = std::stoi(arg.substr(max_frames_prefix.size()));
            options.max_frames =
                static_cast<std::uint32_t>(std::clamp(max_frames, 1, 1'000'000));
            continue;
        }
        constexpr std::string_view dump_frame_at_render_prefix = "--dump-frame-at-render=";
        if (arg.rfind(dump_frame_at_render_prefix, 0) == 0) {
            const int render_frame = std::stoi(arg.substr(dump_frame_at_render_prefix.size()));
            options.dump_frame_at_render =
                static_cast<std::uint32_t>(std::clamp(render_frame, 0, 1'000'000));
            continue;
        }
        constexpr std::string_view inject_input_at_tick_prefix = "--inject-input-at-tick=";
        if (arg.rfind(inject_input_at_tick_prefix, 0) == 0) {
            const int tick = std::stoi(arg.substr(inject_input_at_tick_prefix.size()));
            options.inject_input_at_tick =
                static_cast<std::uint32_t>(std::clamp(tick, 0, 1'000'000));
            continue;
        }
        constexpr std::string_view inject_input_prefix = "--inject-input=";
        if (arg.rfind(inject_input_prefix, 0) == 0) {
            options.inject_input = arg.substr(inject_input_prefix.size());
            continue;
        }
        constexpr std::string_view inject_input_until_tick_prefix =
            "--inject-input-until-tick=";
        if (arg.rfind(inject_input_until_tick_prefix, 0) == 0) {
            const int tick = std::stoi(arg.substr(inject_input_until_tick_prefix.size()));
            options.inject_input_until_tick =
                static_cast<std::uint32_t>(std::clamp(tick, 0, 1'000'000));
            continue;
        }
        constexpr std::string_view inject_input_seed_prefix = "--inject-input-seed=";
        if (arg.rfind(inject_input_seed_prefix, 0) == 0) {
            const int seed = std::stoi(arg.substr(inject_input_seed_prefix.size()));
            options.injected_input_seed =
                static_cast<std::uint32_t>(std::clamp(seed, 0, 1'000'000));
            continue;
        }
        constexpr std::string_view diagnostic_screen_prefix = "--diagnostic-screen=";
        if (arg.rfind(diagnostic_screen_prefix, 0) == 0) {
            options.diagnostic_screen = arg.substr(diagnostic_screen_prefix.size());
            continue;
        }
        constexpr std::string_view original_graphics_prefix = "--graphics-ntr=";
        constexpr std::string_view survivor_schedule_prefix = "--survivor-update-schedule=";
        constexpr std::string_view finale_schedule_prefix = "--finale-hardware-schedule=";
        if (arg.rfind(finale_schedule_prefix, 0) == 0) {
            options.finale_hardware_schedule_path = arg.substr(finale_schedule_prefix.size());
            continue;
        }
        if (arg.rfind(survivor_schedule_prefix, 0) == 0) {
            options.survivor_update_schedule_path = arg.substr(survivor_schedule_prefix.size());
            continue;
        }
        if (arg.rfind(original_graphics_prefix, 0) == 0) {
            options.original_graphics_path = arg.substr(original_graphics_prefix.size());
            continue;
        }
        constexpr std::string_view graphics_archive_prefix = "--graphics-archive=";
        if (arg.rfind(graphics_archive_prefix, 0) == 0) {
            options.original_graphics_path = arg.substr(graphics_archive_prefix.size());
            continue;
        }
        constexpr std::string_view save_directory_prefix = "--save-dir=";
        if (arg.rfind(save_directory_prefix, 0) == 0) {
            options.save_directory = arg.substr(save_directory_prefix.size());
            if (options.save_directory.empty()) {
                throw std::invalid_argument("--save-dir requires a path");
            }
            continue;
        }
        constexpr std::string_view control_panel_row_prefix = "--control-panel-row=";
        if (arg.rfind(control_panel_row_prefix, 0) == 0) {
            const int row = std::stoi(arg.substr(control_panel_row_prefix.size()));
            options.diagnostic_control_panel_row = std::clamp(row, 0, 5);
            continue;
        }
        if (arg == "--mute" || arg == "--no-audio") {
            options.audio_enabled = false;
            continue;
        }
        if (arg == "--debug-hitboxes") {
            options.debug_hitboxes = true;
            continue;
        }
        if (arg == "--faithful-presentation" || arg == "--no-modern-presentation") {
            options.modern_presentation = false;
            continue;
        }
        if (arg == "--capture-presentation-timeline") {
            options.capture_presentation_timeline = true;
            continue;
        }
        if (arg == "--modern-capture") {
            options.modern_capture = true;
            continue;
        }
        if (arg == "--crt" || arg == "--crt-filter") {
            options.crt_filter = true;
            continue;
        }
        if (arg == "--headless" || arg == "--no-render") {
            options.render_enabled = false;
            continue;
        }
        if (arg == "--no-auto-import" || arg == "--no-original-assets") {
            options.auto_load_original_assets = false;
            continue;
        }
        if (arg == "--ntr-only-assets") {
            options.ntr_only_assets = true;
            continue;
        }
        constexpr std::string_view dump_state_at_prefix = "--dump-state-at-tick=";
        if (arg.rfind(dump_state_at_prefix, 0) == 0) {
            const int tick = std::stoi(arg.substr(dump_state_at_prefix.size()));
            options.dump_state_at_tick =
                static_cast<std::uint32_t>(std::clamp(tick, 0, 1'000'000));
            continue;
        }
        constexpr std::string_view dump_state_path_prefix = "--dump-state-path=";
        if (arg.rfind(dump_state_path_prefix, 0) == 0) {
            options.dump_state_path = arg.substr(dump_state_path_prefix.size());
            continue;
        }
        constexpr std::string_view dump_object_summary_path_prefix = "--dump-object-summary-path=";
        if (arg.rfind(dump_object_summary_path_prefix, 0) == 0) {
            options.dump_object_summary_path = arg.substr(dump_object_summary_path_prefix.size());
            continue;
        }
        constexpr std::string_view dump_gameplay_trace_path_prefix = "--dump-gameplay-trace-path=";
        if (arg.rfind(dump_gameplay_trace_path_prefix, 0) == 0) {
            options.dump_gameplay_trace_path = arg.substr(dump_gameplay_trace_path_prefix.size());
            continue;
        }
        constexpr std::string_view input_schedule_prefix = "--input-schedule=";
        if (arg.rfind(input_schedule_prefix, 0) == 0) {
            options.input_schedule_path = arg.substr(input_schedule_prefix.size());
            if (options.input_schedule_path.empty()) {
                throw std::invalid_argument("--input-schedule requires a path");
            }
            continue;
        }
        constexpr std::string_view pause_replay_prefix = "--pause-replay-after-update=";
        if (arg.rfind(pause_replay_prefix, 0) == 0) {
            const int index = std::stoi(arg.substr(pause_replay_prefix.size()));
            if (index < 0 || index > 1'000'000) {
                throw std::invalid_argument("pause replay update index is out of range");
            }
            options.pause_replay_after_update = static_cast<std::uint32_t>(index);
            continue;
        }
        constexpr std::string_view dump_frame_path_prefix = "--dump-frame-path=";
        if (arg.rfind(dump_frame_path_prefix, 0) == 0) {
            options.dump_frame_path = arg.substr(dump_frame_path_prefix.size());
            continue;
        }
        constexpr std::string_view dump_frame_sequence_dir_prefix = "--dump-frame-sequence-dir=";
        if (arg.rfind(dump_frame_sequence_dir_prefix, 0) == 0) {
            options.dump_frame_sequence_dir = arg.substr(dump_frame_sequence_dir_prefix.size());
            continue;
        }
        constexpr std::string_view dump_frame_sequence_start_prefix = "--dump-frame-sequence-start=";
        if (arg.rfind(dump_frame_sequence_start_prefix, 0) == 0) {
            const int frame = std::stoi(arg.substr(dump_frame_sequence_start_prefix.size()));
            options.dump_frame_sequence_start =
                static_cast<std::uint32_t>(std::clamp(frame, 0, 1'000'000));
            continue;
        }
        constexpr std::string_view dump_frame_sequence_stride_prefix =
            "--dump-frame-sequence-stride=";
        if (arg.rfind(dump_frame_sequence_stride_prefix, 0) == 0) {
            const int stride = std::stoi(arg.substr(dump_frame_sequence_stride_prefix.size()));
            options.dump_frame_sequence_stride =
                static_cast<std::uint32_t>(
                    std::clamp(stride, 1,
                               static_cast<int>(niteraid::kSimulationRateHz)));
            continue;
        }
        constexpr std::string_view dump_audio_path_prefix = "--dump-audio-path=";
        if (arg.rfind(dump_audio_path_prefix, 0) == 0) {
            options.dump_audio_path = arg.substr(dump_audio_path_prefix.size());
            continue;
        }
        constexpr std::string_view dump_mixed_audio_path_prefix = "--dump-mixed-audio-path=";
        if (arg.rfind(dump_mixed_audio_path_prefix, 0) == 0) {
            options.dump_mixed_audio_path = arg.substr(dump_mixed_audio_path_prefix.size());
            continue;
        }
        constexpr std::string_view dump_mixed_audio_start_prefix =
            "--dump-mixed-audio-start-frame=";
        if (arg.rfind(dump_mixed_audio_start_prefix, 0) == 0) {
            const int frame = std::stoi(arg.substr(dump_mixed_audio_start_prefix.size()));
            options.dump_mixed_audio_start_frame =
                static_cast<std::uint32_t>(std::clamp(frame, 0, 1'000'000));
            continue;
        }
        constexpr std::string_view audio_sound_mode_prefix = "--audio-sound-mode=";
        if (arg.rfind(audio_sound_mode_prefix, 0) == 0) {
            const int value = std::stoi(arg.substr(audio_sound_mode_prefix.size()));
            options.audio_sound_mode_override =
                static_cast<std::uint16_t>(std::clamp(value, 0, 3));
            continue;
        }
        constexpr std::string_view audio_voice_count_prefix = "--audio-voice-count=";
        if (arg.rfind(audio_voice_count_prefix, 0) == 0) {
            const int value = std::stoi(arg.substr(audio_voice_count_prefix.size()));
            options.audio_voice_count_override =
                static_cast<std::uint16_t>(std::clamp(value, 0, 2));
            continue;
        }
        constexpr std::string_view audio_music_prefix = "--audio-music=";
        if (arg.rfind(audio_music_prefix, 0) == 0) {
            const int value = std::stoi(arg.substr(audio_music_prefix.size()));
            options.audio_music_override =
                static_cast<std::uint16_t>(std::clamp(value, 0, 1));
            continue;
        }
    }

    if (options.capture_presentation_timeline && options.dump_frame_sequence_stride != 1) {
        throw std::runtime_error("presentation-timeline capture requires frame sequence stride 1");
    }
    return options;
}

}  // namespace

int main(int argc, char** argv)
{
    std::cout << "Nuked-OPL3: Copyright (C) 2013-2020 Nuke.YKT; LGPL-2.1-or-later.\n"
                 "License: reimpl/third_party/nuked-opl3/LICENSE in the release package.\n";
    try {
        niteraid::App app(parse_options(argc, argv));
        return app.run();
    } catch (const std::exception& exception) {
        std::cerr << "niteraid: " << exception.what() << '\n';
        return 1;
    }
}
