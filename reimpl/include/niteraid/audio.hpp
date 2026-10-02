#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "niteraid/game_types.hpp"
#include "opl3.h"

namespace niteraid {

class Audio {
public:
    explicit Audio(bool enabled = true, bool deterministic_capture = false);
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    void play_events(const WorldState& world);
    void stop_all();
    void capture_frame(const WorldState& world, bool record);
    void capture_hold(const WorldState& world, bool record);
    void write_capture_wav(const char* path) const;
    void write_capture_manifest(const char* path) const;
    static void write_diagnostic_wav(const WorldState& world,
                                     const char* path,
                                     std::uint32_t frames);

private:
    struct ActiveVoice {
        std::shared_ptr<const std::vector<float>> samples {};
        double source_position = 0.0;
        float sample_rate = 44100.0f;
        std::size_t replacement_length = 0;
        std::uint16_t priority = 0;
        std::uint16_t logical_id = 0;
        std::uint16_t resource_id = 0;
        bool looping = false;
    };
    struct MusicState {
        std::vector<std::uint8_t> data {};
        std::size_t offset = 0;
        int wait_samples = 0;
        std::uint8_t pending_delay = 0;
        opl3_chip opl {};
    };
    struct CaptureFrameTrace {
        std::uint32_t capture_frame = 0;
        std::uint32_t frame_tick = 0;
        Screen screen = Screen::Title;
        GameplayState gameplay_state = GameplayState::Active;
        std::uint16_t sound_mode = 0;
        std::uint16_t voice_config = 0;
        std::uint16_t music_resource = 0;
        bool reset_sound_effects = false;
        bool events_applied = true;
        std::vector<std::uint16_t> active_resource_sounds {};
        std::vector<std::uint32_t> active_source_sample_counts {};
        std::vector<std::uint32_t> active_source_sample_rates {};
        std::vector<std::uint16_t> logical_events {};
        std::vector<std::uint16_t> resource_events {};
        std::vector<std::uint16_t> logical_stop_events {};
        std::vector<std::uint16_t> resource_stop_events {};
        std::vector<std::uint16_t> aircraft_logical_loops {};
        std::vector<std::uint16_t> aircraft_resource_loops {};
        std::vector<std::uint16_t> owner_logical_sounds {};
        std::vector<std::uint16_t> owner_resource_sounds {};
        std::vector<std::uint16_t> owner_logical_loops {};
        std::vector<std::uint16_t> owner_resource_loops {};
    };

    static void audio_callback(void* userdata, std::uint8_t* stream, int len);
    static std::optional<std::uint16_t> music_for_world(const WorldState& world);
    void play_sound(std::uint16_t sound_id, std::uint16_t sound_mode,
                    std::size_t voice_limit);
    void stop_sound(std::uint16_t sound_id, std::uint16_t sound_mode);
    void silence_sound_effects(std::uint16_t sound_mode, bool clear_owner_slots);
    void enforce_voice_limit(std::size_t voice_limit);
    void update_owned_sounds(const WorldState& world, std::uint16_t sound_mode,
                             std::size_t voice_limit);
    void update_music(const WorldState& world);
    void start_music(std::uint16_t music_id);
    void stop_music();
    float render_music_sample();
    void prepare_digital_block();
    float render_digital_sample();
    void mix_samples(float* output, std::size_t sample_count);
    void capture_interval(const WorldState& world, bool record, bool apply_events);
    static std::size_t remaining_source_samples(const ActiveVoice& voice);
    static bool voice_is_weaker(const ActiveVoice& lhs, const ActiveVoice& rhs);
    void lock_device();
    void unlock_device();

    std::vector<ActiveVoice> active_ {};
    std::array<float, 100> digital_queued_ {};
    std::array<float, 100> digital_prepared_ {};
    std::uint32_t digital_phase_ = 0;
    std::uint32_t digital_sample_rate_ = 7000;
    bool digital_composite_ = false;
    bool digital_running_ = false;
    std::vector<float> captured_samples_ {};
    std::vector<CaptureFrameTrace> capture_trace_ {};
    std::vector<std::uint16_t> object_sound_slots_ {};
    std::uint16_t object_sound_mode_ = 0;
    std::optional<MusicState> music_ {};
    std::uint16_t current_music_id_ = 0;
    std::uint32_t device_ = 0;
    bool enabled_ = false;
    bool deterministic_capture_ = false;
};

}  // namespace niteraid
