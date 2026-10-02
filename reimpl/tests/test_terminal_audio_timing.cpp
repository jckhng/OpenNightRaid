#include "niteraid/game.hpp"
#include "niteraid/presenter_timing.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace {

using niteraid::Game;
using niteraid::GameplayState;
using niteraid::InputState;
using niteraid::Screen;

struct TimedSound {
    std::uint32_t frame = 0;
    std::uint16_t id = 0;
};

struct TerminalAudioConfiguration {
    std::uint16_t mode;
    std::uint16_t voice_config;
};

constexpr std::array<TerminalAudioConfiguration, 8> kTerminalAudioConfigurations {{
    {0, 2},
    {1, 2},
    {2, 0},
    {2, 1},
    {2, 2},
    {3, 0},
    {3, 1},
    {3, 2},
}};

Game terminal_game(bool interrupted, std::uint16_t audio_mode = 0,
                   std::uint16_t voice_config = 2)
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.screen = Screen::GameOver;
    world.gameplay_state = GameplayState::GameOver;
    world.player_dead = true;
    world.bunker_white_flag = true;
    world.bunker_assault_entries = 3;
    world.overrun_interrupt_residue = interrupted;
    world.audio_config_words[3] = audio_mode;
    world.audio_config_words[4] = voice_config;
    world.transition_started_tick = world.frame_tick;
    world.game_over_frames_remaining = 720;
    return game;
}

std::vector<TimedSound> collect_terminal_sounds(Game& game)
{
    std::vector<TimedSound> sounds;
    for (int tick = 0; tick < 1000 && sounds.size() < 5; ++tick) {
        game.tick(InputState {});
        for (const auto id : game.world().sound_events) {
            sounds.push_back({game.world().frame_tick, id});
        }
    }
    return sounds;
}

void tick_neutral(Game& game, int ticks)
{
    for (int index = 0; index < ticks; ++index) {
        game.tick(InputState {});
    }
}

}  // namespace

TEST_CASE("terminal draw observations follow natural game ticks through flag cycles")
{
    for (const auto mode : {0, 1, 3}) {
        auto game = terminal_game(false, static_cast<std::uint16_t>(mode));
        auto& world = game.diagnostic_world();
        const auto retained = niteraid::presenter_timing::terminal_audio_timing(world).retained;
        world.frame_tick = world.transition_started_tick + retained - 1;
        CHECK(!niteraid::presenter_timing::terminal_presenter_frame(world).has_value());
        for (std::uint32_t frame = 0; frame < 300; ++frame) {
            game.tick(InputState {});
            const auto observed = niteraid::presenter_timing::terminal_presenter_frame(world);
            CHECK(observed.has_value());
            CHECK_EQ(observed.value_or(9999), frame);
            const auto expected_draw = frame < 35 ? frame / 7 : 5 + (frame - 35) / 8;
            CHECK_EQ(niteraid::presenter_timing::terminal_presenter_draw(frame), expected_draw);
        }
    }
}

TEST_CASE("terminal draw observations exclude retained and interrupted pages")
{
    auto game = terminal_game(false);
    auto& world = game.diagnostic_world();
    world.bunker_white_flag = false;
    world.bunker_assault_entries = 0;
    world.transition_started_tick = 0xfffffffeu;
    world.frame_tick = 1;
    CHECK_EQ(niteraid::presenter_timing::terminal_presenter_frame(world).value_or(9999), 3u);
    world.overrun_interrupt_residue = true;
    CHECK(!niteraid::presenter_timing::terminal_presenter_frame(world).has_value());
    world.overrun_interrupt_residue = false;
    world.player_dead = false;
    CHECK(!niteraid::presenter_timing::terminal_presenter_frame(world).has_value());
    world.player_dead = true;
    world.screen = Screen::Gameplay;
    CHECK(!niteraid::presenter_timing::terminal_presenter_frame(world).has_value());
}

TEST_CASE("sound-off terminal audio keeps the logical sequence and relative waits")
{
    constexpr std::array<std::uint16_t, 5> expected_ids {{
        0x365, 0x367, 0x367, 0x367, 0x369,
    }};

    for (const bool interrupted : {false, true}) {
        auto game = terminal_game(interrupted);
        const auto sounds = collect_terminal_sounds(game);

        CHECK_EQ(sounds.size(), expected_ids.size());
        if (sounds.size() != expected_ids.size()) {
            continue;
        }
        for (std::size_t index = 0; index < expected_ids.size(); ++index) {
            CHECK_EQ(sounds[index].id, expected_ids[index]);
        }

        const auto first_prelude_tick = sounds.front().frame;
        const auto first_pulse_tick = sounds[1].frame;
        CHECK_EQ(first_pulse_tick - first_prelude_tick, 35u);
        CHECK_EQ(sounds[2].frame, first_pulse_tick);
        CHECK_EQ(sounds[3].frame, first_pulse_tick);
        CHECK_EQ(sounds[4].frame, first_pulse_tick);
    }
}

TEST_CASE("terminal input respects the mode-specific retained offset and flag poll")
{
    for (const auto config : kTerminalAudioConfigurations) {
        for (const bool interrupted : {false, true}) {
            auto game = terminal_game(interrupted, config.mode, config.voice_config);
            const auto entry_tick = game.world().frame_tick;
            const auto timing = niteraid::presenter_timing::terminal_audio_timing(game.world());
            const auto last_explosion_frame =
                timing.retained + niteraid::presenter_timing::kTerminalExplosionVideoFrames;

            InputState early_escape {};
            early_escape.escape = true;
            for (std::uint32_t tick = 1; tick < last_explosion_frame; ++tick) {
                game.tick(early_escape);
                CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));
                CHECK(!game.world().game_over_input_latched);
            }
            CHECK_EQ(game.world().frame_tick, entry_tick + last_explosion_frame - 1);
            CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));
            CHECK(!game.world().game_over_input_latched);

            game.tick(InputState {});
            CHECK_EQ(game.world().frame_tick, entry_tick + last_explosion_frame);
            CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));
            CHECK(!game.world().game_over_input_latched);

            const auto flag_key_frame =
                timing.retained + niteraid::presenter_timing::kTerminalFlagWaveStartVideoFrame + 1;
            tick_neutral(
                game,
                static_cast<int>(entry_tick + flag_key_frame - game.world().frame_tick - 1));
            InputState flag_key {};
            flag_key.move_left = true;
            game.tick(flag_key);
            CHECK_EQ(game.world().frame_tick, entry_tick + flag_key_frame);
            CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));
            CHECK(game.world().game_over_input_latched);

            const auto next_poll_frame =
                timing.retained + niteraid::presenter_timing::kTerminalFlagWaveStartVideoFrame +
                niteraid::presenter_timing::kFlagVideoFramesEach;
            tick_neutral(
                game,
                static_cast<int>(entry_tick + next_poll_frame - game.world().frame_tick - 1));
            CHECK_EQ(game.world().frame_tick, entry_tick + next_poll_frame - 1);
            CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));
            CHECK(game.world().game_over_input_latched);

            game.tick(InputState {});
            // Exiting starts a new screen and resets its clock; the preceding
            // check pins the terminal poll boundary without using that clock.
            CHECK(static_cast<int>(game.world().screen) != static_cast<int>(Screen::GameOver));
        }
    }
}

TEST_CASE("digital terminal audio preserves logical cue order across voice configurations")
{
    constexpr std::array<std::uint16_t, 5> expected_ids {{
        0x365, 0x367, 0x367, 0x367, 0x369,
    }};

    for (const auto config : kTerminalAudioConfigurations) {
        if (config.mode != 2 && config.mode != 3) {
            continue;
        }
        for (const bool interrupted : {false, true}) {
            auto game = terminal_game(interrupted, config.mode, config.voice_config);
            const auto sounds = collect_terminal_sounds(game);

            CHECK_EQ(sounds.size(), expected_ids.size());
            if (sounds.size() != expected_ids.size()) {
                continue;
            }
            for (std::size_t index = 0; index < expected_ids.size(); ++index) {
                CHECK_EQ(sounds[index].id, expected_ids[index]);
            }
        }
    }
}

TEST_CASE("terminal input distinguishes held keys from released taps across digital voices")
{
    for (const auto config : kTerminalAudioConfigurations) {
        for (const bool interrupted : {false, true}) {
            const auto timing = [&]() {
                auto game = terminal_game(interrupted, config.mode, config.voice_config);
                return niteraid::presenter_timing::terminal_audio_timing(game.world());
            }();
            const auto explosion_frame =
                timing.retained + niteraid::presenter_timing::kTerminalExplosionVideoFrames;
            const auto poll_frame =
                timing.retained + niteraid::presenter_timing::kTerminalFlagWaveStartVideoFrame;

            auto held_game = terminal_game(interrupted, config.mode, config.voice_config);
            const auto held_entry_tick = held_game.world().frame_tick;
            InputState held_escape {};
            held_escape.escape = true;
            for (std::uint32_t tick = 0; tick < poll_frame; ++tick) {
                held_game.tick(held_escape);
            }
            CHECK_EQ(held_game.world().frame_tick, held_entry_tick + poll_frame);
            CHECK_EQ(static_cast<int>(held_game.world().screen), static_cast<int>(Screen::GameOver));
            CHECK(!held_game.world().game_over_input_latched);

            auto tapped_game = terminal_game(interrupted, config.mode, config.voice_config);
            const auto tapped_entry_tick = tapped_game.world().frame_tick;
            tick_neutral(
                tapped_game,
                static_cast<int>(tapped_entry_tick + explosion_frame - tapped_game.world().frame_tick - 1));
            InputState tap {};
            tap.escape = true;
            tapped_game.tick(tap);
            CHECK(tapped_game.world().game_over_input_latched);
            tapped_game.tick(InputState {});
            CHECK(tapped_game.world().game_over_input_latched);

            tick_neutral(
                tapped_game,
                static_cast<int>(tapped_entry_tick + poll_frame - tapped_game.world().frame_tick - 1));
            CHECK_EQ(tapped_game.world().frame_tick, tapped_entry_tick + poll_frame - 1);
            tapped_game.tick(InputState {});
            CHECK(static_cast<int>(tapped_game.world().screen) != static_cast<int>(Screen::GameOver));
        }
    }
}

TEST_CASE("PC-speaker terminal countdown preserves the shared half-tick phase")
{
    constexpr std::array<std::uint16_t, 5> ids {0x365, 0x367, 0x367, 0x367, 0x369};
    // Original 5008 capture after 1480:0006(1,2), measured from prelude.
    constexpr std::array<std::uint32_t, 5> relative_ticks {0, 116, 139, 163, 186};
    for (const bool interrupted : {false, true}) {
        auto game = terminal_game(interrupted);
        game.diagnostic_world().audio_config_words[3] = 1;
        const auto sounds = collect_terminal_sounds(game);
        CHECK_EQ(sounds.size(), ids.size());
        if (sounds.size() != ids.size()) continue;
        for (std::size_t index = 0; index < ids.size(); ++index) {
            CHECK_EQ(sounds[index].id, ids[index]);
            CHECK_EQ(sounds[index].frame - sounds.front().frame, relative_ticks[index]);
        }
        const auto timing = niteraid::presenter_timing::terminal_audio_timing(game.world());
        CHECK_EQ(timing.retained - timing.explosion, 7u);
    }
}
