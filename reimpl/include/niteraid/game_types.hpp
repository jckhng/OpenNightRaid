#pragma once

#include "niteraid/original_random.hpp"
#include "niteraid/config_internals.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace niteraid {

inline constexpr std::uint32_t kSimulationRateHz = 70;
inline constexpr std::uint32_t kLegacyPresentationRateHz = 60;
inline constexpr std::uint32_t kEnhancedPresentationRateHz = 120;

inline constexpr std::uint32_t original_timer_tick_at_simulation_frame(
    std::uint32_t frame)
{
    return frame * 70u / kSimulationRateHz;
}

inline constexpr int legacy_video_frames_to_simulation_frames(std::uint32_t frames)
{
    return static_cast<int>((frames * kSimulationRateHz +
                             kLegacyPresentationRateHz / 2u) /
                            kLegacyPresentationRateHz);
}

enum class Screen {
    Title,
    Credits,
    AttractInterlude,
    ControlPanel,
    Gameplay,
    Intermission,
    GameOver,
    HighScoreEntry,
    HighScores,
    Finale,
    SharewareEnding,
};

inline constexpr int original_timer_ticks_to_video_frames(std::uint32_t ticks)
{
    // DAT_2730_deae/deb0 is the game's paced timer. Runtime captures align it
    // to roughly 70 Hz, not the raw BIOS 18.2 Hz tick.
    return static_cast<int>((ticks * kSimulationRateHz + 35u) / 70u);
}

inline constexpr int kAttractOverlayDelayFrames = original_timer_ticks_to_video_frames(4);
inline constexpr int kAttractInputWaitFrames = original_timer_ticks_to_video_frames(0x1a4);
inline constexpr int kCreditsOverlayFrameCount = 23;
inline constexpr int kAttractInterludeOverlayFrameCount = 25;
inline constexpr int kTitleScreenFrames = kAttractInputWaitFrames;
inline constexpr int kCreditsScreenFrames =
    kCreditsOverlayFrameCount * kAttractOverlayDelayFrames + kAttractInputWaitFrames;
inline constexpr int kAttractInterludeScreenFrames =
    kAttractInterludeOverlayFrameCount * kAttractOverlayDelayFrames + kAttractInputWaitFrames;
inline constexpr int kHighScoreAttractScreenFrames = kAttractInputWaitFrames;
inline constexpr int kScreenFadeFrames = original_timer_ticks_to_video_frames(0x18);

enum class GameplayState : std::uint8_t {
    Active = 0,
    ScriptedSequence = 1,
    Transition = 2,
    GameOver = 3,
    LevelComplete = 4,
    FinaleComplete = 5,
};

enum class MilestoneIntermission : std::uint8_t {
    None = 0,
    Level4Pizza,
    Level8Helicopter,
};

enum class ConfirmationPromptAction : std::uint8_t {
    None = 0,
    ReturnToTitle,
    QuitToDos,
};

enum class ObjectType : std::uint8_t {
    None = 0,
    PlayerCannon = 2,
    WaveController = 3,
    PlayerProjectile = 4,
    Aircraft = 5,
    EnemyDeath = 6,
    AircraftDebris = 7,
    SmartBomb = 8,
    Paratrooper = 9,
    GroundedTransition = 10,
    LandedInvader = 11,
    ResolutionParticle = 12,
    FinaleController = 13,
    Presenter = 14,
};

enum class AircraftVariant : std::uint8_t {
    D,
    A,
    B,
    C,
};

enum class MenuKeyAction : std::uint8_t {
    None,
    Up,
    Down,
    Enter,
    Space,
    Escape,
    Confirm,
    Decline,
    Ignored,
};

struct InputState {
    bool move_left = false;
    bool move_right = false;
    bool move_up = false;
    bool move_down = false;
    bool fire = false;
    bool start = false;
    bool start_repeat = false;
    bool menu_navigation_up_repeat = false;
    bool menu_navigation_down_repeat = false;
    bool backspace = false;
    bool name_delete = false;
    bool name_submit = false;
    bool accept = false;
    bool menu = false;
    bool escape = false;
    bool decline = false;
    bool mouse_primary = false;
    bool mouse_secondary = false;
    bool mouse_middle = false;
    bool mouse_moved = false;
    MenuKeyAction menu_key_action = MenuKeyAction::None;
    bool control_modifier = false;
    bool alt_modifier = false;
    bool gamepad_accept = false;
    bool gamepad_back = false;
    bool gamepad_start = false;
    bool gamepad_name_up = false;
    bool gamepad_name_down = false;
    int level_warp_digit = -1;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    std::string text_input {};
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Preserve every original 16.16 coordinate bit, including positions past 256.
struct WorldPosition {
    double x = 0.0;
    double y = 0.0;

    constexpr WorldPosition() = default;
    constexpr WorldPosition(double x_value, double y_value) : x(x_value), y(y_value) {}
    constexpr WorldPosition(Vec2 value) : x(value.x), y(value.y) {}
};

struct WaveRecord {
    std::uint32_t start_tick = 0;
    std::uint32_t stop_tick = 0xffffffffu;
    std::uint16_t spawn_count = 0;
    std::uint16_t min_delay = 0;
    std::uint16_t max_delay = 0;
    std::uint16_t paratrooper_cadence = 0;
};

struct RuntimeWaveState {
    std::uint32_t start_tick = 0;
    std::uint32_t stop_tick = 0xffffffffu;
    std::uint16_t remaining_spawns = 0;
    std::uint16_t min_delay = 0;
    std::uint16_t max_delay = 0;
    std::uint16_t paratrooper_cadence = 0;
    bool import_flag = false;
    std::uint32_t next_trigger_tick = 0;
    std::uint32_t last_paratrooper_tick = 0;
    std::uint32_t live_next_trigger_tick = 0;
};

struct ScoreState {
    std::int32_t score = 0;
    std::uint16_t grounded_invader_resolutions = 0;
    std::uint16_t enemy_kills = 0;
};

struct HighScoreEntry {
    std::string name {};
    std::int32_t score = 100;
    std::uint16_t stat_a = 0;
    std::uint16_t stat_b = 0;
    std::uint16_t stat_c = 0;
    bool highlighted = false;
};

struct Object {
    // Enhanced presentation identity. This is not original game state and is
    // deliberately excluded from Tier 3 serialization.
    std::uint32_t presentation_id = 0;
    ObjectType type = ObjectType::None;
    AircraftVariant aircraft_variant = AircraftVariant::D;
    std::size_t wave_bank = 0;
    WorldPosition position {};
    Vec2 velocity {};
    Vec2 extent {4.0f, 4.0f};
    int direction = 1;
    int frame = 0;
    int presented_rotor_frame = -1;
    int sprite_id = -1;
    int assault_stage = 0;
    std::uint32_t timer = 0;
    // Original object records store their currently bound sustained sound at
    // offset +6. Deleting or rebinding the owner stops this exact sound.
    std::uint16_t sound_id = 0;
    std::uint32_t last_drop_tick = 0;
    bool has_dropped_payload = false;
    bool finale_drop = false;
    // Original finale-drop object +0x24, post-decremented by collision 1da3.
    std::uint16_t finale_hits_remaining = 0;
    bool parachute_lost = false;
    bool assaulting = false;
    bool active = false;
    bool pending_destroy = false;
};

struct RetainedPage {
    std::vector<Object> objects;
    std::uint32_t gameplay_tick = 0;
};

enum class SurvivorPhase { Ingress, PickupMove, Common, Rare, Aftermath, Egress };

struct SurvivorLiveState {
    SurvivorPhase phase = SurvivorPhase::Ingress;
    std::size_t controller_slot = 0;
    int leg = 0;
    bool animation_complete = false;
    bool owner_return_ready = false;
    int beam_variant = 0;
};

struct WorldState {
    static constexpr std::size_t kWaveBankCount = 5;
    static constexpr std::uint16_t kNormalLevelCount = 12;
    static constexpr std::uint16_t kFinalGameplayLevel = 0x0c;

    Screen screen = Screen::Title;
    GameplayState gameplay_state = GameplayState::Active;
    std::uint32_t frame_tick = 0;
    float presentation_alpha = 1.0f;
    std::uint16_t current_level = 0;
    bool waves_exhausted = false;
    bool player_dead = false;
    bool transition_armed = false;
    bool transition_freeze = false;
    bool bunker_overrun_pending = false;
    bool bunker_assault_active = false;
    bool overrun_interrupt_residue = false;
    bool survivor_intermission_active = false;
    bool no_survivor_intermission_active = false;
    std::uint16_t survivor_intermission_failure_mask = 0;
    std::uint32_t survivor_intermission_frame = 0;
    std::uint32_t survivor_intermission_timer_origin = 0;
    std::uint32_t survivor_intermission_rng_seed = 1;
    bool survivor_native_presenter = false;
    std::optional<SurvivorLiveState> survivor_live {};
    std::vector<std::uint8_t> survivor_presenter_update_batches {};
    MilestoneIntermission milestone_intermission = MilestoneIntermission::None;
    std::uint32_t milestone_intermission_frame = 0;
    std::uint32_t milestone_timer_origin = 0;
    bool pizza_all_draws_diagnostic = false;
    std::uint32_t finale_presenter_frame = 0;
    std::uint32_t finale_presenter_timer_origin = 0;
    std::vector<std::uint32_t> finale_hardware_tick_schedule {};
    std::uint32_t finale_particle_rng_seed = 0xff9652c1u;
    std::uint16_t finale_particle_sounds_spawned = 0;
    std::uint16_t finale_particle_sounds_burst = 0;
    std::uint32_t shareware_ending_tick = 0;
    std::uint16_t shareware_ending_frame = 0;
    std::uint16_t shareware_ending_audio_cue = 0;
    bool shareware_ending_waiting_for_input = false;
    bool shareware_ending_exit_after = false;
    int title_frames_remaining = kTitleScreenFrames;
    int credits_frames_remaining = kCreditsScreenFrames;
    int attract_interlude_frames_remaining = kAttractInterludeScreenFrames;
    int intermission_frames_remaining = 90;
    int game_over_frames_remaining = 120;
    int high_score_frames_remaining = kHighScoreAttractScreenFrames;
    int screen_fade_frames_remaining = kScreenFadeFrames;
    bool credits_seen = false;
    bool high_score_checked = false;
    int active_high_score_index = -1;
    char high_score_gamepad_character = 'A';
    bool high_score_gamepad_active = false;
    int level_banner_frames_remaining = 0;
    int muzzle_flash_frames_remaining = 0;
    int bunker_special_effect_frames_remaining = 0;
    int bunker_door_opening_frames_remaining = 0;
    bool bunker_native_choreography = false;
    bool bunker_assault_direct_draw = false;
    bool bunker_assault_cleanup_pending = false;
    // Explicit diagnostic boundary import, not the live shared gameplay RNG.
    std::optional<std::uint32_t> overrun_cleanup_rng_seed {};
    std::uint32_t gameplay_rng_seed = original_palette_random_seed();
    int bunker_terminal_explosion_frames_total = 0;
    WorldPosition muzzle_flash_position {};
    std::uint32_t transition_started_tick = 0;
    std::uint32_t bunker_assault_step_tick = 0;
    int bunker_explosion_frames_remaining = 0;
    int death_flash_frames_remaining = 0;
    bool death_palette_restored = false;
    // Render-only scene snapshot; these actors no longer own gameplay/audio.
    std::optional<RetainedPage> fatal_retained_page {};
    std::optional<RetainedPage> overrun_retained_page {};
    std::uint16_t finale_budget_seed = 0;
    std::uint16_t finale_spawn_budget_remaining = 0;
    std::uint16_t finale_spawn_budget_total = 0;
    std::uint16_t bunker_assault_entries = 0;
    bool bunker_white_flag = false;
    bool bunker_walk_sound_toggle = false;
    int control_panel_row = 0;
    bool control_panel_dirty = false;
    bool control_panel_from_gameplay = false;
    bool control_panel_initial_hint = false;
    bool control_panel_option_feedback = false;
    int control_panel_pending_option_row = -1;
    std::uint16_t control_panel_pending_option_value = 0;
    int control_panel_action_frames_remaining = 0;
    int control_panel_pending_action_row = -1;
    ConfirmationPromptAction confirmation_prompt = ConfirmationPromptAction::None;
    bool quit_requested = false;
    bool game_over_input_latched = false;
    bool fast_shots_enabled = false;
    bool female_finale_enabled = false;
    // Layout matches SaveConfigAndHighScores (1480:03df):
    //   [0]=DAT_2730_dea4 (hw)   [1]=DAT_2730_dea2 (hw)   [2]=DAT_2730_dea6 (hw)
    //   [3]=DAT_2730_1ff8 (sound mode, row 0)
    //   [4]=DAT_2730_1ffa (voice count, row 1)
    //   [5]=DAT_2730_1ffc (music toggle, row 2)
    //   [6]=DAT_2730_e1d0 (mouse detect)
    //   [7]=DAT_2730_1ffe (mouse-gated preference)
    std::array<std::uint16_t, 8> audio_config_words = config_internals::kDefaultWords;
    ScoreState scores {};
    std::array<HighScoreEntry, 6> high_scores {};
    std::array<RuntimeWaveState, kWaveBankCount> wave_banks {};
    std::vector<Object> objects {};
    // DOS high-water can fall below retained inactive records in objects.
    std::optional<std::size_t> object_highwater {};
    std::vector<std::uint16_t> sound_events {};
    std::vector<std::uint16_t> sound_stop_events {};
    bool reset_sound_effects = false;
};

std::string to_string(Screen screen);
std::string to_string(GameplayState state);
std::string to_string(ObjectType type);
std::string to_string(AircraftVariant variant);
std::string_view aircraft_bucket(AircraftVariant variant);

}  // namespace niteraid
