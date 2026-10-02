#include "niteraid/game.hpp"
#include "niteraid/game_internals.hpp"
#include "niteraid/pizza_presenter.hpp"
#include "niteraid/presenter_timing.hpp"
#include "niteraid/survivor_presenter.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>

using niteraid::Game;
using niteraid::GameplayState;
using niteraid::ConfirmationPromptAction;
using niteraid::InputState;
using niteraid::MilestoneIntermission;
using niteraid::ObjectType;
using niteraid::Screen;
using niteraid::kAttractInterludeScreenFrames;
using niteraid::kCreditsScreenFrames;
using niteraid::kHighScoreAttractScreenFrames;
using niteraid::kTitleScreenFrames;

TEST_CASE("deployed trooper callback moves without advancing its retained timer")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.objects.clear();
    world.object_highwater.reset();
    world.wave_banks = {};
    niteraid::Object trooper {};
    trooper.active = true;
    trooper.type = ObjectType::Paratrooper;
    trooper.assault_stage = 1;
    trooper.position = {80, 80};
    trooper.velocity = {0, 0.25f};
    trooper.extent = {5, 7.5f};
    trooper.timer = 37;
    world.objects.push_back(trooper);
    game.tick(InputState {});
    CHECK_EQ(world.objects[0].position.y, 80.25);
    CHECK_EQ(world.objects[0].timer, 37u);
}

namespace {

void tick_neutral(Game& game, int ticks)
{
    InputState input {};
    for (int index = 0; index < ticks; ++index) {
        game.tick(input);
        if (game.world().survivor_live) {
            game.prepare_gameplay_page();
        }
    }
}

int active_projectile_count(const Game& game)
{
    return static_cast<int>(std::count_if(
        game.world().objects.begin(), game.world().objects.end(), [](const auto& object) {
            return object.active && !object.pending_destroy &&
                   object.type == ObjectType::PlayerProjectile;
        }));
}

}  // namespace

TEST_CASE("wave banner retains the original tick 210 page before clearing")
{
    constexpr std::uint32_t kLastBannerFrame = 210;
    for (const bool enhanced : {false, true}) {
        for (const std::uint16_t level : {0, 1}) {
            Game game(level, std::nullopt, false, false, enhanced);
            CHECK_EQ(game.world().frame_tick, 1u);
            while (game.world().frame_tick < kLastBannerFrame) {
                CHECK(game.world().level_banner_frames_remaining > 0);
                game.tick(InputState {});
            }
            CHECK_EQ(game.world().frame_tick, kLastBannerFrame);
            CHECK_EQ(game.world().level_banner_frames_remaining, 1);
            game.tick(InputState {});
            CHECK_EQ(game.world().frame_tick, kLastBannerFrame + 1);
            CHECK_EQ(game.world().level_banner_frames_remaining, 0);
        }
    }
}

TEST_CASE("enhanced input requires fire release after entering gameplay")
{
    Game enhanced(std::uint16_t {0}, std::nullopt, false, false, true);
    InputState held_fire {};
    held_fire.fire = true;

    for (int tick = 0; tick < 20; ++tick) {
        enhanced.tick(held_fire);
    }
    CHECK_EQ(active_projectile_count(enhanced), 0);

    enhanced.tick(InputState {});
    for (int tick = 0; tick < 9; ++tick) {
        enhanced.tick(held_fire);
    }
    CHECK_EQ(active_projectile_count(enhanced), 1);

    Game faithful(std::uint16_t {0}, std::nullopt);
    for (int tick = 0; tick < 10; ++tick) {
        faithful.tick(held_fire);
    }
    CHECK_EQ(active_projectile_count(faithful), 1);
}

TEST_CASE("enhanced input buffers a short fire press until the original firing gate")
{
    InputState fire {};
    fire.fire = true;

    Game enhanced(std::uint16_t {0}, std::nullopt, false, false, true);
    enhanced.tick(InputState {});
    enhanced.tick(fire);
    tick_neutral(enhanced, 8);
    CHECK_EQ(enhanced.world().frame_tick, 11u);
    CHECK_EQ(active_projectile_count(enhanced), 1);

    Game faithful(std::uint16_t {0}, std::nullopt);
    faithful.tick(InputState {});
    faithful.tick(fire);
    tick_neutral(faithful, 8);
    CHECK_EQ(faithful.world().frame_tick, 11u);
    CHECK_EQ(active_projectile_count(faithful), 0);
}

TEST_CASE("cannon firing gate uses its own timer rather than the gameplay clock")
{
    Game game(std::uint16_t {0}, std::nullopt);
    game.diagnostic_world().frame_tick = 3;
    InputState fire {};
    fire.fire = true;

    for (int update = 0; update < 9; ++update) {
        game.tick(fire);
    }
    CHECK_EQ(active_projectile_count(game), 0);

    game.tick(fire);
    CHECK_EQ(active_projectile_count(game), 1);
}

TEST_CASE("fresh cannon aim starts at zero beneath the minimum sprite frame")
{
    Game game(std::uint16_t {0}, std::nullopt);
    InputState right {};
    right.move_right = true;
    for (int tick = 0; tick < 7; ++tick) {
        game.tick(right);
    }
    const auto player = std::find_if(game.world().objects.begin(), game.world().objects.end(),
                                     [](const auto& object) {
                                         return object.type == ObjectType::PlayerCannon;
                                     });
    CHECK(player != game.world().objects.end());
    if (player != game.world().objects.end()) {
        CHECK_EQ(player->frame, 10);
    }
}

TEST_CASE("player projectile retains its source cannon aim frame")
{
    Game game(std::uint16_t {0}, std::nullopt);
    InputState input {};
    input.move_right = true;
    input.fire = true;
    for (int tick = 0; tick < 10; ++tick) {
        game.tick(input);
    }
    const auto& objects = game.world().objects;
    const auto cannon = std::find_if(objects.begin(), objects.end(), [](const auto& object) {
        return object.type == ObjectType::PlayerCannon;
    });
    const auto shot = std::find_if(objects.begin(), objects.end(), [](const auto& object) {
        return object.active && object.type == ObjectType::PlayerProjectile;
    });
    CHECK(cannon != objects.end());
    CHECK(shot != objects.end());
    if (cannon != objects.end() && shot != objects.end()) {
        CHECK_EQ(shot->frame, cannon->frame);
    }
}

TEST_CASE("title attract loop advances through recovered title-side screens")
{
    Game game {};

    tick_neutral(game, kTitleScreenFrames);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Credits));

    tick_neutral(game, kCreditsScreenFrames);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::AttractInterlude));

    tick_neutral(game, kAttractInterludeScreenFrames);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::HighScores));

    tick_neutral(game, kHighScoreAttractScreenFrames);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Title));
}

TEST_CASE("finale level reaches finale-complete presentation once its drops are defused")
{
    Game game(std::uint16_t {12}, std::uint16_t {1});

    bool reached_finale = false;
    for (int tick = 0; tick < 950; ++tick) {
        // Controlled winning precondition. Idle normal drops are fatal, not a
        // free win; the actual repeated-hit path has separate collision tests.
        for (auto& object : game.diagnostic_world().objects) {
            if (object.finale_drop && object.assault_stage == 1) object.finale_hits_remaining = 0;
        }
        game.tick(InputState {});
        if (game.world().screen == Screen::Finale ||
            game.world().gameplay_state == GameplayState::FinaleComplete) {
            reached_finale = true;
            break;
        }
    }

    CHECK(reached_finale);
    CHECK_EQ(static_cast<int>(game.world().player_dead), 0);
    CHECK_EQ(game.world().finale_presenter_frame, 0u);

    game.tick(InputState {});
    CHECK_EQ(game.world().finale_presenter_frame, 1u);
    CHECK_EQ(
        game.world().game_over_frames_remaining,
        static_cast<int>(niteraid::presenter_timing::kFinalePresenterVideoFrames - 1));
}

TEST_CASE("late normal level can reach the original game-over presentation without stalling")
{
    Game game(std::uint16_t {7}, std::nullopt);

    bool reached_game_over = false;
    for (int tick = 0; tick < 2200; ++tick) {
        game.tick(InputState {});
        if (game.world().screen == Screen::GameOver ||
            game.world().gameplay_state == GameplayState::GameOver) {
            reached_game_over = true;
            break;
        }
    }

    CHECK(reached_game_over);
    CHECK(game.world().player_dead);
}

TEST_CASE("normal level-complete intermission freezes cannon pose")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.gameplay_state = GameplayState::LevelComplete;

    game.tick(InputState {});

    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Intermission));
    CHECK(game.world().transition_freeze);
    CHECK(game.before_owner_page().has_value());
    if (game.before_owner_page()) {
        CHECK_EQ(static_cast<int>(game.before_owner_page()->screen),
                 static_cast<int>(Screen::Gameplay));
        CHECK_EQ(static_cast<int>(game.before_owner_page()->gameplay_state),
                 static_cast<int>(GameplayState::LevelComplete));
    }
}

TEST_CASE("clear-wave handoff resets the cannon timer and centers before intermission")
{
    constexpr std::uint32_t kOriginalClearWait = 210;
    constexpr int kCenterAim = 128;
    for (const int aim : {0, 96, kCenterAim, 160, 255}) {
        Game game(std::uint16_t {0}, std::nullopt);
        auto& world = game.diagnostic_world();
        world.objects.resize(1);
        world.object_highwater = 1;
        world.wave_banks = {};
        world.wave_banks[0].remaining_spawns = 1;
        world.frame_tick = 4593;
        world.objects[0].timer = 4592;
        InputState setup {};
        setup.mouse_moved = true;
        setup.mouse_x = static_cast<float>(aim) * 319.0f / 255.0f;
        game.tick(setup);
        CHECK_EQ(world.objects[0].frame, std::clamp(aim >> 1,
            niteraid::internals::kAimFrameMin, niteraid::internals::kAimFrameMax));
        world.wave_banks[0].remaining_spawns = 0;
        game.tick(InputState {});
        CHECK(world.transition_armed);
        CHECK_EQ(world.frame_tick, 4595u);
        CHECK_EQ(world.objects[0].timer, 0u);

        const auto seed = world.gameplay_rng_seed;
        InputState ignored {};
        ignored.move_left = true;
        ignored.move_right = true;
        ignored.mouse_moved = true;
        ignored.mouse_x = 0;
        ignored.fire = true;
        for (std::uint32_t tick = 1; tick <= kOriginalClearWait; ++tick) {
            game.tick(ignored);
            const auto distance = std::min(static_cast<int>(tick), std::abs(aim - kCenterAim));
            const auto expected = aim + (aim < kCenterAim ? distance : -distance);
            CHECK_EQ(world.objects[0].frame, std::clamp(expected >> 1,
                niteraid::internals::kAimFrameMin, niteraid::internals::kAimFrameMax));
            CHECK_EQ(world.objects[0].timer, tick);
            CHECK(world.screen == Screen::Gameplay);
            CHECK(world.gameplay_state == GameplayState::Active);
            CHECK_EQ(world.objects.size(), std::size_t {1});
            CHECK_EQ(world.gameplay_rng_seed, seed);
        }
        game.tick(ignored);
        CHECK_EQ(world.objects[0].timer, kOriginalClearWait + 1);
        CHECK(world.screen == Screen::Intermission);
        CHECK(world.gameplay_state == GameplayState::Active);
    }
}

TEST_CASE("regular intermission ignores Space but Escape advances without a prompt")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});

    InputState space {};
    space.fire = true;
    game.tick(space);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Intermission));
    CHECK_EQ(game.world().current_level, 0u);

    game.tick(InputState {});
    InputState escape {};
    escape.escape = true;
    game.tick(escape);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
    CHECK_EQ(static_cast<int>(game.world().gameplay_state),
             static_cast<int>(GameplayState::Active));
    CHECK_EQ(game.world().current_level, 1u);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt), 0);
}

TEST_CASE("terminal game over ignores early keys and waits indefinitely at the waving flag")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.screen = Screen::GameOver;
    world.gameplay_state = GameplayState::GameOver;
    world.player_dead = true;
    world.transition_started_tick = world.frame_tick;
    world.game_over_frames_remaining = 720;

    InputState early_escape {};
    early_escape.escape = true;
    game.tick(early_escape);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));
    CHECK(!game.world().game_over_input_latched);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt), 0);

    game.tick(InputState {});
    tick_neutral(game, 799);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));

    InputState key {};
    key.move_left = true;
    game.tick(key);
    CHECK(game.world().game_over_input_latched);
    game.tick(InputState {}); // Tick 803 is the next completed flag wait.
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::HighScores));
}

TEST_CASE("terminal exit polls after flag waits for both fatal branches")
{
    using namespace niteraid::presenter_timing;
    for (const bool overrun : {false, true}) {
        // Every input tick through raising and two full waving cycles, including
        // exact poll boundaries. A released tap must remain latched until poll.
        for (std::uint32_t input_frame = kTerminalExplosionVideoFrames;
             input_frame <= kTerminalFlagWaveStartVideoFrame + 24 * kFlagVideoFramesEach;
             ++input_frame) {
            Game game(std::uint16_t {0}, std::nullopt);
            auto& world = game.diagnostic_world();
            world.screen = Screen::GameOver;
            world.gameplay_state = GameplayState::GameOver;
            world.player_dead = true;
            world.bunker_white_flag = overrun;
            world.bunker_assault_entries = overrun ? 3 : 0;
            world.transition_started_tick = world.frame_tick;
            const auto retained = overrun ? kOverrunRetainedPageVideoFrames : 0u;
            world.frame_tick += retained + input_frame - 1;
            const auto poll_frame = input_frame <= kTerminalFlagWaveStartVideoFrame
                ? kTerminalFlagWaveStartVideoFrame
                : kTerminalFlagWaveStartVideoFrame +
                    ((input_frame - kTerminalFlagWaveStartVideoFrame + kFlagVideoFramesEach - 1) /
                     kFlagVideoFramesEach) * kFlagVideoFramesEach;
            InputState tap {};
            tap.escape = true;
            game.tick(tap);
            for (auto frame = input_frame; frame < poll_frame; ++frame) {
                CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::GameOver));
                CHECK(world.game_over_input_latched);
                game.tick(InputState {});
            }
            CHECK(static_cast<int>(world.screen) != static_cast<int>(Screen::GameOver));
        }
    }
}

TEST_CASE("nonqualifying overrun exit chains through scores and quit prompt")
{
    using namespace niteraid::presenter_timing;
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.screen = Screen::GameOver;
    world.gameplay_state = GameplayState::GameOver;
    world.player_dead = true;
    world.bunker_white_flag = true;
    world.bunker_assault_entries = 3;
    world.scores.score = 20;
    world.frame_tick = kOverrunRetainedPageVideoFrames +
                       kTerminalFlagWaveStartVideoFrame - 1;
    world.transition_started_tick = 0;

    InputState escape {};
    escape.escape = true;
    game.tick(escape);

    CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::HighScores));
    CHECK_EQ(world.active_high_score_index, -1);

    game.tick(InputState {});
    game.tick(escape);
    CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::ControlPanel));
    CHECK_EQ(world.control_panel_row, 5);
    CHECK(!world.control_panel_from_gameplay);
    CHECK(world.control_panel_initial_hint);

    game.tick(InputState {});
    game.tick(escape);
    CHECK_EQ(world.control_panel_pending_action_row, 5);
    tick_neutral(game, 15);
    CHECK_EQ(static_cast<int>(world.confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::QuitToDos));
}

TEST_CASE("level completion with landed survivors runs the recovered UFO presenter")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    for (const float x : {152.0f, 194.0f}) {
        niteraid::Object survivor {};
        survivor.active = true;
        survivor.type = niteraid::ObjectType::LandedInvader;
        survivor.position = {x, 172.0f};
        world.objects.push_back(survivor);
    }
    world.gameplay_state = GameplayState::LevelComplete;

    game.tick(InputState {});

    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Intermission));
    CHECK(game.world().survivor_intermission_active);
    CHECK(game.world().survivor_native_presenter);
    CHECK(!game.world().no_survivor_intermission_active);
    CHECK_EQ(game.world().survivor_intermission_frame, 0u);
    CHECK_EQ(
        game.world().intermission_frames_remaining,
        static_cast<int>(niteraid::original_survivor_duration(
            2, game.world().survivor_intermission_failure_mask)));

    tick_neutral(game, 180);
    CHECK_EQ(game.world().survivor_intermission_frame, 180u);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Intermission));
    const auto frame = niteraid::original_survivor_presenter_frame(game.world());
    CHECK_EQ(frame.retained_troopers[0].x, 152.0f);
    CHECK_EQ(frame.retained_troopers[1].x, 194.0f);
    const auto remaining = game.world().intermission_frames_remaining;
    tick_neutral(game, remaining + 1);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
    CHECK(!game.world().survivor_native_presenter);
}

TEST_CASE("only successful UFO pickups increase the finale budget at pickup time")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    for (const float x : {120.0f, 194.0f}) {
        niteraid::Object survivor {};
        survivor.active = true;
        survivor.type = ObjectType::LandedInvader;
        survivor.position = {x, 172};
        world.objects.push_back(survivor);
    }
    world.object_highwater = world.objects.size();
    world.gameplay_rng_seed = 1;
    while (niteraid::original_survivor_failure_mask(world.gameplay_rng_seed, 2) != 1) {
        ++world.gameplay_rng_seed;
    }
    world.gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});
    CHECK(world.finale_budget_seed == 0);
    tick_neutral(game, 186);
    CHECK(world.finale_budget_seed == 0);
    game.tick(InputState {});
    game.prepare_gameplay_page();
    CHECK(niteraid::original_survivor_presenter_frame(world).draw_callback == 0x378f);
    for (int tick = 0; tick < 471; ++tick) {
        game.tick(InputState {});
        game.prepare_gameplay_page();
    }
    CHECK(world.finale_budget_seed == 1);
    CHECK(niteraid::original_survivor_presenter_frame(world).draw_callback == 0x36fc);
    tick_neutral(game, 1);
    CHECK(world.finale_budget_seed == 1);
}

TEST_CASE("level completion without landed survivors runs the recovered banner flyby")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.gameplay_state = GameplayState::LevelComplete;

    game.tick(InputState {});

    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Intermission));
    CHECK(!game.world().survivor_intermission_active);
    CHECK(game.world().no_survivor_intermission_active);
    CHECK_EQ(game.world().survivor_intermission_frame, 0u);
    CHECK_EQ(
        game.world().intermission_frames_remaining,
        static_cast<int>(niteraid::presenter_timing::kNoSurvivorFlybyVideoFrames));
    CHECK(std::any_of(game.world().objects.begin(), game.world().objects.end(), [](const auto& object) {
        return object.active && object.type == niteraid::ObjectType::Presenter &&
               object.sound_id == 0x357;
    }));
}

TEST_CASE("banner flyby retains live actor slots and cannon updates until its old counter is zero")
{
    for (const std::uint16_t level : {0, 3}) {
        Game game(level, std::nullopt);
        auto& world = game.diagnostic_world();
        world.objects.resize(48);
        for (std::size_t slot = 1; slot < world.objects.size(); ++slot) {
            world.objects[slot] = {};
        }
        world.object_highwater = 46;
        world.wave_banks = {};
        world.frame_tick = 4805;
        world.objects[0].timer = 210;
        world.gameplay_state = GameplayState::LevelComplete;
        world.transition_armed = true;
        world.gameplay_rng_seed = 2197413576u;
        game.tick(InputState {});
        const auto seed = world.gameplay_rng_seed;
        const auto cannon_frame = world.objects[0].frame;
        CHECK(world.screen == Screen::Intermission);
        CHECK(world.gameplay_state == GameplayState::Active);
        CHECK(!world.transition_armed);
        CHECK(!world.waves_exhausted);
        CHECK_EQ(world.objects[1].position.x, 320.0);
        CHECK_EQ(world.objects[1].position.y, 18.0);
        CHECK_EQ(world.objects[1].frame, 544);
        CHECK_EQ(world.objects[1].extent.x, 0.0f);
        CHECK_EQ(world.objects[1].extent.y, 0.0f);
        CHECK_EQ(world.objects[1].sound_id, 0x357);

        InputState ignored {};
        ignored.fire = true;
        ignored.move_left = true;
        ignored.mouse_moved = true;
        ignored.mouse_x = 0;
        for (int tick = 1; tick <= 544; ++tick) {
            game.tick(ignored);
            CHECK(world.screen == Screen::Intermission);
            CHECK(world.gameplay_state == GameplayState::Active);
            CHECK(world.transition_freeze);
            CHECK(!world.transition_armed);
            CHECK(world.waves_exhausted);
            CHECK_EQ(world.frame_tick, 4806u + tick);
            CHECK_EQ(world.objects[0].timer, 211u + tick);
            CHECK_EQ(world.objects[0].frame, cannon_frame);
            CHECK_EQ(world.objects[1].position.x, 320.0 - tick);
            CHECK_EQ(world.objects[1].position.y, 18.0);
            CHECK_EQ(world.objects[1].velocity.x, -1.0f);
            CHECK_EQ(world.objects[1].frame, 544 - tick);
            CHECK_EQ(world.objects[1].timer, 0u);
            CHECK_EQ(world.objects[1].sound_id, 0x357);
            CHECK_EQ(world.gameplay_rng_seed, seed);
            CHECK_EQ(world.object_highwater.value(), std::size_t {45});
            CHECK_EQ(std::count_if(world.objects.begin(), world.objects.end(), [](const auto& actor) {
                return actor.active && !actor.pending_destroy;
            }), 2);
            const auto draw = niteraid::original_no_survivor_presenter_frame(world);
            CHECK_EQ(draw.x_fixed, static_cast<std::int32_t>((320 - tick) * 65536));
            CHECK_EQ(draw.counter, world.objects[1].frame);
            CHECK_EQ(draw.timer_tick, world.frame_tick);
        }
        game.tick(ignored);
        CHECK(game.before_owner_page().has_value());
        if (game.before_owner_page()) {
            const auto& last_page = *game.before_owner_page();
            CHECK(last_page.screen == Screen::Intermission);
            CHECK_EQ(last_page.current_level, level);
            CHECK_EQ(last_page.frame_tick, 5351u);
            CHECK_EQ(last_page.gameplay_rng_seed, seed);
            CHECK_EQ(last_page.objects[0].timer, 756u);
            CHECK(last_page.objects[1].active);
            CHECK_EQ(last_page.objects[1].frame, -1);
            CHECK_EQ(last_page.objects[1].position.x, -225.0);
            CHECK_EQ(last_page.objects[1].sound_id, 0x357);
        }
        if (level == 0) {
            CHECK(world.screen == Screen::Gameplay);
            CHECK_EQ(world.current_level, 1u);
        } else {
            CHECK(world.milestone_intermission == MilestoneIntermission::Level4Pizza);
            CHECK_EQ(world.objects[0].timer, 756u);
            CHECK_EQ(std::count_if(world.objects.begin(), world.objects.end(), [](const auto& actor) {
                return actor.active && actor.type == ObjectType::Presenter && actor.sound_id == 0x357;
            }), 0);
        }
    }
}

TEST_CASE("completed round handoff retains cannon aim with and without flyby interruption")
{
    for (const bool interrupt : {false, true}) {
        Game game(std::uint16_t {0}, std::nullopt);
        auto& world = game.diagnostic_world();
        world.objects.resize(1);
        world.object_highwater = 1;
        world.wave_banks = {};
        world.wave_banks[0].remaining_spawns = 1;
        InputState aim {};
        aim.mouse_moved = true;
        aim.mouse_x = 192.0f * 319.0f / 255.0f;
        game.tick(aim);
        CHECK_EQ(world.objects[0].frame, 96);
        world.gameplay_state = GameplayState::LevelComplete;
        game.tick(InputState {});
        if (interrupt) {
            InputState escape {};
            escape.escape = true;
            game.tick(escape);
        } else {
            tick_neutral(game, 545);
        }
        CHECK(world.screen == Screen::Gameplay);
        CHECK_EQ(world.current_level, 1u);
        game.tick(InputState {});
        CHECK_EQ(world.objects[0].frame, 96);
    }
}

TEST_CASE("level four milestone scopes recovered music phase and pizza audio owners")
{
    Game game(std::uint16_t {3}, std::nullopt);
    game.diagnostic_world().gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});

    tick_neutral(game, niteraid::presenter_timing::kNoSurvivorFlybyVideoFrames);
    CHECK_EQ(static_cast<int>(game.world().milestone_intermission),
             static_cast<int>(niteraid::MilestoneIntermission::Level4Pizza));
    CHECK(std::any_of(game.world().objects.begin(), game.world().objects.end(), [](const auto& object) {
        return object.active && object.type == niteraid::ObjectType::Presenter &&
               object.sound_id == 0x343;
    }));
    CHECK(game.world().intermission_frames_remaining == 1866);
    std::uint32_t elapsed = 0;

    std::vector<std::uint16_t> sounds;
    std::vector<std::uint32_t> door_frames;
    while (game.world().screen == Screen::Intermission) {
        game.tick(InputState {});
        ++elapsed;
        for (const auto sound : game.world().sound_events) {
            if (sound == 0x337 || sound == 0x339) door_frames.push_back(game.world().milestone_intermission_frame);
        }
        sounds.insert(sounds.end(), game.world().sound_events.begin(),
                      game.world().sound_events.end());
    }
    for (const auto required : {0x33b, 0x33d, 0x347, 0x345, 0x337, 0x339}) {
        CHECK(std::find(sounds.begin(), sounds.end(), required) != sounds.end());
    }
    CHECK_EQ(game.world().current_level, 4);
    CHECK(elapsed == 1867);
    CHECK((door_frames == std::vector<std::uint32_t> {870, 934, 1160, 1223}));
}

TEST_CASE("natural pizza presents its first and final poses before exit")
{
    Game game(std::uint16_t {3}, std::nullopt);
    game.diagnostic_world().gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});
    tick_neutral(game, niteraid::presenter_timing::kNoSurvivorFlybyVideoFrames);

    auto& world = game.diagnostic_world();
    CHECK_EQ(static_cast<int>(world.milestone_intermission),
             static_cast<int>(niteraid::MilestoneIntermission::Level4Pizza));
    CHECK_EQ(world.milestone_intermission_frame, 0u);

    game.tick(InputState {});
    CHECK_EQ(world.milestone_intermission_frame, 0u);
    CHECK_EQ(world.intermission_frames_remaining, 1865);
    CHECK(niteraid::original_pizza_presenter_frame(world).active);

    tick_neutral(game, 1865);
    CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::Intermission));
    CHECK_EQ(world.milestone_intermission_frame, 1865u);
    CHECK_EQ(world.intermission_frames_remaining, 0);
    CHECK(niteraid::original_pizza_presenter_frame(world).active);

    game.tick(InputState {});
    CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::Gameplay));
    CHECK_EQ(world.current_level, 4u);
}

TEST_CASE("extended pizza diagnostic advances audio on its first update")
{
    Game game(std::uint16_t {3}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.screen = Screen::Intermission;
    world.transition_freeze = true;
    world.milestone_intermission = MilestoneIntermission::Level4Pizza;
    world.intermission_frames_remaining = 1'000'000;

    game.tick(InputState {});
    CHECK_EQ(world.milestone_intermission_frame, 1u);
    tick_neutral(game, 79);
    CHECK_EQ(world.milestone_intermission_frame, 80u);
    game.tick(InputState {});
    CHECK_EQ(world.milestone_intermission_frame, 81u);
    CHECK(std::find(world.sound_events.begin(), world.sound_events.end(), 0x33b) !=
          world.sound_events.end());
}

TEST_CASE("natural pizza preserves both same-tick doorway draws")
{
    Game game(std::uint16_t {3}, std::nullopt);
    game.diagnostic_world().gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});
    tick_neutral(game, niteraid::presenter_timing::kNoSurvivorFlybyVideoFrames);

    tick_neutral(game, 884);
    CHECK(!game.before_owner_page());
    game.tick(InputState {});
    CHECK_EQ(game.world().milestone_intermission_frame, 884u);
    CHECK(game.before_owner_page());
    if (game.before_owner_page()) {
        CHECK_EQ(niteraid::original_pizza_presenter_frame(*game.before_owner_page()).draw_index, 884u);
    }
    CHECK_EQ(niteraid::original_pizza_presenter_frame(game.world()).draw_index, 885u);

    tick_neutral(game, 337);
    CHECK(!game.before_owner_page());
    game.tick(InputState {});
    CHECK_EQ(game.world().milestone_intermission_frame, 1222u);
    CHECK(game.before_owner_page());
    if (game.before_owner_page()) {
        CHECK_EQ(niteraid::original_pizza_presenter_frame(*game.before_owner_page()).draw_index, 1223u);
    }
    CHECK_EQ(niteraid::original_pizza_presenter_frame(game.world()).draw_index, 1224u);
}

TEST_CASE("level eight milestone owns helicopter phases and departure effect")
{
    Game game(std::uint16_t {7}, std::nullopt);
    game.diagnostic_world().gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});

    tick_neutral(game, niteraid::presenter_timing::kNoSurvivorFlybyVideoFrames);
    CHECK_EQ(static_cast<int>(game.world().milestone_intermission),
             static_cast<int>(niteraid::MilestoneIntermission::Level8Helicopter));
    CHECK(game.world().intermission_frames_remaining == 1675);
    CHECK(game.world().milestone_timer_origin == game.world().frame_tick);
    std::uint32_t elapsed = 0;

    bool saw_flight_owner = false;
    bool saw_parked_owner = false;
    std::vector<std::uint16_t> sounds;
    std::vector<std::uint32_t> door_frames;
    while (game.world().screen == Screen::Intermission) {
        for (const auto& object : game.world().objects) {
            if (!object.active || object.type != niteraid::ObjectType::Presenter) {
                continue;
            }
            saw_flight_owner |= object.sound_id == 0x349;
            saw_parked_owner |= object.sound_id == 0x34b;
        }
        game.tick(InputState {});
        ++elapsed;
        for (const auto sound : game.world().sound_events) {
            if (sound == 0x337 || sound == 0x339) door_frames.push_back(game.world().milestone_intermission_frame);
            if (sound == 0x34d) CHECK(game.world().milestone_intermission_frame == 911);
        }
        sounds.insert(sounds.end(), game.world().sound_events.begin(),
                      game.world().sound_events.end());
    }
    CHECK(saw_flight_owner);
    CHECK(saw_parked_owner);
    for (const auto required : {0x337, 0x339, 0x34d}) {
        CHECK(std::find(sounds.begin(), sounds.end(), required) != sounds.end());
    }
    CHECK_EQ(game.world().current_level, 8);
    CHECK(elapsed == 1675);
    CHECK((door_frames == std::vector<std::uint32_t> {162, 225, 600, 615, 1052, 1067, 1435, 1498}));
}

TEST_CASE("single-effect helicopter does not bind the composite parked loop")
{
    Game game(std::uint16_t {7}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.audio_config_words[4] = 0;
    world.gameplay_state = GameplayState::LevelComplete;
    game.tick(InputState {});
    tick_neutral(game, niteraid::presenter_timing::kNoSurvivorFlybyVideoFrames + 162);
    CHECK(world.milestone_intermission_frame == 162);
    CHECK(std::none_of(world.objects.begin(), world.objects.end(), [](const auto& object) {
        return object.active && object.type == niteraid::ObjectType::Presenter && object.sound_id != 0;
    }));
}

TEST_CASE("live finale uses serial waits when composite effects are disabled")
{
    Game game(std::uint16_t {12}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.audio_config_words[4] = 0;
    world.gameplay_state = GameplayState::FinaleComplete;
    game.tick(InputState {});
    CHECK(world.screen == Screen::Finale);
    CHECK(world.game_over_frames_remaining == 2943);
    tick_neutral(game, 591);
    CHECK(std::find(world.sound_events.begin(), world.sound_events.end(), 0x33b) != world.sound_events.end());
    CHECK(std::find(world.sound_events.begin(), world.sound_events.end(), 0x351) == world.sound_events.end());
    tick_neutral(game, 9);
    CHECK(std::find(world.sound_events.begin(), world.sound_events.end(), 0x351) != world.sound_events.end());
    tick_neutral(game, 141);
    CHECK(std::find(world.sound_events.begin(), world.sound_events.end(), 0x33d) != world.sound_events.end());
    tick_neutral(game, 36);
    CHECK(std::any_of(world.objects.begin(), world.objects.end(), [](const auto& object) {
        return object.active && object.type == niteraid::ObjectType::Presenter && object.sound_id == 0x34f;
    }));
}

TEST_CASE("smart bomb bunker hit flashes before the shared fatal presenter")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    const auto remaining_before = world.wave_banks[0].remaining_spawns;

    niteraid::Object bomb {};
    bomb.active = true;
    bomb.type = niteraid::ObjectType::SmartBomb;
    bomb.position = {160.0f, 180.0f};
    bomb.extent = {10.0f, 7.0f};
    world.objects.push_back(bomb);

    game.tick(InputState {});

    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
    CHECK_EQ(static_cast<int>(game.world().gameplay_state), static_cast<int>(GameplayState::GameOver));
    CHECK(game.world().death_flash_frames_remaining > 0);
    CHECK_EQ(game.world().bunker_special_effect_frames_remaining, 0);
    CHECK_EQ(game.world().bunker_explosion_frames_remaining, 0);
    CHECK(!game.world().bunker_white_flag);
    CHECK_EQ(game.world().wave_banks[0].remaining_spawns, remaining_before);

    tick_neutral(game, 10);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));
    CHECK_EQ(game.world().death_flash_frames_remaining, 0);
}

TEST_CASE("fatal flash freezes actors and rejects modal and level-warp input")
{
    std::array<InputState, 6> inputs {};
    inputs[1].escape = true;
    inputs[2].gamepad_back = true;
    inputs[3].gamepad_start = true;
    inputs[4].control_modifier = true;
    inputs[4].alt_modifier = true;
    inputs[4].level_warp_digit = 8;
    inputs[5].fire = true;
    for (const auto& input : inputs) {
        Game game(std::uint16_t {7}, std::nullopt);
        auto& world = game.diagnostic_world();
        for (int tick = 0; tick < 500 && !world.player_dead; ++tick) {
            game.tick(InputState {});
        }
        CHECK(world.death_flash_frames_remaining > 0);
        CHECK_EQ(world.frame_tick, 464u);
        CHECK(!world.fatal_retained_page);
        const auto frozen = world;
        CHECK(!world.reset_sound_effects);
        const auto check_frozen = [&]() {
            CHECK_EQ(world.current_level, frozen.current_level);
            CHECK_EQ(world.gameplay_rng_seed, frozen.gameplay_rng_seed);
            CHECK_EQ(world.scores.score, frozen.scores.score);
            CHECK_EQ(world.objects.size(), frozen.objects.size());
            CHECK_EQ(world.level_banner_frames_remaining, frozen.level_banner_frames_remaining);
            CHECK_EQ(static_cast<int>(world.confirmation_prompt), 0);
            CHECK_EQ(std::count(world.sound_events.begin(), world.sound_events.end(), 0x369),
                     world.death_flash_frames_remaining == 1 ? 1 : 0);
            CHECK(world.fatal_retained_page.has_value());
            if (!world.fatal_retained_page) return;
            CHECK_EQ(world.fatal_retained_page->gameplay_tick, frozen.frame_tick);
            CHECK_EQ(world.fatal_retained_page->objects.size(), frozen.objects.size());
            for (std::size_t slot = 0; slot < frozen.objects.size(); ++slot) {
                const auto& before = frozen.objects[slot];
                const auto& after = world.fatal_retained_page->objects[slot];
                CHECK_EQ(after.position.x, before.position.x);
                CHECK_EQ(after.position.y, before.position.y);
                CHECK_EQ(after.frame, before.frame);
                CHECK_EQ(after.timer, before.timer);
                CHECK_EQ(after.active, before.active);
                CHECK_EQ(after.pending_destroy, before.pending_destroy);
                const bool survives = before.type == ObjectType::PlayerCannon ||
                                      before.type == ObjectType::LandedInvader;
                CHECK_EQ(world.objects[slot].active, before.active && survives);
                if (before.active && !survives) CHECK_EQ(world.objects[slot].sound_id, 0);
            }
        };
        for (int remaining = frozen.death_flash_frames_remaining; remaining > 1; --remaining) {
            game.tick(input);
            CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::Gameplay));
            CHECK_EQ(world.death_flash_frames_remaining, remaining - 1);
            CHECK_EQ(world.reset_sound_effects, remaining == frozen.death_flash_frames_remaining);
            check_frozen();
        }
        game.tick(input);
        CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::Gameplay));
        CHECK_EQ(world.death_flash_frames_remaining, 0);
        CHECK(world.death_palette_restored);
        CHECK(!world.reset_sound_effects);
        check_frozen();
        game.tick(input);
        CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::GameOver));
        CHECK_EQ(world.death_flash_frames_remaining, 0);
        CHECK(!world.death_palette_restored);
        CHECK(!world.fatal_retained_page);
        CHECK(!world.game_over_input_latched);
        CHECK(std::find(world.sound_events.begin(), world.sound_events.end(), 0x369) == world.sound_events.end());
        CHECK_EQ(world.scores.score, frozen.scores.score);
        CHECK_EQ(world.gameplay_rng_seed, frozen.gameplay_rng_seed);
        CHECK(std::all_of(world.objects.begin(), world.objects.end(), [](const auto& object) {
            return !object.active || object.type == ObjectType::PlayerCannon ||
                   object.type == ObjectType::LandedInvader;
        }));
    }
}

TEST_CASE("retained fatal picture cannot keep aircraft sound owners alive")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.gameplay_state = GameplayState::GameOver;
    world.player_dead = true;
    world.death_flash_frames_remaining = 9;
    world.transition_started_tick = world.frame_tick;
    niteraid::Object aircraft {};
    aircraft.type = ObjectType::Aircraft;
    aircraft.active = true;
    aircraft.sound_id = 0x335;
    aircraft.position = {98.5f, 60.0f};
    const auto slot = world.objects.size();
    world.objects.push_back(aircraft);
    niteraid::Object landed {};
    landed.type = ObjectType::LandedInvader;
    landed.active = true;
    landed.position = {40.0f, 172.0f};
    world.objects.push_back(landed);

    game.tick(InputState {});
    CHECK(!world.objects[slot].active);
    CHECK(world.reset_sound_effects);
    CHECK_EQ(world.objects[slot].sound_id, 0);
    CHECK(world.objects[slot + 1].active);
    CHECK(world.fatal_retained_page.has_value());
    if (!world.fatal_retained_page) return;
    CHECK(world.fatal_retained_page->objects[slot].active);
    CHECK_EQ(world.fatal_retained_page->objects[slot].sound_id, 0x335);
    CHECK_EQ(world.fatal_retained_page->objects[slot].position.x, 98.5f);
    CHECK(std::none_of(world.objects.begin(), world.objects.end(), [](const auto& object) {
        return object.active && object.sound_id == 0x335;
    }));
    tick_neutral(game, 9);
    CHECK_EQ(static_cast<int>(world.screen), static_cast<int>(Screen::GameOver));
    CHECK(!world.fatal_retained_page);
    CHECK(!world.reset_sound_effects);
}

TEST_CASE("shooting a deployed parachute cuts it during the first animation frames")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();

    niteraid::Object paratrooper {};
    paratrooper.active = true;
    paratrooper.type = niteraid::ObjectType::Paratrooper;
    paratrooper.position = {100.0f, 70.0f};
    paratrooper.velocity = {0.0f, 0.0f};
    paratrooper.extent = {5.0f, 7.5f};
    paratrooper.frame = 0;
    paratrooper.timer = 1;
    world.objects.push_back(paratrooper);

    niteraid::Object projectile {};
    projectile.active = true;
    projectile.type = niteraid::ObjectType::PlayerProjectile;
    projectile.position = {103.0f, 80.5f};
    projectile.velocity = {0.0f, 0.0f};
    projectile.extent = {1.0f, 1.0f};
    world.objects.push_back(projectile);

    game.tick(InputState {});

    const auto falling = std::find_if(
        game.world().objects.begin(), game.world().objects.end(), [](const auto& object) {
            return object.active && !object.pending_destroy &&
                   object.type == niteraid::ObjectType::GroundedTransition &&
                   object.parachute_lost;
        });
    CHECK(falling != game.world().objects.end());
    CHECK_EQ(falling->frame, 0);
    CHECK_EQ(falling->timer, 2u);
    CHECK_NEAR(falling->position.x, 103.0f, 1e-6f);
    CHECK_NEAR(falling->position.y, 81.0f, 1e-6f);
    CHECK_NEAR(falling->velocity.y, 0.5f, 1e-6f);

    tick_neutral(game, 220);
    const auto still_falling = std::find_if(
        game.world().objects.begin(), game.world().objects.end(), [](const auto& object) {
            return object.active && !object.pending_destroy &&
                   object.type == niteraid::ObjectType::GroundedTransition &&
                   object.parachute_lost;
        });
    CHECK(still_falling == game.world().objects.end());
    CHECK_EQ(game.world().scores.grounded_invader_resolutions, 1);
    CHECK_EQ(game.world().scores.score, 2u);
}

TEST_CASE("cutting a deployed chute preserves its current horizontal drift")
{
    for (const float drift : {-0.375f, 0.375f}) {
        Game game(std::uint16_t {0}, std::nullopt);
        auto& world = game.diagnostic_world();
        niteraid::Object trooper {};
        trooper.active = true;
        trooper.type = niteraid::ObjectType::Paratrooper;
        trooper.position = {100.0f, 70.0f};
        trooper.velocity = {drift, 0.0f};
        trooper.extent = {5.5f, 8.0f};
        trooper.assault_stage = 1;
        trooper.timer = 1;
        world.objects.push_back(trooper);
        niteraid::Object shot {};
        shot.active = true;
        shot.type = niteraid::ObjectType::PlayerProjectile;
        shot.position = {103.0f, 80.5f};
        world.objects.push_back(shot);

        game.tick(InputState {});

        const auto falling = std::find_if(world.objects.begin(), world.objects.end(),
                                         [](const auto& actor) {
            return actor.active && actor.type == niteraid::ObjectType::GroundedTransition &&
                   actor.parachute_lost;
        });
        CHECK(falling != world.objects.end());
        if (falling == world.objects.end()) {
            continue;
        }
        CHECK_EQ(falling->velocity.x, drift);
        CHECK_EQ(falling->position.x, 103.0f + drift);
    }
}

TEST_CASE("smart bomb replaces its object-owned sound at the falling transition")
{
    Game game(std::uint16_t {7}, std::nullopt);
    tick_neutral(game, 100);

    auto bomb = std::find_if(
        game.diagnostic_world().objects.begin(),
        game.diagnostic_world().objects.end(),
        [](const auto& object) {
            return object.active && !object.pending_destroy &&
                   object.type == niteraid::ObjectType::SmartBomb;
        });
    CHECK(bomb != game.diagnostic_world().objects.end());
    if (bomb == game.diagnostic_world().objects.end()) {
        return;
    }
    CHECK_EQ(bomb->sound_id, 0x32f);

    bomb->timer = 0;
    game.tick(InputState {});

    bomb = std::find_if(
        game.diagnostic_world().objects.begin(),
        game.diagnostic_world().objects.end(),
        [](const auto& object) {
            return object.active && !object.pending_destroy &&
                   object.type == niteraid::ObjectType::SmartBomb;
        });
    CHECK(bomb != game.diagnostic_world().objects.end());
    if (bomb == game.diagnostic_world().objects.end()) {
        return;
    }
    CHECK_EQ(bomb->sound_id, 0x331);
}

TEST_CASE("smart bomb fatal threshold reads integer Y before movement")
{
    Game game(std::uint16_t {7}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    niteraid::Object bomb {};
    bomb.type = niteraid::ObjectType::SmartBomb;
    bomb.position = {222.125f, 159.875f};
    bomb.active = true;
    bomb.velocity = {0.0f, 1.0f};
    bomb.extent = {10.0f, 7.0f};
    bomb.frame = 4;
    world.objects.push_back(bomb);
    game.tick(InputState {});
    CHECK(!world.player_dead);
    CHECK_NEAR(world.objects[0].position.y, 160.875f, 1e-6f);
    game.tick(InputState {});
    CHECK(world.player_dead);
}

TEST_CASE("smart bomb exhaust owns its phase and resets it when turning begins")
{
    Game game(std::uint16_t {7}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    niteraid::Object bomb {};
    bomb.active = true;
    bomb.type = niteraid::ObjectType::SmartBomb;
    bomb.position = {100.0f, 49.0f};
    bomb.velocity = {1.0f, 0.0f};
    bomb.extent = {8.5f, 2.0f};
    bomb.timer = 6;
    world.objects.push_back(bomb);
    for (int update = 1; update <= 6; ++update) {
        game.tick(InputState {});
        CHECK_EQ(world.objects[0].frame, update % 3);
    }
    game.tick(InputState {});
    CHECK_EQ(world.objects[0].frame, 0);
    CHECK_EQ(world.objects[0].timer, 30u);
    CHECK_NEAR(world.objects[0].position.y, 39.0f, 1e-6f);
}

TEST_CASE("deployed paratrooper resolves on the original bunker rectangle")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();

    niteraid::Object paratrooper {};
    paratrooper.active = true;
    paratrooper.type = niteraid::ObjectType::Paratrooper;
    paratrooper.position = {162.625f, 151.0f};
    paratrooper.velocity = {0.015625f, 0.25f};
    paratrooper.assault_stage = 1;
    world.objects.push_back(paratrooper);

    game.tick(InputState {});

    const auto unresolved = std::find_if(
        game.world().objects.begin(), game.world().objects.end(), [](const auto& object) {
            return object.active && !object.pending_destroy &&
                   object.type == niteraid::ObjectType::Paratrooper;
        });
    CHECK(unresolved == game.world().objects.end());
    CHECK_EQ(game.world().scores.grounded_invader_resolutions, 1);
    CHECK_EQ(game.world().scores.score, 2u);
    CHECK_EQ(
        std::count_if(game.world().objects.begin(), game.world().objects.end(), [](const auto& object) {
            return object.active && object.type == niteraid::ObjectType::ResolutionParticle &&
                   std::isfinite(object.position.x) && std::isfinite(object.position.y);
        }),
        8);
}

TEST_CASE("bunker collision particles wait until the next object update")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    const auto cannon = world.objects.front();
    world.objects = {cannon};
    world.object_highwater.reset();
    world.wave_banks = {};
    world.gameplay_rng_seed = 3808909436u;

    niteraid::Object trooper {};
    trooper.active = true;
    trooper.type = niteraid::ObjectType::Paratrooper;
    trooper.position = {162.578125, 151.0};
    trooper.velocity = {0.015625f, 0.25f};
    trooper.assault_stage = 1;
    world.objects.push_back(trooper);

    game.tick(InputState {});

    CHECK_EQ(world.scores.score, 2u);
    CHECK_EQ(world.scores.grounded_invader_resolutions, 1);
    CHECK_EQ(world.gameplay_rng_seed, 3033129820u);
    int count = 0;
    for (const auto& particle : world.objects) {
        if (!particle.active || particle.type != niteraid::ObjectType::ResolutionParticle) {
            continue;
        }
        CHECK_EQ(particle.position.x, 167.59375);
        CHECK_EQ(particle.position.y, 163.25);
        CHECK_EQ(particle.timer, 0u);
        CHECK(particle.assault_stage >= 32);
        ++count;
    }
    CHECK_EQ(count, 8);
    game.tick(InputState {});
    const auto first = std::find_if(world.objects.begin(), world.objects.end(), [](const auto& object) {
        return object.active && object.type == niteraid::ObjectType::ResolutionParticle;
    });
    CHECK(first != world.objects.end());
    if (first != world.objects.end()) {
        CHECK_EQ(first->position.x, 167.364044189453125);
        CHECK_EQ(first->position.y, 162.79364013671875);
        CHECK_EQ(first->assault_stage, 35);
    }
}

TEST_CASE("cannon flash survives two updates and clears after its drawn page")
{
    Game game(std::uint16_t {0}, std::nullopt);
    tick_neutral(game, 9);

    InputState fire {};
    fire.fire = true;
    game.tick(fire);

    CHECK_EQ(game.world().muzzle_flash_frames_remaining, 1);
    CHECK(std::any_of(
        game.world().objects.begin(), game.world().objects.end(), [](const auto& object) {
            return object.active && object.type == niteraid::ObjectType::PlayerProjectile;
        }));

    game.tick(InputState {});
    CHECK_EQ(game.world().muzzle_flash_frames_remaining, 1);
    game.prepare_gameplay_page();
    CHECK_EQ(game.world().muzzle_flash_frames_remaining, 1);
    game.prepare_gameplay_page();
    CHECK_EQ(game.world().muzzle_flash_frames_remaining, 0);
}

TEST_CASE("armed finale drop ground contact flashes before the shared fatal presenter")
{
    Game game(std::uint16_t {niteraid::WorldState::kFinalGameplayLevel}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.erase(
        std::remove_if(world.objects.begin(),
                       world.objects.end(),
                       [](const auto& object) { return object.type == niteraid::ObjectType::FinaleController; }),
        world.objects.end());

    niteraid::Object drop {};
    drop.active = true;
    drop.type = niteraid::ObjectType::Paratrooper;
    drop.position = {160.0f, 175.0f};
    drop.extent = {2.5f, 3.0f};
    drop.finale_drop = true;
    drop.finale_hits_remaining = 3;
    drop.parachute_lost = true;
    drop.assault_stage = 1;
    drop.timer = 5;
    world.objects.push_back(drop);

    game.tick(InputState {});

    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
    CHECK_EQ(static_cast<int>(game.world().gameplay_state), static_cast<int>(GameplayState::GameOver));
    CHECK(game.world().death_flash_frames_remaining > 0);
    CHECK_EQ(game.world().bunker_special_effect_frames_remaining, 0);
    CHECK_EQ(game.world().bunker_explosion_frames_remaining, 0);
    CHECK(!game.world().bunker_white_flag);

    tick_neutral(game, 10);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));
    CHECK_EQ(game.world().death_flash_frames_remaining, 0);
}

TEST_CASE("landing installs the grounded callback for the next update and tests its old timer")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    niteraid::Object trooper {};
    trooper.active = true;
    trooper.type = ObjectType::Paratrooper;
    trooper.position = {100.0f, 164.0f};
    trooper.assault_stage = 1;
    world.objects.push_back(trooper);
    const auto slot = world.objects.size() - 1;
    game.tick(InputState {});
    CHECK(world.objects[slot].type == ObjectType::GroundedTransition);
    CHECK_EQ(world.objects[slot].timer, 0u);
    CHECK_EQ(world.objects[slot].frame, 0);
    game.tick(InputState {});
    CHECK_EQ(world.objects[slot].timer, 1u);
    CHECK_EQ(world.objects[slot].frame, 1);
    tick_neutral(game, 104);
    CHECK(world.objects[slot].type == ObjectType::GroundedTransition);
    CHECK_EQ(world.objects[slot].timer, 105u);
    CHECK_EQ(world.objects[slot].frame, 7);
    game.tick(InputState {});
    CHECK(world.objects[slot].type == ObjectType::LandedInvader);
    CHECK_EQ(world.objects[slot].timer, 0u);
    CHECK_EQ(world.objects[slot].position.x, 103.0f);
    CHECK_EQ(world.objects[slot].position.y, 172.0f);
}

TEST_CASE("overrun presents all landed troopers before cleanup and waits for actual particle completion")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    for (const float x : {30.0f, 120.0f, 114.0f, 194.0f}) {
        niteraid::Object trooper {};
        trooper.active = true;
        trooper.type = ObjectType::LandedInvader;
        trooper.position = {x, 172.0f};
        world.objects.push_back(trooper);
    }
    const auto count = [&](ObjectType type) {
        return std::count_if(world.objects.begin(), world.objects.end(), [=](const auto& object) {
            return object.active && !object.pending_destroy && object.type == type;
        });
    };
    const auto walking = [&] {
        return std::any_of(world.objects.begin(), world.objects.end(), [](const auto& object) {
            return object.active && !object.pending_destroy && object.assaulting;
        });
    };
    game.tick(InputState {});
    CHECK(world.bunker_assault_cleanup_pending);
    CHECK_EQ(count(ObjectType::LandedInvader), 4);
    CHECK_EQ(count(ObjectType::ResolutionParticle), 0);
    CHECK_EQ(world.scores.score, 0);
    CHECK(!walking());
    game.tick(InputState {});
    CHECK(!world.bunker_assault_cleanup_pending);
    CHECK_EQ(count(ObjectType::LandedInvader), 3);
    CHECK_EQ(count(ObjectType::ResolutionParticle), 8);
    CHECK_EQ(world.scores.score, 2);
    CHECK(!walking());
    // A shorter cleanup must start walking earlier than the former fixed wait.
    for (auto& object : world.objects) {
        if (object.type == ObjectType::ResolutionParticle) object.assault_stage = 0;
    }
    game.tick(InputState {});
    CHECK_EQ(count(ObjectType::ResolutionParticle), 0);
    CHECK(!walking());
    game.tick(InputState {});
    CHECK(walking());
    const auto actor = std::find_if(world.objects.begin(), world.objects.end(), [](const auto& object) {
        return object.active && !object.pending_destroy && object.assaulting;
    });
    if (actor != world.objects.end()) {
        CHECK_EQ(actor->position.x, 120.0f);
        CHECK_EQ(actor->timer, 1u);
    }
}

TEST_CASE("overrun retains cannon aim while scripted control advances its timer")
{
    struct AimCase { float mouse_x; int frame; };
    const AimCase cases[] {{0.0f, 6}, {96.0f, 38}, {319.0f, 121}};
    for (const auto& expected : cases) {
        Game game(std::uint16_t {0}, std::nullopt);
        auto& world = game.diagnostic_world();
        for (const float x : {120.0f, 114.0f, 194.0f}) {
            niteraid::Object trooper {};
            trooper.active = true;
            trooper.type = ObjectType::LandedInvader;
            trooper.position = {x, 172.0f};
            world.objects.push_back(trooper);
        }
        InputState aim {};
        aim.mouse_moved = true;
        aim.mouse_x = expected.mouse_x;
        game.tick(aim);
        CHECK(world.bunker_assault_active);
        CHECK(world.transition_freeze);
        CHECK_EQ(world.objects[0].frame, expected.frame);
        CHECK_EQ(world.objects[0].timer, 1u);

        InputState ignored {};
        ignored.move_right = true;
        ignored.fire = true;
        for (std::uint32_t timer = 2; timer <= 10; ++timer) {
            game.tick(ignored);
            CHECK_EQ(world.objects[0].frame, expected.frame);
            CHECK_EQ(world.objects[0].timer, timer);
            CHECK_EQ(active_projectile_count(game), 0);
        }
    }
}

TEST_CASE("overrun with exactly three landed troopers does not invent a cleanup delay")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    for (const float x : {120.0f, 114.0f, 194.0f}) {
        niteraid::Object trooper {};
        trooper.active = true;
        trooper.type = ObjectType::LandedInvader;
        trooper.position = {x, 172.0f};
        world.objects.push_back(trooper);
    }
    tick_neutral(game, 2);
    CHECK(std::any_of(world.objects.begin(), world.objects.end(), [](const auto& object) {
        return object.active && object.assaulting && object.position.x == 120.0f && object.timer == 1;
    }));
    CHECK_EQ(world.scores.score, 0);
}

TEST_CASE("overrun waits beyond the former fixed delay for an active cleanup particle")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    world.bunker_assault_active = true;
    world.gameplay_state = GameplayState::ScriptedSequence;
    niteraid::Object trooper {};
    trooper.active = true;
    trooper.type = ObjectType::LandedInvader;
    trooper.position = {120, 172};
    world.objects.push_back(trooper);
    niteraid::Object particle {};
    particle.active = true;
    particle.type = ObjectType::ResolutionParticle;
    particle.position = {100, 0};
    particle.assault_stage = 60;
    world.objects.push_back(particle);
    tick_neutral(game, 60);
    CHECK(!world.objects[0].assaulting);
    CHECK_EQ(world.objects[1].assault_stage, 0);
    game.tick(InputState {});
    CHECK(!world.objects[0].assaulting);
    game.tick(InputState {});
    CHECK(world.objects[0].assaulting);
    CHECK_EQ(world.objects[0].timer, 1u);
}

TEST_CASE("overrun owner reaches the original cleanup RNG and motion phases from normal startup")
{
    Game game(std::uint16_t {0}, std::nullopt);
    struct RandomCheckpoint { int tick; std::uint32_t seed; };
    const RandomCheckpoint random_checkpoints[] {
        {671,3808909436u}, {818,3033129820u}, {952,3033129820u},
        {1239,996809229u}, {1662,3744082541u}, {2001,3744082541u},
        {2036,3509644653u}, {2305,3509644653u}, {2745,3509644653u},
        {3142,3509644653u},
    };
    struct Checkpoint { int tick; float x; int animation; int stage; int timer; };
    const Checkpoint checkpoints[] {
        {2055,120,0,0,1}, {2382,161,41,3,0}, {2397,161,41,1,0},
        {2427,155,47,2,0}, {2428,114,0,0,1}, {2803,161,47,1,0},
        {2833,155,53,2,0}, {2834,194,0,0,1}, {3097,161,33,1,0},
        {3102,160,34,1,0}, {3127,155,39,2,0},
    };
    while (game.world().frame_tick < 3142) {
        game.tick(InputState {});
        const auto& world = game.world();
        const auto tick = world.frame_tick;
        for (const auto& expected : random_checkpoints) {
            if (tick == expected.tick) CHECK_EQ(world.gameplay_rng_seed, expected.seed);
        }
        for (const auto& expected : checkpoints) {
            if (tick != expected.tick) continue;
            const auto actor = std::find_if(world.objects.begin(), world.objects.end(), [](const auto& object) {
                return object.active && !object.pending_destroy && object.type == ObjectType::LandedInvader && object.assaulting;
            });
            CHECK(actor != world.objects.end());
            if (actor != world.objects.end()) {
                CHECK_EQ(actor->position.x, expected.x);
                CHECK_EQ(actor->frame, expected.animation);
                CHECK_EQ(actor->assault_stage, expected.stage);
                CHECK_EQ(actor->timer, expected.timer);
            }
        }
        if (tick == 2397 || tick == 2803 || tick == 3097) CHECK(world.bunker_assault_direct_draw);
        if (tick == 3132) CHECK_EQ(niteraid::internals::bunker_door_sprite_index(world), 1);
        if (tick == 3137) CHECK_EQ(niteraid::internals::bunker_door_sprite_index(world), 0);
        if (tick == 3127) CHECK(std::find(world.sound_events.begin(), world.sound_events.end(), 0x339) != world.sound_events.end());
    }
    CHECK_EQ(game.world().bunker_assault_entries, 3);
    CHECK_EQ(game.world().transition_started_tick, 3142);
    CHECK_EQ(game.world().gameplay_rng_seed, 3509644653u);
    CHECK(!game.world().overrun_cleanup_rng_seed.has_value());
    CHECK(game.world().screen == Screen::GameOver);
    CHECK_EQ(niteraid::internals::bunker_door_sprite_index(game.world()), -1);
}

TEST_CASE("level one overrun sequence emits recovered bunker-script sounds")
{
    Game game(std::uint16_t {0}, std::nullopt);

    bool saw_bunker_assault = false;
    bool saw_setup_sound = false;
    bool saw_assault_sound = false;
    bool saw_bad_sustain_sound = false;
    bool saw_unrelated_novelty_sound = false;
    bool saw_walk_sound_a = false;
    bool saw_walk_sound_b = false;
    bool saw_terminal_prelude = false;
    int terminal_pulse_count = 0;
    bool saw_terminal_explosion_sound = false;
    bool saw_post_special_hold = false;
    bool saw_terminal_blowup_actor = false;
    bool door_open_during_terminal_blowup = false;
    bool door_open_during_hold = false;
    bool reached_game_over = false;
    int retained_landed_invaders_when_assault_started = -1;
    bool saw_first_entry_door_light_state = false;
    bool saw_assaulter_entry = false;
    bool saw_retained_entered_assaulter = false;
    int third_entry_tick = -1;
    int terminal_blowup_tick = -1;
    int game_over_tick = -1;
    bool matched_landed_set = false;
    bool matched_pre_overrun = false;
    bool matched_first_entry = false;
    bool matched_takeover = false;
    bool matched_explosion_start = false;
    bool assault_started_in_scripted_state = false;
    bool assault_started_without_transition_arm = false;

    for (int tick = 0; tick < 5000; ++tick) {
        game.tick(InputState {});
        const auto& world = game.world();
        if (world.frame_tick == 1360) {
            std::vector<int> landed_x;
            for (const auto& object : world.objects) {
                if (object.active && !object.pending_destroy &&
                    object.type == niteraid::ObjectType::LandedInvader) {
                    landed_x.push_back(static_cast<int>(object.position.x));
                }
            }
            std::sort(landed_x.begin(), landed_x.end());
            matched_landed_set = landed_x == std::vector<int> {6, 86, 246, 280};
        }
        for (const auto& object : world.objects) {
            if (!object.active || object.pending_destroy ||
                object.type != niteraid::ObjectType::LandedInvader || !object.assaulting) {
                continue;
            }
            matched_pre_overrun |= object.position.x == 154.0f && object.frame == 34;
            matched_first_entry |= object.position.x == 133.0f && object.frame == 19;
            matched_takeover |= object.position.x == 156.0f && object.frame == 42;
            matched_explosion_start |= object.position.x == 175.0f && object.frame == 19;
        }
        if (world.bunker_assault_active && !saw_bunker_assault) {
            saw_bunker_assault = true;
            assault_started_in_scripted_state =
                world.gameplay_state == GameplayState::ScriptedSequence;
            assault_started_without_transition_arm = !world.transition_armed;
            retained_landed_invaders_when_assault_started = static_cast<int>(std::count_if(
                world.objects.begin(), world.objects.end(), [](const auto& object) {
                    return object.active && !object.pending_destroy &&
                           object.type == niteraid::ObjectType::LandedInvader;
                }));
        }
        saw_post_special_hold |= world.bunker_white_flag;
        saw_first_entry_door_light_state |= world.bunker_special_effect_frames_remaining < 0;
        saw_assaulter_entry |= world.bunker_assault_entries > 0;
        if (world.bunker_assault_entries >= 3 && third_entry_tick < 0) {
            third_entry_tick = static_cast<int>(world.frame_tick);
        }
        if (world.bunker_assault_entries >= 3 &&
            world.bunker_special_effect_frames_remaining > 0) {
            saw_terminal_blowup_actor = true;
            if (terminal_blowup_tick < 0) {
                terminal_blowup_tick = static_cast<int>(world.frame_tick);
            }
        }
        saw_retained_entered_assaulter |= std::any_of(
            world.objects.begin(), world.objects.end(), [](const auto& object) {
                return object.active && !object.pending_destroy &&
                       object.type == niteraid::ObjectType::LandedInvader &&
                       object.assaulting && object.assault_stage == 2;
            });
        door_open_during_terminal_blowup |=
            world.bunker_assault_entries > 0 && world.bunker_special_effect_frames_remaining > 0;
        door_open_during_hold |= world.bunker_assault_entries > 0 && world.bunker_white_flag;
        for (const auto sound_id : world.sound_events) {
            saw_walk_sound_a |= sound_id == 0x33f;
            saw_walk_sound_b |= sound_id == 0x341;
            saw_setup_sound |= sound_id == 0x337;
            saw_assault_sound |= sound_id == 0x339;
            saw_bad_sustain_sound |= sound_id == 0x349 || sound_id == 0x34b;
            saw_terminal_prelude |= sound_id == 0x365;
            terminal_pulse_count += sound_id == 0x367 ? 1 : 0;
            saw_terminal_explosion_sound |= sound_id == 0x369;
            saw_unrelated_novelty_sound |= sound_id == 0x34d;
        }
        if (world.screen == Screen::GameOver) {
            reached_game_over = true;
            if (game_over_tick < 0) {
                game_over_tick = static_cast<int>(world.frame_tick);
                CHECK(world.reset_sound_effects);
            } else {
                CHECK(!world.reset_sound_effects);
            }
            if (saw_terminal_explosion_sound) {
                break;
            }
        }
    }

    CHECK(saw_bunker_assault);
    CHECK(assault_started_in_scripted_state);
    CHECK(assault_started_without_transition_arm);
    CHECK_EQ(retained_landed_invaders_when_assault_started, 11);
    CHECK(saw_setup_sound);
    CHECK(saw_assault_sound);
    CHECK(saw_walk_sound_a);
    CHECK(saw_walk_sound_b);
    CHECK(!saw_bad_sustain_sound);
    CHECK(!saw_unrelated_novelty_sound);
    CHECK(saw_terminal_prelude);
    CHECK_EQ(terminal_pulse_count, 3);
    CHECK(saw_terminal_explosion_sound);
    CHECK(saw_first_entry_door_light_state);
    CHECK(saw_assaulter_entry);
    CHECK(saw_retained_entered_assaulter);
    CHECK(saw_terminal_blowup_actor);
    CHECK(door_open_during_terminal_blowup);
    CHECK(door_open_during_hold);
    CHECK(saw_post_special_hold);
    CHECK(reached_game_over);
    CHECK(third_entry_tick >= 0);
    CHECK(terminal_blowup_tick >= third_entry_tick);
    CHECK(game_over_tick > terminal_blowup_tick);
    CHECK(matched_landed_set);
    CHECK(matched_pre_overrun);
    CHECK(matched_first_entry);
    CHECK(matched_takeover);
    CHECK(matched_explosion_start);
}

TEST_CASE("overrun preserves four pre-owner pictures without advancing simulation or RNG")
{
    Game game(std::uint16_t {0}, std::nullopt);
    const std::array<std::uint32_t, 4> expected_ticks {{2014, 2397, 2803, 3097}};
    std::size_t presentations = 0;
    while (game.world().frame_tick < 3142) {
        const auto previous_tick = game.world().frame_tick;
        game.tick(InputState {});
        const auto tick = game.world().frame_tick;
        CHECK_EQ(tick, previous_tick + 1);
        if (!game.before_owner_page()) continue;
        CHECK(presentations < expected_ticks.size());
        if (presentations >= expected_ticks.size()) return;
        CHECK_EQ(tick, expected_ticks[presentations]);
        const auto& before = *game.before_owner_page();
        const auto& after = game.world();
        CHECK_EQ(before.frame_tick, after.frame_tick);
        CHECK_EQ(before.gameplay_rng_seed, after.gameplay_rng_seed);
        CHECK_EQ(before.scores.score, after.scores.score);
        if (presentations == 0) {
            CHECK(!before.bunker_assault_active);
            CHECK(after.bunker_assault_active);
        } else {
            CHECK(before.bunker_assault_active);
            CHECK(!before.bunker_assault_direct_draw);
            CHECK(after.bunker_assault_direct_draw);
            auto actor = std::find_if(before.objects.begin(), before.objects.end(), [](const auto& object) {
                return object.active && object.assaulting && object.assault_stage != 2;
            });
            CHECK(actor != before.objects.end());
            if (actor == before.objects.end()) return;
            CHECK_EQ(actor->position.x, 161.0f);
            CHECK_EQ(actor->position.y, 172.0f);
            CHECK(actor->assault_stage == 0 || actor->assault_stage == 3);
        }
        ++presentations;
    }
    CHECK_EQ(presentations, 4u);
    game.tick(InputState {});
    CHECK(!game.before_owner_page());
}

TEST_CASE("terminal game-over audio follows blocking original mixer schedule")
{
    Game game;
    auto& world = game.diagnostic_world();
    world.screen = Screen::GameOver;
    world.gameplay_state = GameplayState::GameOver;
    world.player_dead = true;
    world.bunker_white_flag = true;
    world.bunker_assault_entries = 3;
    world.transition_started_tick = world.frame_tick;
    world.game_over_frames_remaining = 720;

    std::vector<std::pair<std::uint32_t, std::uint16_t>> events;
    for (int tick = 0; tick < 500; ++tick) {
        game.tick(InputState {});
        for (const auto sound : game.world().sound_events) {
            events.emplace_back(game.world().frame_tick, sound);
        }
    }

    const std::array<std::pair<std::uint32_t, std::uint16_t>, 5> expected {{
        {21, 0x365},
        {261, 0x367},
        {322, 0x367},
        {384, 0x367},
        {448, 0x369},
    }};
    CHECK_EQ(events.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK_EQ(events[index].first, expected[index].first);
        CHECK_EQ(events[index].second, expected[index].second);
    }
}

TEST_CASE("fire press during bunker overrun does not force terminal skip")
{
    Game game(std::uint16_t {0}, std::nullopt);

    bool reached_bunker_assault = false;
    for (int tick = 0; tick < 5000; ++tick) {
        game.tick(InputState {});
        if (game.world().bunker_assault_active) {
            reached_bunker_assault = true;
            break;
        }
    }

    CHECK(reached_bunker_assault);

    InputState fire {};
    fire.fire = true;
    game.tick(fire);

    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
    CHECK_EQ(static_cast<int>(game.world().gameplay_state),
             static_cast<int>(GameplayState::ScriptedSequence));
    CHECK(game.world().bunker_assault_active);
    CHECK(!game.world().overrun_interrupt_residue);
    CHECK(!game.world().bunker_white_flag);
}

TEST_CASE("escape during bunker overrun enters retained residue game-over state")
{
    Game game(std::uint16_t {0}, std::nullopt);

    bool reached_bunker_assault = false;
    for (int tick = 0; tick < 5000; ++tick) {
        game.tick(InputState {});
        if (game.world().bunker_assault_active) {
            reached_bunker_assault = true;
            break;
        }
    }

    CHECK(reached_bunker_assault);

    const auto source_tick = game.world().frame_tick;
    const auto source_entries = game.world().bunker_assault_entries;
    InputState escape {};
    escape.escape = true;
    game.tick(escape);

    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));
    CHECK_EQ(static_cast<int>(game.world().gameplay_state), static_cast<int>(GameplayState::GameOver));
    CHECK(game.world().player_dead);
    CHECK(game.world().overrun_interrupt_residue);
    CHECK(!game.world().transition_freeze);
    CHECK(game.world().bunker_assault_active);
    CHECK_EQ(game.world().bunker_assault_entries, source_entries);
    CHECK_EQ(game.world().transition_started_tick, source_tick + 1);
    CHECK(game.world().reset_sound_effects);
    CHECK(!game.world().bunker_white_flag);

    tick_neutral(
        game, static_cast<int>(niteraid::presenter_timing::kOverrunRetainedPageVideoFrames - 1));
    CHECK(game.world().overrun_interrupt_residue);

    game.tick(InputState {});
    CHECK(!game.world().reset_sound_effects);
    CHECK(!game.world().overrun_interrupt_residue);
    CHECK(game.world().bunker_white_flag);
    CHECK_EQ(game.world().bunker_assault_entries, 3);

    tick_neutral(
        game,
        static_cast<int>(
            niteraid::presenter_timing::kExplosionFrameCount *
                niteraid::presenter_timing::kExplosionVideoFramesEach +
            niteraid::presenter_timing::kFlagRaiseFrameCount *
                niteraid::presenter_timing::kFlagVideoFramesEach));
    InputState exit {};
    exit.escape = true;
    game.tick(exit);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::GameOver));
    tick_neutral(game, niteraid::presenter_timing::kFlagVideoFramesEach - 1);
    CHECK(static_cast<int>(game.world().screen) != static_cast<int>(Screen::GameOver));
}

TEST_CASE("escape retires the active bunker assaulter at each observed phase")
{
    for (const int interrupt_tick : {2057, 2584, 2996}) {
        Game game(std::uint16_t {0}, std::nullopt);
        while (game.world().frame_tick + 1 < static_cast<std::uint32_t>(interrupt_tick)) {
            game.tick(InputState {});
        }
        CHECK(game.world().bunker_assault_active);

        int landed_before = 0;
        int assaulting_before = 0;
        for (const auto& object : game.world().objects) {
            if (object.active && !object.pending_destroy &&
                object.type == ObjectType::LandedInvader) {
                ++landed_before;
                assaulting_before += object.assaulting ? 1 : 0;
            }
        }
        CHECK_EQ(assaulting_before, 1);

        InputState escape {};
        escape.escape = true;
        game.tick(escape);

        int landed_after = 0;
        int assaulting_after = 0;
        for (const auto& object : game.world().objects) {
            if (object.active && !object.pending_destroy &&
                object.type == ObjectType::LandedInvader) {
                ++landed_after;
                assaulting_after += object.assaulting ? 1 : 0;
            }
        }
        CHECK_EQ(landed_after, landed_before - 1);
        CHECK_EQ(assaulting_after, 0);
        CHECK(game.world().overrun_retained_page.has_value());
        if (game.world().overrun_retained_page) {
            int drawn_landed = 0;
            for (const auto& object : game.world().overrun_retained_page->objects) {
                if (object.active && !object.pending_destroy &&
                    object.type == ObjectType::LandedInvader) {
                    ++drawn_landed;
                }
            }
            CHECK_EQ(drawn_landed, landed_before);
        }
    }
}

TEST_CASE("resolution particle setup retains original palette lookup and fixed-point range")
{
    const auto first = niteraid::internals::resolution_particle_setup({25593, 28582, 28631, 15467});
    CHECK_EQ(first.color, 0x6c);
    CHECK_EQ(first.vx, 24397);
    CHECK_EQ(first.vy, 24495);
    CHECK_EQ(first.lifetime, 35);
    const auto low = niteraid::internals::resolution_particle_setup({0, 0, 0, 0});
    CHECK_EQ(low.color, 0x0c);
    CHECK_EQ(low.vx, -32768);
    CHECK_EQ(low.lifetime, 32);
    const auto high = niteraid::internals::resolution_particle_setup({32767, 32767, 32767, 32767});
    CHECK_EQ(high.color, 0xa1);
    CHECK_EQ(high.vy, 32768);
    CHECK_EQ(high.lifetime, 39);
    CHECK(!niteraid::internals::original_motion_outside_bounds({319.5f, 199.5f}));
    CHECK(niteraid::internals::original_motion_outside_bounds({320.0f, 199.5f}));
    CHECK(niteraid::internals::original_motion_outside_bounds({319.5f, 200.0f}));
    CHECK(niteraid::internals::original_motion_outside_bounds({-100.0f, 0.0f}));
    CHECK(niteraid::internals::original_motion_outside_bounds({0.0f, -40.0f}));
}

TEST_CASE("resolution particles apply gravity before movement and expire without a final step")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    niteraid::Object particle {};
    particle.active = true;
    particle.type = niteraid::ObjectType::ResolutionParticle;
    particle.position = {100.0f, 180.0f};
    particle.velocity = {0.25f, -0.5f};
    particle.assault_stage = 1;
    world.objects.clear();
    world.objects.push_back(particle);
    game.tick(InputState {});
    CHECK_NEAR(world.objects[0].position.x, 100.25f, 1e-6f);
    CHECK_NEAR(world.objects[0].position.y, 179.53125f, 1e-6f);
    CHECK_EQ(world.objects[0].assault_stage, 0);
    CHECK_EQ(world.objects[0].timer, 0u);
    game.tick(InputState {});
    CHECK(!world.objects[0].active || world.objects[0].pending_destroy);
}

TEST_CASE("falling type-ten trooper retires on the exact right edge")
{
    for (const float start_x : {319.5f, 319.75f}) {
        Game game(std::uint16_t {0}, std::nullopt);
        auto& world = game.diagnostic_world();
        world.objects.clear();
        world.object_highwater.reset();
        world.wave_banks = {};
        niteraid::Object trooper {};
        trooper.active = true;
        trooper.type = ObjectType::GroundedTransition;
        trooper.parachute_lost = true;
        trooper.position = {start_x, 33.0f};
        trooper.velocity = {0.25f, 0.5f};
        trooper.extent = {2.5f, 3.0f};
        trooper.frame = 1;
        trooper.timer = 1;
        world.objects.push_back(trooper);

        game.tick(InputState {});
        CHECK_EQ(world.objects[0].active, start_x == 319.5f);
    }
}

TEST_CASE("trooper splatter uses live projectile motion in both collision slot orders")
{
    struct CollisionCase {
        bool no_chute;
        bool projectile_first;
    };
    for (const auto [no_chute, projectile_first] : std::array<CollisionCase, 4> {{
             {false, false}, {false, true}, {true, false}, {true, true}}}) {
        Game game(std::uint16_t {0}, std::nullopt);
        auto& world = game.diagnostic_world();
        world.objects.clear();
        niteraid::Object trooper {};
        trooper.active = true;
        trooper.type = no_chute ? niteraid::ObjectType::GroundedTransition : niteraid::ObjectType::Paratrooper;
        trooper.parachute_lost = no_chute;
        trooper.assault_stage = 1;
        trooper.position = {100.0f, 70.0f};
        trooper.extent = no_chute ? niteraid::Vec2 {3.0f, 2.5f} : niteraid::Vec2 {5.0f, 7.5f};
        niteraid::Object shot {};
        shot.active = true;
        shot.type = niteraid::ObjectType::PlayerProjectile;
        shot.position = {102.75f, no_chute ? 74.0f : 84.0f};
        shot.velocity = {0.5f, -0.5f};
        shot.extent = {1.0f, 1.0f};
        world.objects = projectile_first ? std::vector<niteraid::Object> {shot, trooper}
                                         : std::vector<niteraid::Object> {trooper, shot};
        world.objects.shrink_to_fit();
        auto seed = world.gameplay_rng_seed;
        game.tick(InputState {});
        int count = 0;
        const bool reuses_projectile = no_chute && !projectile_first;
        auto impact_velocity = reuses_projectile ? niteraid::Vec2 {} : shot.velocity;
        for (const auto& particle : world.objects) {
            if (!particle.active || particle.pending_destroy ||
                particle.type != niteraid::ObjectType::ResolutionParticle) {
                continue;
            }
            std::array<std::uint16_t, 4> draws {};
            for (auto& draw : draws) {
                draw = niteraid::original_random_next(seed);
            }
            const auto setup = niteraid::internals::resolution_particle_setup(draws);
            CHECK_NEAR(particle.position.x, shot.position.x + shot.velocity.x, 1e-6f);
            CHECK_NEAR(particle.position.y, shot.position.y + shot.velocity.y, 1e-6f);
            CHECK_EQ(particle.velocity.x, impact_velocity.x + setup.vx / 65536.0f);
            CHECK_EQ(particle.velocity.y, impact_velocity.y + setup.vy / 65536.0f);
            CHECK_EQ(particle.assault_stage, setup.lifetime);
            CHECK_EQ(particle.frame, setup.color);
            if (reuses_projectile && count == 0) {
                impact_velocity = particle.velocity;
            }
            ++count;
        }
        CHECK_EQ(count, 8);
        CHECK_EQ(world.gameplay_rng_seed, seed);
        CHECK_EQ(world.scores.score, 2);
        CHECK_EQ(world.objects.size(), reuses_projectile ? std::size_t {9} : std::size_t {10});
    }
}

TEST_CASE("debris-hit splatter starts at the fragment rather than the trooper corner")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    niteraid::Object trooper {};
    trooper.active = true;
    trooper.type = niteraid::ObjectType::Paratrooper;
    trooper.assault_stage = 1;
    trooper.position = {100.0f, 70.0f};
    trooper.extent = {5.0f, 7.5f};
    world.objects.push_back(trooper);
    niteraid::Object debris {};
    debris.active = true;
    debris.type = niteraid::ObjectType::AircraftDebris;
    debris.has_dropped_payload = true;
    debris.position = {105.0f, 82.0f};
    debris.velocity = {0.25f, 0.25f};
    world.objects.push_back(debris);
    auto seed = world.gameplay_rng_seed;
    std::array<std::uint16_t, 4> draws {};
    for (auto& draw : draws) draw = niteraid::original_random_next(seed);
    const auto setup = niteraid::internals::resolution_particle_setup(draws);
    game.tick(InputState {});
    const auto first = std::find_if(world.objects.begin(), world.objects.end(), [](const auto& actor) {
        return actor.active && !actor.pending_destroy && actor.type == niteraid::ObjectType::ResolutionParticle;
    });
    CHECK(first != world.objects.end());
    if (first != world.objects.end()) {
        CHECK_NEAR(first->position.x, 105.25f, 1e-6f);
        CHECK_NEAR(first->position.y, 82.28125f, 1e-6f);
        CHECK_NEAR(first->velocity.x, 0.25f + setup.vx / 65536.0f, 1e-6f);
        CHECK_NEAR(first->velocity.y, 0.28125f + setup.vy / 65536.0f, 1e-6f);
    }
}

TEST_CASE("self-resolution splatter uses the original five and twelve pixel offset")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    niteraid::Object trooper {};
    trooper.active = true;
    trooper.type = niteraid::ObjectType::LandedInvader;
    trooper.position = {-1.0f, 172.0f};
    world.objects.push_back(trooper);
    game.tick(InputState {});
    int count = 0;
    for (const auto& particle : world.objects) {
        if (!particle.active || particle.pending_destroy ||
            particle.type != niteraid::ObjectType::ResolutionParticle) continue;
        // Self-resolution happens during updates, so children have moved once.
        CHECK_NEAR(particle.position.x - particle.velocity.x, 4.0f, 1e-6f);
        CHECK_NEAR(particle.position.y - particle.velocity.y, 184.0f, 1e-6f);
        ++count;
    }
    CHECK_EQ(count, 8);
}

TEST_CASE("ground-impact splatter inherits the original upward velocity before self-resolution")
{
    for (const bool finale : {false, true}) {
        Game game(std::uint16_t {0}, std::nullopt);
        auto& world = game.diagnostic_world();
        world.objects.clear();
        niteraid::Object trooper {};
        trooper.active = true;
        trooper.type = finale ? niteraid::ObjectType::Paratrooper : niteraid::ObjectType::GroundedTransition;
        trooper.parachute_lost = true;
        trooper.finale_drop = finale;
        trooper.assault_stage = 1;
        trooper.position = {100.0f, 174.75f};
        trooper.velocity = {0.25f, 0.5f};
        world.objects.push_back(trooper);
        auto seed = world.gameplay_rng_seed;
        std::array<std::uint16_t, 4> draws {};
        for (auto& draw : draws) draw = niteraid::original_random_next(seed);
        const auto setup = niteraid::internals::resolution_particle_setup(draws);
        game.tick(InputState {});
        const auto first = std::find_if(world.objects.begin(), world.objects.end(), [](const auto& actor) {
            return actor.active && !actor.pending_destroy && actor.type == niteraid::ObjectType::ResolutionParticle;
        });
        CHECK(first != world.objects.end());
        if (first != world.objects.end()) {
            CHECK_NEAR(first->position.x - first->velocity.x, 107.25f, 1e-6f);
            CHECK_NEAR(first->position.y - first->velocity.y, 190.25f, 1e-6f);
            CHECK_NEAR(first->velocity.y, -1.0f + setup.vy / 65536.0f + 0.03125f, 1e-6f);
        }
    }
}

TEST_CASE("landing merge uses the surviving neighbor as its particle origin")
{
    Game game(std::uint16_t {0}, std::nullopt);
    auto& world = game.diagnostic_world();
    world.objects.clear();
    niteraid::Object trooper {};
    trooper.active = true;
    trooper.type = niteraid::ObjectType::GroundedTransition;
    trooper.position = {100.0f, 164.0f};
    trooper.frame = 7;
    world.objects.push_back(trooper);
    trooper.type = niteraid::ObjectType::LandedInvader;
    trooper.position = {107.0f, 172.0f};
    world.objects.push_back(trooper);
    game.tick(InputState {});
    int count = 0;
    for (const auto& particle : world.objects) {
        if (!particle.active || particle.pending_destroy ||
            particle.type != niteraid::ObjectType::ResolutionParticle) continue;
        CHECK_NEAR(particle.position.x - particle.velocity.x, 107.0f, 1e-6f);
        CHECK_NEAR(particle.position.y - particle.velocity.y, 172.0f, 1e-6f);
        ++count;
    }
    CHECK_EQ(count, 8);
}

TEST_CASE("shareware edition runs its dedicated ending after level four")
{
    Game game(std::nullopt, std::nullopt, false, false, false, true);
    auto& world = game.diagnostic_world();
    world.screen = Screen::Intermission;
    world.current_level = 3;
    world.milestone_intermission = MilestoneIntermission::Level4Pizza;
    world.intermission_frames_remaining = 1;

    game.tick(InputState {});
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Intermission));
    CHECK_EQ(game.world().milestone_intermission_frame, 1865u);
    game.tick(InputState {});

    CHECK_EQ(static_cast<int>(game.world().screen),
             static_cast<int>(Screen::SharewareEnding));
    CHECK_EQ(game.world().shareware_ending_frame, 0);
    CHECK_EQ(game.world().shareware_ending_audio_cue, 1);
    CHECK_EQ(game.world().sound_events.size(), 1);
    if (!game.world().sound_events.empty()) {
        CHECK_EQ(game.world().sound_events.front(), 0x32b);
    }

    tick_neutral(game, 6);
    CHECK_EQ(game.world().shareware_ending_frame, 0);
    game.tick(InputState {});
    CHECK_EQ(game.world().shareware_ending_frame, 1);

    InputState early_escape {};
    early_escape.escape = true;
    game.tick(early_escape);
    CHECK_EQ(static_cast<int>(game.world().screen),
             static_cast<int>(Screen::SharewareEnding));
    CHECK(!game.world().shareware_ending_waiting_for_input);

    tick_neutral(game, 154 * 7);
    CHECK(game.world().shareware_ending_waiting_for_input);
    CHECK_EQ(game.world().shareware_ending_frame, 153);

    InputState acknowledge {};
    acknowledge.accept = true;
    game.tick(acknowledge);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Title));
}

TEST_CASE("shareware confirmed quit presents the ending before exiting")
{
    Game game(std::nullopt, std::nullopt, false, false, false, true);
    auto& world = game.diagnostic_world();
    world.screen = Screen::ControlPanel;
    world.confirmation_prompt = ConfirmationPromptAction::QuitToDos;

    InputState confirm {};
    confirm.accept = true;
    game.tick(confirm);
    CHECK_EQ(static_cast<int>(game.world().screen),
             static_cast<int>(Screen::SharewareEnding));
    CHECK(game.world().shareware_ending_exit_after);
    CHECK(!game.world().quit_requested);

    tick_neutral(game, 154 * 7);
    CHECK(game.world().shareware_ending_waiting_for_input);
    InputState acknowledge {};
    acknowledge.fire = true;
    game.tick(acknowledge);
    CHECK(game.world().quit_requested);
}
