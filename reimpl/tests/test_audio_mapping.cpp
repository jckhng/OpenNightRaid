#include "test_harness.hpp"
#include "niteraid/audio_internals.hpp"
#include "niteraid/pc_speaker.hpp"

TEST_CASE("composite effect state depends on device and selected voice count")
{
    niteraid::WorldState world {};
    for (std::uint16_t mode = 0; mode < 4; ++mode) {
        for (std::uint16_t count = 0; count < 3; ++count) {
            world.audio_config_words[3] = mode;
            world.audio_config_words[4] = count;
            CHECK(niteraid::audio_internals::composite_effects_enabled(world) == (mode >= 2 && count != 0));
        }
    }
}

using niteraid::audio_internals::backend_sound_resource;
using niteraid::audio_internals::aircraft_sound_logical_id;
using niteraid::audio_internals::bunker_assault_suppresses_music;
using niteraid::audio_internals::configured_voice_limit;
using niteraid::audio_internals::is_aircraft_sound_logical_id;
using niteraid::audio_internals::music_enabled;
using niteraid::audio_internals::music_stream_loop_marker;
using niteraid::audio_internals::mixer_voice_is_weaker;
using niteraid::audio_internals::resource_repeats;
using niteraid::audio_internals::screen_allows_sound_effects;
using niteraid::audio_internals::sound_blaster_sample_rate;
using niteraid::audio_internals::software_mixer_remaining_length;

TEST_CASE("audio configuration values have one shared runtime interpretation")
{
    CHECK(!music_enabled(0));
    CHECK(music_enabled(1));
    CHECK(music_enabled(0xffff));

    CHECK_EQ(configured_voice_limit(0), 1u);
    CHECK_EQ(configured_voice_limit(1), 4u);
    CHECK_EQ(configured_voice_limit(2), 8u);
    CHECK_EQ(configured_voice_limit(0xffff), 8u);

    CHECK(screen_allows_sound_effects(niteraid::Screen::Gameplay));
    CHECK(screen_allows_sound_effects(niteraid::Screen::Intermission));
    CHECK(screen_allows_sound_effects(niteraid::Screen::GameOver));
    CHECK(screen_allows_sound_effects(niteraid::Screen::Finale));
    CHECK(screen_allows_sound_effects(niteraid::Screen::SharewareEnding));
    CHECK(!screen_allows_sound_effects(niteraid::Screen::Title));
    CHECK(!screen_allows_sound_effects(niteraid::Screen::ControlPanel));
    CHECK(!screen_allows_sound_effects(niteraid::Screen::HighScores));
}

TEST_CASE("music delay high bit is the recovered stream loop marker")
{
    CHECK(!music_stream_loop_marker(0x00));
    CHECK(!music_stream_loop_marker(0x7f));
    CHECK(music_stream_loop_marker(0x80));
    CHECK(music_stream_loop_marker(0xff));
}

TEST_CASE("three-overrun presenter owns the audio bed without gameplay music")
{
    niteraid::WorldState world {};
    world.screen = niteraid::Screen::Gameplay;
    world.gameplay_state = niteraid::GameplayState::Active;
    CHECK(!bunker_assault_suppresses_music(world));

    world.bunker_assault_active = true;
    world.gameplay_state = niteraid::GameplayState::ScriptedSequence;
    CHECK(bunker_assault_suppresses_music(world));

    world.bunker_assault_active = false;
    world.bunker_assault_entries = 1;
    CHECK(bunker_assault_suppresses_music(world));
}

TEST_CASE("aircraft variants retain their recovered spawn-path sound IDs")
{
    using niteraid::AircraftVariant;

    CHECK_EQ(aircraft_sound_logical_id(AircraftVariant::D), 0x32d);
    CHECK_EQ(aircraft_sound_logical_id(AircraftVariant::A), 0x32b);
    CHECK_EQ(aircraft_sound_logical_id(AircraftVariant::B), 0x335);
    CHECK_EQ(aircraft_sound_logical_id(AircraftVariant::C), 0x335);

    CHECK(is_aircraft_sound_logical_id(0x32b));
    CHECK(is_aircraft_sound_logical_id(0x32d));
    CHECK(is_aircraft_sound_logical_id(0x335));
    CHECK(!is_aircraft_sound_logical_id(0x337));
}

TEST_CASE("Sound Source and Sound Blaster use paired even SFX resources")
{
    CHECK_EQ(backend_sound_resource(0x335, 2), 0x336);
    CHECK_EQ(backend_sound_resource(0x335, 3), 0x336);
    CHECK_EQ(backend_sound_resource(0x337, 3), 0x338);
    CHECK_EQ(backend_sound_resource(0x339, 3), 0x33a);
    CHECK_EQ(backend_sound_resource(0x34d, 3), 0x34e);
    CHECK_EQ(backend_sound_resource(0x32d, 2), 0x32e);
    CHECK_EQ(backend_sound_resource(0x32d, 3), 0x32e);
    CHECK_EQ(backend_sound_resource(0x32b, 2), 0x32c);
    CHECK_EQ(backend_sound_resource(0x32b, 3), 0x32c);
    CHECK_EQ(backend_sound_resource(0x33f, 3), 0x340);
    CHECK_EQ(backend_sound_resource(0x341, 3), 0x342);
    CHECK_EQ(backend_sound_resource(0x357, 3), 0x358);
    CHECK_EQ(backend_sound_resource(0x365, 3), 0x366);
    CHECK_EQ(backend_sound_resource(0x367, 3), 0x368);
    CHECK_EQ(backend_sound_resource(0x369, 3), 0x36a);
}

TEST_CASE("disabled and PC-speaker modes retain logical SFX resources")
{
    CHECK_EQ(backend_sound_resource(0x335, 0), 0x335);
    CHECK_EQ(backend_sound_resource(0x335, 1), 0x335);

    CHECK_EQ(backend_sound_resource(0x34d, 1), 0x34d);
}

TEST_CASE("Sound Blaster software mixer consumes the padded final 100-byte block")
{
    using niteraid::audio_internals::software_mixer_payload_size;

    CHECK_EQ(software_mixer_payload_size(6733, 6800), 6800u);
    CHECK_EQ(software_mixer_payload_size(4417, 4500), 4500u);
    CHECK_EQ(software_mixer_payload_size(20572, 20600), 20600u);
    CHECK_EQ(software_mixer_payload_size(62, 62), 62u);
    CHECK_EQ(software_mixer_payload_size(0, 0), 0u);
}

TEST_CASE("Sound Blaster time constants use the original integer sample rate")
{
    CHECK_EQ(sound_blaster_sample_rate(0x72), 7042u);
}

TEST_CASE("Sound Source uses declared serial bytes and padded composite blocks")
{
    using niteraid::audio_internals::digital_payload_size;
    CHECK_EQ(niteraid::audio_internals::kSoundSourceSampleRate, 7000u);
    CHECK_EQ(digital_payload_size(20572, 20600, false), 20572u);
    CHECK_EQ(digital_payload_size(20572, 20600, true), 20600u);
    CHECK_EQ(digital_payload_size(6195, 6200, false), 6195u);
    CHECK_EQ(digital_payload_size(6195, 6200, true), 6200u);
    CHECK_EQ(digital_payload_size(101, 100, false), 100u);
    CHECK_EQ(digital_payload_size(101, 100, true), 100u);
    CHECK_EQ(digital_payload_size(0, 0, false), 0u);
}

TEST_CASE("Sound Blaster full-slot replacement follows priority then remaining length")
{
    CHECK(mixer_voice_is_weaker(1, 900, 2, 100));
    CHECK(!mixer_voice_is_weaker(2, 100, 1, 900));
    CHECK(mixer_voice_is_weaker(1, 99, 1, 100));
    CHECK(!mixer_voice_is_weaker(1, 100, 1, 99));
    CHECK(!mixer_voice_is_weaker(1, 100, 1, 100));
}

TEST_CASE("Sound Blaster replacement length advances on 100-byte block boundaries")
{
    CHECK_EQ(software_mixer_remaining_length(6733, 0), 6733u);
    CHECK_EQ(software_mixer_remaining_length(6733, 99), 6733u);
    CHECK_EQ(software_mixer_remaining_length(6733, 100), 6633u);
    CHECK_EQ(software_mixer_remaining_length(6733, 6700), 33u);
    CHECK_EQ(software_mixer_remaining_length(6733, 6799), 33u);
    CHECK_EQ(software_mixer_remaining_length(6733, 6800), 0u);
}

TEST_CASE("audio resource repeat behavior follows header word two")
{
    CHECK(!resource_repeats(0));
    CHECK(resource_repeats(1));
    CHECK(resource_repeats(0xffff));
}

TEST_CASE("PC-speaker repeated service words preserve the running carrier")
{
    using niteraid::audio_internals::PcSpeakerOscillator;
    PcSpeakerOscillator continuous, serviced;
    continuous.program(2100);
    bool differs_from_restarted = false;
    for (int word = 0; word < 4; ++word) {
        serviced.program(2100);
        PcSpeakerOscillator restarted;
        restarted.program(2100);
        for (int sample = 0; sample < 315; ++sample) {
            const auto actual = serviced.sample();
            CHECK(actual == continuous.sample());
            differs_from_restarted |= actual != restarted.sample();
        }
    }
    CHECK(differs_from_restarted);
}

TEST_CASE("PC-speaker divisor changes and silent service words reset the tone")
{
    using niteraid::audio_internals::PcSpeakerOscillator;
    PcSpeakerOscillator oscillator;
    oscillator.program(2100);
    for (int sample = 0; sample < 315; ++sample) oscillator.sample();
    for (const std::uint16_t divisor : {864, 0, 0, 2100}) {
        oscillator.program(divisor);
        PcSpeakerOscillator fresh;
        fresh.program(divisor);
        for (int sample = 0; sample < 315; ++sample) {
            CHECK(oscillator.sample() == fresh.sample());
        }
    }
}
