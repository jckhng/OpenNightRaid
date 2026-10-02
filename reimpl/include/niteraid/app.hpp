#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace niteraid {

struct LaunchOptions {
    std::optional<std::uint16_t> start_level {};
    std::optional<std::uint16_t> finale_budget_override {};
    std::optional<std::uint32_t> max_frames {};
    std::optional<std::uint32_t> pause_replay_after_update {};
    std::optional<std::uint32_t> finale_particle_seed {};
    std::optional<std::uint32_t> overrun_cleanup_seed {};
    std::optional<std::uint32_t> gameplay_seed_override {};
    std::optional<std::uint32_t> dump_frame_at_render {};
    std::optional<int> diagnostic_control_panel_row {};
    std::optional<std::uint32_t> dump_state_at_tick {};
    std::optional<std::uint32_t> inject_input_at_tick {};
    std::optional<std::uint32_t> inject_input_until_tick {};
    std::optional<std::uint16_t> audio_sound_mode_override {};
    std::optional<std::uint16_t> audio_voice_count_override {};
    std::optional<std::uint16_t> audio_music_override {};
    std::string dump_state_path {};
    std::string dump_object_summary_path {};
    std::string dump_gameplay_trace_path {};
    std::string input_schedule_path {};
    std::string dump_frame_path {};
    std::string dump_frame_sequence_dir {};
    std::uint32_t dump_frame_sequence_start = 0;
    std::uint32_t dump_frame_sequence_stride = 1;
    std::string diagnostic_screen {};
    std::string survivor_update_schedule_path {};
    std::string finale_hardware_schedule_path {};
    std::string inject_input {};
    std::string dump_audio_path {};
    std::string dump_mixed_audio_path {};
    std::string executable_path {};
    std::string original_graphics_path {};
    std::string save_directory {};
    std::uint32_t dump_mixed_audio_start_frame = 0;
    std::uint32_t injected_input_seed = 1;
    bool audio_enabled = true;
    bool render_enabled = true;
    bool debug_hitboxes = false;
    bool modern_presentation = true;
    bool modern_capture = false;
    bool capture_presentation_timeline = false;
    bool crt_filter = false;
    bool fast_shots = false;
    bool female_finale = false;
    bool auto_load_original_assets = true;
    bool ntr_only_assets = false;
};

class App {
public:
    explicit App(LaunchOptions options = {});

    int run();

private:
    LaunchOptions options_ {};
};

}  // namespace niteraid
