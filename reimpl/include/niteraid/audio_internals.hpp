#pragma once

#include <cstddef>
#include <cstdint>

#include "niteraid/game_types.hpp"

namespace niteraid::audio_internals {

constexpr std::uint32_t sound_blaster_sample_rate(std::uint8_t time_constant)
{
    return 1'000'000u / (256u - time_constant);
}

// Sound Source's FIFO has its own nominal 7 kHz clock, not the rounded SB
// time-constant clock. FIFO refill/completion latency is a separate concern.
inline constexpr std::uint32_t kSoundSourceSampleRate = 7000;

constexpr bool music_enabled(std::uint16_t music_config)
{
    return music_config != 0;
}

constexpr bool music_stream_loop_marker(std::uint8_t delay)
{
    return (delay & 0x80u) != 0;
}

constexpr bool bunker_assault_suppresses_music(const WorldState& world)
{
    return world.bunker_assault_active ||
           (world.gameplay_state == GameplayState::ScriptedSequence &&
            world.bunker_assault_entries != 0);
}

constexpr std::size_t configured_voice_limit(std::uint16_t voice_config)
{
    if (voice_config == 0) {
        return 1;
    }
    if (voice_config == 1) {
        return 4;
    }
    return 8;
}

constexpr bool composite_effects_enabled(const WorldState& world)
{
    // ApplyAudioOptions calls SD_SetCompositeCount(0) for modes 0/1 and
    // passes the selected 0/4/8 composite count for digital modes 2/3.
    return (world.audio_config_words[3] == 2 || world.audio_config_words[3] == 3) &&
           world.audio_config_words[4] != 0;
}

constexpr bool screen_allows_sound_effects(Screen screen)
{
    switch (screen) {
    case Screen::Gameplay:
    case Screen::Intermission:
    case Screen::GameOver:
    case Screen::Finale:
    case Screen::SharewareEnding:
        return true;
    case Screen::Title:
    case Screen::Credits:
    case Screen::AttractInterlude:
    case Screen::ControlPanel:
    case Screen::HighScoreEntry:
    case Screen::HighScores:
        return false;
    }
    return false;
}

constexpr std::uint16_t backend_sound_resource(std::uint16_t logical_id,
                                               std::uint16_t sound_mode)
{
    return sound_mode == 2 || sound_mode == 3
               ? static_cast<std::uint16_t>(logical_id + 1)
               : logical_id;
}

constexpr std::uint16_t aircraft_sound_logical_id(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D: return 0x32d;
    case AircraftVariant::A: return 0x32b;
    case AircraftVariant::B:
    case AircraftVariant::C: return 0x335;
    }
    return 0;
}

constexpr bool is_aircraft_sound_logical_id(std::uint16_t logical_id)
{
    return logical_id == 0x32b || logical_id == 0x32d || logical_id == 0x335;
}

constexpr std::size_t software_mixer_payload_size(std::size_t declared_size,
                                                  std::size_t stored_size)
{
    const auto padded_size = ((declared_size + 99) / 100) * 100;
    return padded_size < stored_size ? padded_size : stored_size;
}

constexpr std::size_t digital_payload_size(std::size_t declared_size,
                                                 std::size_t stored_size, bool composite)
{
    // 061d -> 05fb (Sound Source) and 03d4 -> 033f (SB) use declared
    // serial bytes. The shared 0a13/0b29 mixer reads 100-byte blocks.
    return composite ? software_mixer_payload_size(declared_size, stored_size)
                     : (declared_size < stored_size ? declared_size : stored_size);
}

constexpr bool resource_repeats(std::uint16_t repeat_word)
{
    return repeat_word != 0;
}

constexpr std::size_t software_mixer_remaining_length(std::size_t declared_size,
                                                       std::size_t source_position)
{
    const auto completed_bytes = (source_position / 100) * 100;
    return completed_bytes < declared_size ? declared_size - completed_bytes : 0;
}

constexpr bool mixer_voice_is_weaker(std::uint16_t lhs_priority,
                                     std::size_t lhs_remaining,
                                     std::uint16_t rhs_priority,
                                     std::size_t rhs_remaining)
{
    return lhs_priority < rhs_priority ||
           (lhs_priority == rhs_priority && lhs_remaining < rhs_remaining);
}

}  // namespace niteraid::audio_internals
