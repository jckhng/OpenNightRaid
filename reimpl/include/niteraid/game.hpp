#pragma once

#include "niteraid/game_types.hpp"

#include <filesystem>
#include <optional>

namespace niteraid {

class Game {
public:
    Game();
    explicit Game(std::optional<std::uint16_t> start_level);
    Game(std::optional<std::uint16_t> start_level,
         std::optional<std::uint16_t> finale_budget_override,
         bool fast_shots = false,
         bool female_finale = false,
         bool enhanced_input_debounce = false,
         bool shareware_edition = false,
         std::optional<std::uint32_t> gameplay_seed_override = std::nullopt,
         std::filesystem::path config_path = {});

    void tick(const InputState& input);
    void prepare_gameplay_page();
    [[nodiscard]] const WorldState& world() const;
    [[nodiscard]] const std::optional<WorldState>& before_owner_page() const { return before_owner_page_; }
    [[nodiscard]] WorldState& diagnostic_world();

private:
#if defined(NITERAID_TESTS_BUILD)
    friend struct WreckageProbeAccess;
#endif
    void update_title(const InputState& input);
    void update_credits(const InputState& input);
    void update_attract_interlude(const InputState& input);
    void update_control_panel(const InputState& input);
    void enter_control_panel(bool from_gameplay = false, int initial_row = 3);
    void dispatch_control_panel_action(int row);
    void update_gameplay_wrapper(const InputState& input);
    void update_death_flash();
    void update_intermission(const InputState& input);
    void finish_intermission();
    void update_survivor_intermission_presenter();
    void begin_live_survivor_intermission();
    void move_survivor_controller(WorldPosition target, int sprite);
    void select_survivor_pickup();
    void advance_survivor_phase();
    void update_survivor_controller(Object& object);
    void begin_milestone_intermission(MilestoneIntermission milestone);
    void update_milestone_intermission_audio();
    void update_game_over(const InputState& input);
    void update_high_score_entry(const InputState& input);
    void update_high_scores(const InputState& input);
    void update_finale(const InputState& input);
    void update_finale_presenter_audio();
    void begin_shareware_ending(bool exit_after);
    void update_shareware_ending(const InputState& input);
    void emit_shareware_ending_cues_for_frame(std::uint16_t frame);
    bool update_confirmation_prompt(const InputState& input);
    [[nodiscard]] MenuKeyAction menu_key_action(const InputState& input) const;

    void start_new_game();
    void start_level(std::uint16_t level);
    void advance_level();
    void reset_to_title();
    bool check_and_insert_high_score();
    void load_high_scores();
    bool save_high_scores() const;
    void advance_from_game_over();
    void reset_object_table_and_seed_level_actors();
    void process_player_input_and_modal_state(const InputState& input);
    void update_active_objects();
    void check_object_collisions();
    void check_source_collisions(std::size_t source_index);
    void dispatch_collision_target(std::size_t target_index, const Object& impact);
    void check_debris_collisions(std::size_t debris_index);
    bool resolve_airborne_enemy_hit(Object& object);
    void hit_finale_drop(Object& object, const Object& impact);
    void evaluate_level_progression();
    void handle_level_transition_or_intermission();

    void import_wave_record(std::size_t bank_index, const WaveRecord& record);
    void update_wave_controller(std::size_t bank_index, AircraftVariant variant);
    void update_finale_controller(Object& object);
    void spawn_aircraft(AircraftVariant variant, int direction_flag);
    void spawn_paratrooper_from_aircraft(const Object& aircraft);
    void spawn_paratrooper(const WorldPosition& position,
                           float drift_x,
                           AircraftVariant source_variant = AircraftVariant::D,
                           std::size_t source_bank = 0);
    void spawn_finale_drop(const WorldPosition& position, bool immediate_fall);
    void spawn_enemy_death(const Object& source);
    void convert_aircraft_to_debris(Object& aircraft);
    void spawn_aircraft_fragment(const Object& shell, int index);
    void scatter_aircraft_debris(const Object& shell);
    void spawn_resolution_particles(const Object& source);
    void spawn_resolution_particles(const Object& source, const Object& impact);
    void spawn_bunker_resolution_particles(const Object& source);
    void spawn_finale_controller();
    void spawn_player_projectile();
    std::size_t store_object(Object object);
    void retire_object(std::size_t index);
    void assign_presentation_id(Object& object);
    void add_to_score(int amount);
    void play_sound(std::uint16_t sound_id);
    void stop_sound(std::uint16_t sound_id);
    Object& ensure_presenter_sound_owner();
    void bind_presenter_sound(std::uint16_t sound_id);
    void stop_presenter_sound();
    void interrupt_bunker_assault();
    void trigger_bunker_hit_from_enemy(Object& source);
    void trigger_player_death();
    [[nodiscard]] bool start_or_fire_pressed(const InputState& input) const;
    [[nodiscard]] bool any_keyboard_pressed(const InputState& input) const;
    [[nodiscard]] bool has_airborne_hostiles() const;
    [[nodiscard]] bool object_table_is_clearable() const;
    [[nodiscard]] std::uint32_t next_wave_delay(const RuntimeWaveState& bank);
    [[nodiscard]] const std::array<std::array<WaveRecord, WorldState::kNormalLevelCount>,
                                   WorldState::kWaveBankCount>&
    wave_table() const;

    WorldState world_ {};
    std::filesystem::path config_path_ {};
    // 4e52/4cfb can present twice without another simulation update.
    std::optional<WorldState> before_owner_page_ {};
    std::array<std::array<WaveRecord, WorldState::kNormalLevelCount>,
               WorldState::kWaveBankCount>
        wave_table_ {};
    InputState previous_input_ {};
    int player_aim_accumulator_ = 0x80;
    std::optional<std::uint16_t> finale_budget_override_ {};
    bool enhanced_input_debounce_ = false;
    bool shareware_edition_ = false;
    bool fire_release_required_ = false;
    bool fire_buffered_ = false;
    bool muzzle_flash_pending_draw_ = false;
    float control_panel_mouse_y_ = 0.0f;
    int control_panel_prompt_return_row_ = 5;
    MenuKeyAction control_panel_key_latch_ = MenuKeyAction::None;
    enum class PromptMouseResponse { None, Confirm, Cancel };
    PromptMouseResponse prompt_mouse_response_ = PromptMouseResponse::None;
    std::uint32_t next_presentation_id_ = 1;
    std::optional<std::size_t> reusable_object_slot_ {};
    WorldPosition survivor_target_ {};
    std::size_t survivor_scan_slot_ = 0;
    std::optional<std::size_t> survivor_pickup_slot_ {};
};

}  // namespace niteraid
