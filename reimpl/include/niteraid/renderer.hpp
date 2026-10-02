#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "niteraid/game_types.hpp"
#include "niteraid/shareware_ending.hpp"

namespace niteraid {

class Renderer {
public:
    explicit Renderer(bool debug_hitboxes = false,
                      bool modern_presentation = false,
                      bool crt_filter = false);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] bool is_running() const;
    [[nodiscard]] bool is_interactive() const;
    [[nodiscard]] bool modern_presentation_enabled() const;
    [[nodiscard]] const InputState& input_state() const;
    void render(const WorldState& world,
                float presentation_delta_seconds = 1.0f / kSimulationRateHz) const;
    void present() const;
    void show_faithful_startup_card(const char* capture_path = nullptr);
    void show_faithful_exit_card(bool wait_for_acknowledgement = true,
                                 const char* capture_path = nullptr) const;
    bool save_frame_bmp(const char* path) const;
    void pump_events();

private:
    void render_console(const WorldState& world) const;
    void render_sdl(const WorldState& world, float presentation_delta_seconds) const;
    void open_gamepad(std::int32_t device_id);
    void open_first_gamepad();
    void close_gamepad();
    void refresh_combined_input();
    void update_gamepad_name_repeat(std::uint64_t now_ms);
#if defined(NITERAID_REMAKE)
    void toggle_modern_presentation();

    struct RemakePersistentDebris {
        int sprite_id = -1;
        double x = 0.0;
        int ground_layer = 0;
    };

    struct RemakeDebrisSlot {
        double previous_y = 0.0;
        bool active = false;
    };

    void prepare_remake_frame(const WorldState& world) const;
    void draw_remake_persistent_debris(const WorldState& world) const;
    void composite_remake_frame(float presentation_delta_seconds) const;
#endif

    void* window_ = nullptr;
    void* renderer_ = nullptr;
    void* gamepad_ = nullptr;
#if defined(NITERAID_REMAKE)
    void* remake_scene_texture_ = nullptr;
#endif
    void* title_texture_ = nullptr;
    void* credits_texture_ = nullptr;
    void* attract_interlude_texture_ = nullptr;
    void* gameplay_texture_ = nullptr;
    std::array<void*, 10> hud_digit_textures_ {};
    std::array<int, 10> hud_digit_origin_x_ {};
    std::array<int, 10> hud_digit_origin_y_ {};
    std::vector<std::uint8_t> gameplay_palette_ {};
    void* high_score_texture_ = nullptr;
    void* control_panel_texture_ = nullptr;
    void* control_panel_audio_default_texture_ = nullptr;
    void* control_panel_selector_texture_ = nullptr;
    void* transition_texture_ = nullptr;
    void* shareware_ending_base_texture_ = nullptr;
    std::array<void*, shareware_ending::kFrameCount> shareware_ending_frame_textures_ {};
    void* aircraft_d_texture_ = nullptr;
    void* aircraft_d_reverse_texture_ = nullptr;
    void* aircraft_a_texture_ = nullptr;
    void* aircraft_a_reverse_texture_ = nullptr;
    void* aircraft_b_texture_ = nullptr;
    void* aircraft_b_reverse_texture_ = nullptr;
    void* aircraft_c_texture_ = nullptr;
    void* aircraft_c_reverse_texture_ = nullptr;
    void* player_base_texture_ = nullptr;
    void* projectile_texture_ = nullptr;
    void* paratrooper_body_texture_ = nullptr;
    void* paratrooper_texture_ = nullptr;
    void* grounded_invader_texture_ = nullptr;
    void* landed_invader_texture_ = nullptr;
    std::array<void*, 4> assault_invader_left_textures_ {};
    std::array<void*, 4> assault_invader_right_textures_ {};
    void* muzzle_flash_texture_ = nullptr;
    void* player_fire_overlay_texture_ = nullptr;
    void* player_mount_texture_ = nullptr;
    void* player_pivot_texture_ = nullptr;
    void* player_frozen_aim_texture_ = nullptr;
    std::array<void*, 17> finale_drop_textures_ {};
    std::array<int, 17> finale_drop_origin_x_ {};
    std::array<int, 17> finale_drop_origin_y_ {};
    void* finale_backdrop_texture_ = nullptr;
    std::array<void*, 6> finale_controller_textures_ {};
    std::array<void*, 0x20> survivor_ufo_textures_ {};
    std::array<void*, 0x16> survivor_joke_textures_ {};
    std::array<void*, 8> no_survivor_flyby_textures_ {};
    void* survivor_retained_trooper_texture_ = nullptr;
    void* finale_vehicle_texture_ = nullptr;
    void* finale_vehicle_female_texture_ = nullptr;
    std::array<void*, 6> finale_vehicle_animation_textures_ {};
    std::array<void*, 10> finale_trooper_textures_ {};
    void* finale_particle_texture_ = nullptr;
    std::array<void*, 10> milestone_pizza_textures_ {};
    std::array<void*, 0x2a> milestone_helicopter_textures_ {};
    std::array<void*, 23> credits_overlay_textures_ {};
    std::array<void*, 25> attract_interlude_overlay_textures_ {};
    std::array<void*, 3> bunker_special_effect_textures_ {};
    std::array<void*, 5> terminal_game_over_strip_textures_ {};
    void* terminal_game_over_ground_texture_ = nullptr;
    void* death_flash_ground_texture_ = nullptr;
    std::array<void*, 15> terminal_game_over_flag_textures_ {};
    void* terminal_game_over_banner_texture_ = nullptr;
    std::array<void*, 3> overrun_assault_residue_textures_ {};
    void* level_entry_residue_texture_ = nullptr;
    std::array<void*, 0x1f> player_aim_textures_ {};
    std::array<void*, 10> paratrooper_deploy_textures_ {};
    std::array<void*, 9> paratrooper_frame_textures_ {};
    std::array<void*, 4> paratrooper_no_chute_textures_ {};
    std::array<void*, 13> paratrooper_landing_textures_ {};
    std::array<void*, 10> aircraft_d_rotor_textures_ {};
    std::array<void*, 10> aircraft_a_rotor_textures_ {};
    std::array<int, 10> aircraft_a_rotor_origin_x_ {};
    std::array<int, 10> aircraft_a_rotor_origin_y_ {};
    std::array<void*, 0x1d> aircraft_c_rotor_textures_ {};
    std::array<void*, 0x41> aircraft_b_rotor_textures_ {};
    std::array<void*, 8> smart_bomb_armed_textures_ {};
    std::array<int, 8> smart_bomb_armed_origin_x_ {{0, 0, 2, 4, 0, 0, 0, 0}};
    std::array<int, 8> smart_bomb_armed_origin_y_ {};
    std::array<void*, 10> smart_bomb_fall_textures_ {};
    std::array<int, 10> smart_bomb_fall_origin_x_ {{3, 4, 4, 7, 10, 3, 4, 6, 8, 10}};
    std::array<int, 10> smart_bomb_fall_origin_y_ {{10, 8, 6, 4, 2, 10, 8, 6, 4, 3}};
    std::array<void*, 9> aircraft_shell_textures_ {};
    std::array<int, 9> aircraft_shell_origin_x_ {};
    std::array<int, 9> aircraft_shell_origin_y_ {};
    std::array<void*, 6> enemy_death_textures_ {};
    std::array<int, 6> enemy_death_origin_x_ {};
    std::array<int, 6> enemy_death_origin_y_ {};
    std::array<void*, 0x3a> aircraft_fragment_textures_ {};
    std::array<int, 0x3a> aircraft_fragment_origin_x_ {};
    std::array<int, 0x3a> aircraft_fragment_origin_y_ {};
    std::array<void*, 0x26> control_panel_glyph_textures_ {};
    std::array<void*, 0x36> control_panel_widget_textures_ {};
    std::array<void*, 256> font_textures_ {};
    std::array<void*, 256> font_mask_textures_ {};
    bool running_ = true;
    bool announced_console_mode_ = false;
    bool debug_hitboxes_ = false;
    bool modern_presentation_ = false;
    bool crt_filter_ = false;
    InputState input_ {};
    bool keyboard_left_ = false;
    bool keyboard_right_ = false;
    bool keyboard_up_ = false;
    bool keyboard_down_ = false;
    bool keyboard_fire_ = false;
    bool keyboard_start_ = false;
    bool mouse_fire_ = false;
    bool gamepad_dpad_left_ = false;
    bool gamepad_dpad_right_ = false;
    bool gamepad_dpad_up_ = false;
    bool gamepad_dpad_down_ = false;
    bool gamepad_fire_button_ = false;
    bool gamepad_start_button_ = false;
    std::int16_t gamepad_axis_x_ = 0;
    std::int16_t gamepad_axis_y_ = 0;
    std::int16_t gamepad_right_trigger_ = 0;
    int gamepad_name_repeat_direction_ = 0;
    std::uint64_t gamepad_name_repeat_at_ms_ = 0;
    mutable std::uint32_t last_console_frame_ = 0;
#if defined(NITERAID_REMAKE)
    mutable WorldPosition remake_muzzle_afterglow_position_ {};
    mutable std::uint32_t remake_muzzle_source_tick_ = 0;
    mutable float remake_muzzle_afterglow_seconds_ = 0.0f;
    mutable bool remake_muzzle_source_seen_ = false;
    mutable std::vector<RemakePersistentDebris> remake_persistent_debris_ {};
    mutable std::vector<RemakeDebrisSlot> remake_debris_slots_ {};
    mutable std::uint16_t remake_debris_level_ = 0;
    mutable std::uint32_t remake_previous_world_tick_ = 0;
    mutable std::uint32_t remake_shake_source_tick_ = 0;
    mutable std::uint32_t remake_presentation_serial_ = 0;
    mutable float remake_shake_seconds_ = 0.0f;
    mutable int remake_previous_death_flash_frames_ = 0;
    mutable bool remake_level_seen_ = false;
    mutable bool remake_shake_source_seen_ = false;
#endif
};

}  // namespace niteraid
