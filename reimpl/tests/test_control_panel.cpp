// Per the decomp dump in tmp-ghidra/control_panel_decomp.txt:
//   row 0 ↔ audio_config_words[3]  (DAT_2730_1ff8)
//   row 1 ↔ audio_config_words[4]  (DAT_2730_1ffa)
//   row 2 ↔ audio_config_words[5]  (DAT_2730_1ffc)
//   row 3..5 are START / BACK / QUIT actions
//
// Input scheme from RunControlPanelInputLoop (1480:0d87):
//   UP/LEFT  → cursor up
//   DOWN/RIGHT → cursor down
//   Enter on audio row → (cur + 1) % max; row 1 max is zero unless sound mode is Sound Source/SoundBlaster
//   Enter on action row → activate

#include "niteraid/game.hpp"
#include "test_harness.hpp"

using niteraid::Game;
using niteraid::ConfirmationPromptAction;
using niteraid::InputState;
using niteraid::Screen;

namespace {

constexpr int kReplayShortTapTicks = 6;
constexpr int kReplayInterActionTicks = 9;
constexpr int kReplaySpacedTapGapTicks = 30;
constexpr int kLogicalNavigationGapTicks = 40;

void hold(Game& g, InputState in, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        g.tick(in);
    }
}

void press(Game& g, InputState held, void (*set_press)(InputState&))
{
    InputState pressed = held;
    set_press(pressed);
    g.tick(pressed);  // edge
    g.tick(held);     // release
}

void press_menu(Game& g) { press(g, InputState{}, [](InputState& i) { i.menu = true; }); }
void press_up(Game& g)
{
    press(g, InputState{}, [](InputState& i) { i.move_up = true; });
    hold(g, InputState {}, kLogicalNavigationGapTicks);
}
void press_down(Game& g)
{
    press(g, InputState{}, [](InputState& i) { i.move_down = true; });
    hold(g, InputState {}, kLogicalNavigationGapTicks);
}
void press_enter(Game& g) { press(g, InputState{}, [](InputState& i) { i.accept = true; i.start = true; }); }
void press_escape(Game& g) { press(g, InputState{}, [](InputState& i) { i.escape = true; }); }
void press_decline(Game& g) { press(g, InputState{}, [](InputState& i) { i.decline = true; }); }
void open_title_panel(Game& g) { press_enter(g); }

void replay_navigation_tap(Game& g, bool up, int gap_ticks)
{
    InputState input {};
    input.move_up = up;
    input.move_left = !up;
    hold(g, input, kReplayShortTapTicks);
    hold(g, InputState {}, 1);
    hold(g, InputState {}, gap_ticks);
}

void finish_action(Game& g)
{
    for (int tick = 0; tick < 20 && g.world().control_panel_pending_action_row >= 0; ++tick) {
        g.tick(InputState {});
    }
}

}  // namespace

TEST_CASE("Fire on Title enters Control Panel, cursor at row 3")
{
    Game game;
    game.tick(InputState{});  // initial title state
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Title));

    InputState fire {};
    fire.fire = true;
    game.tick(fire);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::ControlPanel));
    CHECK_EQ(game.world().control_panel_row, 3);
}

TEST_CASE("Menu mouse activation waits for all buttons before committing sound")
{
    for (const bool secondary : {false, true}) {
        Game game;
        open_title_panel(game);
        press_up(game);
        press_up(game);
        press_up(game);
        game.diagnostic_world().audio_config_words[3] = 1;
        InputState held {};
        held.mouse_primary = !secondary;
        held.mouse_secondary = secondary;
        held.fire = !secondary;
        hold(game, held, 10);
        CHECK_EQ(game.world().audio_config_words[3], 1);
        game.tick(InputState {});
        CHECK_EQ(game.world().audio_config_words[3], 3);
    }
}

TEST_CASE("Menu retains a keyboard tap received during mouse release ownership")
{
    Game game;
    open_title_panel(game);
    press_up(game);
    press_up(game);
    press_up(game);
    InputState held {};
    held.mouse_secondary = true;
    game.tick(held);
    auto down = held;
    down.move_down = true;
    game.tick(down);
    game.tick(held);
    CHECK_EQ(game.world().control_panel_row, 0);
    game.tick(InputState {});
    game.tick(InputState {});
    CHECK_EQ(game.world().audio_config_words[3], 0);
    CHECK_EQ(game.world().control_panel_row, 2);
}

TEST_CASE("Motion with a held button selects then activates the menu row")
{
    Game game;
    open_title_panel(game);
    InputState held {};
    held.mouse_primary = true;
    held.fire = true;
    held.mouse_moved = true;
    held.mouse_y = 32;
    game.tick(held);
    CHECK_EQ(game.world().control_panel_row, 0);
    held.mouse_moved = false;
    game.tick(held);
    game.tick(InputState {});
    CHECK_EQ(game.world().audio_config_words[3], 0);
}

TEST_CASE("Quit prompt keeps its selected mouse response until all buttons release")
{
    Game game;
    open_title_panel(game);
    press_escape(game);
    finish_action(game);
    CHECK(game.world().confirmation_prompt == ConfirmationPromptAction::QuitToDos);
    InputState held {};
    held.mouse_secondary = true;
    game.tick(held);
    CHECK(game.world().confirmation_prompt == ConfirmationPromptAction::QuitToDos);
    held.mouse_primary = true;
    held.fire = true;
    game.tick(held);
    held.mouse_secondary = false;
    game.tick(held);
    CHECK(game.world().confirmation_prompt == ConfirmationPromptAction::QuitToDos);
    game.tick(InputState {});
    CHECK(game.world().confirmation_prompt == ConfirmationPromptAction::None);
    CHECK(!game.world().quit_requested);
    CHECK_EQ(game.world().control_panel_row, 3);
}

TEST_CASE("Action row does not dispatch while its activating mouse button is held")
{
    Game game;
    open_title_panel(game);
    InputState held {};
    held.mouse_primary = true;
    held.fire = true;
    hold(game, held, 30);
    CHECK(game.world().screen == Screen::ControlPanel);
    game.tick(InputState {});
    CHECK(game.world().screen == Screen::Gameplay);
}

TEST_CASE("Menu pointer uses recovered row hit regions and strict bottom bounds")
{
    const int positions[] = {-1, 32, 51, 52, 71, 72, 91, 92, 110, 111, 130, 131, 150, 151, 170, 171, 172};
    const int expected[] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 3, 5};
    for (std::size_t index = 0; index < std::size(positions); ++index) {
        Game game;
        open_title_panel(game);
        InputState input {};
        input.mouse_moved = true;
        input.mouse_x = -1000.0f;
        input.mouse_y = static_cast<float>(positions[index]);
        game.tick(input);
        CHECK_EQ(game.world().control_panel_row, expected[index]);
        CHECK_EQ(game.world().control_panel_initial_hint, expected[index] == 3);
    }
}

TEST_CASE("Menu pointer skips disabled voice row without selecting its neighbor")
{
    Game game;
    open_title_panel(game);
    game.diagnostic_world().audio_config_words[3] = 0;
    InputState input {};
    input.mouse_moved = true;
    input.mouse_y = 52.0f;
    game.tick(input);
    CHECK_EQ(game.world().control_panel_row, 3);
    input.mouse_y = 72.0f;
    game.tick(input);
    CHECK_EQ(game.world().control_panel_row, 2);
}

TEST_CASE("Menu pointer uses detected hardware not the stored mouse preference")
{
    Game game;
    open_title_panel(game);
    game.diagnostic_world().audio_config_words[7] = 0;
    InputState input {};
    input.mouse_moved = true;
    input.mouse_y = 0.0f;
    game.tick(input);
    CHECK_EQ(game.world().control_panel_row, 0);
    game.diagnostic_world().audio_config_words[6] = 0;
    input.mouse_y = 200.0f;
    game.tick(input);
    CHECK_EQ(game.world().control_panel_row, 0);
}

TEST_CASE("Stationary pointer does not undo keyboard menu navigation")
{
    Game game;
    open_title_panel(game);
    InputState input {};
    input.mouse_y = 0.0f;
    input.mouse_moved = true;
    game.tick(input);
    input.mouse_moved = false;
    input.move_down = true;
    game.tick(input);
    CHECK_EQ(game.world().control_panel_row, 1);
    input.move_down = false;
    game.tick(input);
    CHECK_EQ(game.world().control_panel_row, 1);
}

TEST_CASE("gamepad Start pauses gameplay and B resumes")
{
    Game game(std::optional<std::uint16_t> {0});
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));

    InputState start {};
    start.gamepad_start = true;
    start.start = true;
    game.tick(start);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::ControlPanel));
    CHECK(game.world().control_panel_from_gameplay);

    game.tick(InputState {});
    InputState back {};
    back.gamepad_back = true;
    game.tick(back);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
}

TEST_CASE("Tab from Title advances to the next attract screen")
{
    Game game;
    game.tick(InputState{});
    press_menu(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Credits));
}

TEST_CASE("Control Panel cursor moves without wrapping")
{
    Game game;
    game.tick(InputState{});
    open_title_panel(game);
    press_down(game);
    press_down(game);
    CHECK_EQ(game.world().control_panel_row, 5);
    press_down(game);
    CHECK_EQ(game.world().control_panel_row, 5);
    for (int i = 0; i < 5; ++i) press_up(game);
    CHECK_EQ(game.world().control_panel_row, 0);
    press_up(game);
    CHECK_EQ(game.world().control_panel_row, 0);
}

TEST_CASE("Enhanced Control Panel accepts released taps without a navigation cooldown")
{
    Game rapid(std::nullopt, std::nullopt, false, false, true);
    rapid.tick(InputState {});
    open_title_panel(rapid);
    replay_navigation_tap(rapid, true, kReplayInterActionTicks);
    CHECK_EQ(rapid.world().control_panel_row, 2);
    replay_navigation_tap(rapid, true, kReplayInterActionTicks);
    CHECK_EQ(rapid.world().control_panel_row, 1);
    replay_navigation_tap(rapid, true, kReplayInterActionTicks);
    CHECK_EQ(rapid.world().control_panel_row, 0);
    replay_navigation_tap(rapid, false, kReplayInterActionTicks);
    CHECK_EQ(rapid.world().control_panel_row, 0);

    Game spaced(std::nullopt, std::nullopt, false, false, true);
    spaced.tick(InputState {});
    open_title_panel(spaced);
    replay_navigation_tap(spaced, true, kReplaySpacedTapGapTicks);
    replay_navigation_tap(spaced, true, kReplaySpacedTapGapTicks);
    replay_navigation_tap(spaced, true, kReplaySpacedTapGapTicks);
    CHECK_EQ(spaced.world().control_panel_row, 0);
}

TEST_CASE("Faithful panel accepts consecutive released navigation taps")
{
    Game game;
    open_title_panel(game);
    for (int expected_row : {2, 1, 0}) {
        press(game, InputState {}, [](InputState& input) { input.move_up = true; });
        CHECK_EQ(game.world().control_panel_row, expected_row);
    }
}

TEST_CASE("Enhanced panel prioritizes Up for opposed navigation directions")
{
    Game game(std::nullopt, std::nullopt, false, false, true);
    open_title_panel(game);
    InputState opposed {};
    opposed.move_up = true;
    opposed.move_down = true;
    game.tick(opposed);
    CHECK_EQ(game.world().control_panel_row, 2);
}

TEST_CASE("Repeated Down makes skip disabled voices and stop at the last menu row")
{
    Game game;
    open_title_panel(game);
    press_up(game);
    press_up(game);
    press_up(game);
    game.diagnostic_world().audio_config_words[3] = 0;
    InputState input {};
    input.move_down = true;
    game.tick(input);
    CHECK_EQ(game.world().control_panel_row, 2);
    input.menu_navigation_down_repeat = true;
    for (const int expected : {3, 4, 5, 5}) {
        game.tick(input);
        CHECK_EQ(game.world().control_panel_row, expected);
    }
}

TEST_CASE("Enhanced held-arrow behavior accepts new keyboard repeat events")
{
    Game game(std::nullopt, std::nullopt, false, false, true);
    open_title_panel(game);
    InputState input {};
    input.move_up = true;
    game.tick(input);
    hold(game, input, kLogicalNavigationGapTicks);
    input.menu_navigation_up_repeat = true;
    game.tick(input);
    CHECK_EQ(game.world().control_panel_row, 1);
}

TEST_CASE("Faithful sound selector skips unavailable Sound Source hardware")
{
    Game game;
    game.diagnostic_world().audio_config_words = niteraid::config_internals::kDefaultWords;
    open_title_panel(game);
    press_up(game);
    press_up(game);
    press_up(game);
    for (const std::uint16_t expected_mode : {0, 1, 3, 0, 1, 3}) {
        press_enter(game);
        CHECK_EQ(game.world().audio_config_words[niteraid::config_internals::kSoundMode], expected_mode);
        CHECK_EQ(game.world().audio_config_words[niteraid::config_internals::kVoiceCount], 2u);
    }
}

TEST_CASE("Enter on audio row 0 cycles audio_config_words[3]")
{
    Game game;
    game.tick(InputState{});
    open_title_panel(game);
    press_up(game);
    press_up(game);
    press_up(game);
    const auto start_value = game.world().audio_config_words[3];
    press_enter(game);
    CHECK_EQ(game.world().audio_config_words[3], (start_value + 1) % 4);
    CHECK(game.world().control_panel_dirty);
}

TEST_CASE("Enter on row 1 cycles audio_config_words[4] (voice count)")
{
    Game game;
    game.tick(InputState{});
    open_title_panel(game);
    press_up(game);
    press_up(game);  // row 1
    const auto start_value = game.world().audio_config_words[4];
    press_enter(game);
    CHECK_EQ(game.world().audio_config_words[4], (start_value + 1) % 3);
}

TEST_CASE("Enter on row 2 cycles audio_config_words[5] (music)")
{
    Game game;
    game.tick(InputState{});
    open_title_panel(game);
    press_up(game);  // row 2
    const auto start_value = game.world().audio_config_words[5];
    press_enter(game);
    CHECK_EQ(game.world().audio_config_words[5], (start_value + 1) % 2);
}

TEST_CASE("Voice row is skipped when sound mode does not support voices")
{
    Game game;
    game.tick(InputState{});
    open_title_panel(game);
    press_up(game);
    press_up(game);
    press_up(game);  // row 0

    while (game.world().audio_config_words[3] != 0) {
        press_enter(game);
    }

    press_down(game);
    CHECK_EQ(game.world().control_panel_row, 2);

    press_up(game);
    CHECK_EQ(game.world().control_panel_row, 0);
}

TEST_CASE("Enter on row 4 (BACK TO DEMO) returns to Title attract")
{
    Game game;
    game.tick(InputState{});
    open_title_panel(game);
    press_down(game);  // row 4
    CHECK_EQ(game.world().control_panel_row, 4);
    press_enter(game);
    CHECK_EQ(game.world().control_panel_pending_action_row, 4);
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Title));
}

TEST_CASE("Enter on row 5 (QUIT TO DOS) opens confirmation prompt")
{
    Game game;
    game.tick(InputState{});
    open_title_panel(game);
    press_down(game);
    press_down(game);  // row 5
    CHECK_EQ(game.world().control_panel_row, 5);
    press_enter(game);
    CHECK_EQ(game.world().control_panel_pending_action_row, 5);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::None));
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::QuitToDos));
    CHECK(!game.world().quit_requested);
}

TEST_CASE("Escape on the control panel presses QUIT before opening its prompt")
{
    Game game;
    game.tick(InputState {});
    open_title_panel(game);

    press_escape(game);
    CHECK_EQ(game.world().control_panel_row, 5);
    CHECK_EQ(game.world().control_panel_pending_action_row, 5);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::None));

    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::QuitToDos));
}

TEST_CASE("Quit confirmation accepts Enter and cancels with Escape/N")
{
    Game game;
    game.tick(InputState{});
    press_escape(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::ControlPanel));
    CHECK_EQ(game.world().control_panel_row, 5);
    press_enter(game);
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::QuitToDos));
    press(game, InputState {}, [](InputState& i) { i.fire = true; });
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::QuitToDos));
    press_decline(game);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::None));
    CHECK(!game.world().quit_requested);

    press_escape(game);
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::QuitToDos));
    press_enter(game);
    CHECK(game.world().quit_requested);
}

TEST_CASE("Quit confirmation maps the original mouse buttons to cancel and confirm")
{
    Game game;
    game.tick(InputState{});
    open_title_panel(game);
    press_escape(game);
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::QuitToDos));

    InputState cancel {};
    cancel.mouse_secondary = true;
    game.tick(cancel);
    CHECK(game.world().confirmation_prompt == ConfirmationPromptAction::QuitToDos);
    game.tick(InputState{});
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::None));
    CHECK(!game.world().quit_requested);

    press_escape(game);
    finish_action(game);
    InputState confirm {};
    confirm.mouse_primary = true;
    game.tick(confirm);
    CHECK(!game.world().quit_requested);
    game.tick(InputState{});
    CHECK(game.world().quit_requested);
}

TEST_CASE("Pending menu choice is visible before stored configuration is committed")
{
    Game game;
    open_title_panel(game);
    press_up(game);
    press_up(game);
    press_up(game);
    InputState held {};
    held.mouse_middle = true;
    game.tick(held);
    CHECK_EQ(game.world().audio_config_words[3], 3);
    CHECK_EQ(game.world().control_panel_pending_option_row, 0);
    CHECK_EQ(game.world().control_panel_pending_option_value, 0);
    game.tick(InputState {});
    CHECK_EQ(game.world().audio_config_words[3], 0);
    CHECK_EQ(game.world().control_panel_pending_option_row, -1);
}

TEST_CASE("Last menu key replaces earlier input while a mouse release owns the loop")
{
    Game game;
    open_title_panel(game);
    press_up(game);
    press_up(game);
    press_up(game);
    InputState held {};
    held.mouse_secondary = true;
    game.tick(held);
    held.menu_key_action = niteraid::MenuKeyAction::Down;
    game.tick(held);
    held.menu_key_action = niteraid::MenuKeyAction::Ignored;
    game.tick(held);
    game.tick(InputState {});
    game.tick(InputState {});
    CHECK_EQ(game.world().control_panel_row, 0);
}

TEST_CASE("Natural Escape then N cancellation closes the modal")
{
    Game game;
    hold(game, InputState {}, 20);
    hold(game, InputState {.escape = true}, 60);
    hold(game, InputState {}, 20);

    press_escape(game);
    finish_action(game);
    press_decline(game);

    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::None));
    CHECK_EQ(game.world().control_panel_row, 5);
    CHECK(!game.world().quit_requested);
}

TEST_CASE("Natural dirty option cancellation restores the original option row")
{
    Game game;
    game.tick(InputState {});
    open_title_panel(game);
    press_up(game);
    press_up(game);
    CHECK_EQ(game.world().control_panel_row, 1);
    press_escape(game);
    finish_action(game);
    press_decline(game);

    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::None));
    CHECK_EQ(game.world().control_panel_row, 1);
}

TEST_CASE("Escape during gameplay opens gameplay control panel, not DOS quit")
{
    Game game(std::uint16_t {0});
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
    press_escape(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::ControlPanel));
    CHECK(game.world().control_panel_from_gameplay);
    CHECK_EQ(game.world().control_panel_row, 3);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::None));
    CHECK(!game.world().quit_requested);
}

TEST_CASE("faithful control panel ignores Tab until Back to Game is selected")
{
    Game game(std::uint16_t {0});
    press_escape(game);
    press_menu(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::ControlPanel));
    CHECK_EQ(game.world().control_panel_row, 3);

    press_down(game);
    press_enter(game);
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
}

TEST_CASE("faithful gameplay clock freezes through the original pause panel")
{
    Game game(std::uint16_t {0});
    hold(game, InputState {}, 10);
    const auto clock = game.world().frame_tick;
    const auto seed = game.world().gameplay_rng_seed;

    press_escape(game);
    CHECK_EQ(game.world().frame_tick, clock);
    press_menu(game);
    press_down(game);
    CHECK_EQ(game.world().frame_tick, clock);
    CHECK_EQ(game.world().gameplay_rng_seed, seed);

    press_enter(game);
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
    CHECK_EQ(game.world().frame_tick, clock);
    game.tick(InputState {});
    CHECK_EQ(game.world().frame_tick, clock + 1);
}

TEST_CASE("faithful pause leaves subsequent gameplay actors and RNG unchanged")
{
    constexpr std::uint32_t kCapturedWindowEndTick = 141;
    Game paused(std::uint16_t {0});
    Game baseline(std::uint16_t {0});
    hold(paused, InputState {}, 10);
    hold(baseline, InputState {}, 10);
    press_escape(paused);
    press_menu(paused);
    press_down(paused);
    press_enter(paused);
    finish_action(paused);

    while (paused.world().frame_tick < kCapturedWindowEndTick) {
        paused.tick(InputState {});
        baseline.tick(InputState {});
        const auto& actual = paused.world();
        const auto& expected = baseline.world();
        CHECK_EQ(actual.frame_tick, expected.frame_tick);
        CHECK_EQ(actual.gameplay_rng_seed, expected.gameplay_rng_seed);
        CHECK_EQ(actual.scores.score, expected.scores.score);
        CHECK(actual.object_highwater == expected.object_highwater);
        CHECK_EQ(actual.objects.size(), expected.objects.size());
        if (actual.objects.size() != expected.objects.size()) {
            break;
        }
        for (std::size_t index = 0; index < actual.objects.size(); ++index) {
            const auto& left = actual.objects[index];
            const auto& right = expected.objects[index];
            CHECK_EQ(left.active, right.active);
            CHECK(left.type == right.type);
            CHECK_EQ(left.frame, right.frame);
            CHECK_EQ(left.timer, right.timer);
            CHECK_EQ(left.position.x, right.position.x);
            CHECK_EQ(left.position.y, right.position.y);
        }
    }
}

TEST_CASE("Gameplay control panel can resume or abandon to title")
{
    Game game(std::uint16_t {0});
    game.diagnostic_world().scores.score = 12345;
    game.diagnostic_world().scores.enemy_kills = 17;
    game.diagnostic_world().scores.grounded_invader_resolutions = 9;
    const auto level_before_pause = game.world().current_level;

    press_escape(game);
    press_down(game);  // row 4: BACK/continue
    press_enter(game);
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
    CHECK_EQ(game.world().current_level, level_before_pause);
    CHECK_EQ(game.world().scores.score, 12345);
    CHECK_EQ(game.world().scores.enemy_kills, 17);
    CHECK_EQ(game.world().scores.grounded_invader_resolutions, 9);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::None));

    press_escape(game);
    CHECK_EQ(game.world().control_panel_row, 3);
    press_enter(game);
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::ReturnToTitle));
    press_decline(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::ControlPanel));
    CHECK(game.world().control_panel_from_gameplay);
    CHECK_EQ(game.world().scores.score, 12345);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::None));

    press_enter(game);
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().confirmation_prompt),
             static_cast<int>(ConfirmationPromptAction::ReturnToTitle));
    press_enter(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Title));
    CHECK(!game.world().quit_requested);
    CHECK_EQ(game.world().scores.score, 0);
    CHECK_EQ(game.world().scores.enemy_kills, 0);
    CHECK_EQ(game.world().scores.grounded_invader_resolutions, 0);
}

TEST_CASE("Enter on row 3 (START GAME) transitions to Gameplay screen at level 0")
{
    Game game;
    game.tick(InputState{});
    game.diagnostic_world().scores.score = 12345;
    game.diagnostic_world().scores.enemy_kills = 17;
    game.diagnostic_world().scores.grounded_invader_resolutions = 9;
    open_title_panel(game);
    CHECK_EQ(game.world().control_panel_row, 3);
    press_enter(game);
    finish_action(game);
    CHECK_EQ(static_cast<int>(game.world().screen), static_cast<int>(Screen::Gameplay));
    CHECK_EQ(game.world().current_level, 0u);
    CHECK_EQ(game.world().scores.score, 0);
    CHECK_EQ(game.world().scores.enemy_kills, 0);
    CHECK_EQ(game.world().scores.grounded_invader_resolutions, 0);
}

TEST_CASE("Option feedback persists until successful menu navigation")
{
    Game game;
    open_title_panel(game);
    CHECK(!game.world().control_panel_option_feedback);
    press_up(game);
    press_enter(game);
    CHECK(game.world().control_panel_option_feedback);
    hold(game, InputState {}, kLogicalNavigationGapTicks);
    CHECK(game.world().control_panel_option_feedback);
    press_up(game);
    CHECK(!game.world().control_panel_option_feedback);
    press_up(game);
    press_enter(game);
    CHECK(game.world().control_panel_option_feedback);
    press_up(game);
    CHECK(game.world().control_panel_option_feedback);
    press_down(game);
    CHECK(!game.world().control_panel_option_feedback);
}
