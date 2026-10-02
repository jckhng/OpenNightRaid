// See tools/compare/dat2730_schema.py for the field layout we mirror
// here. Anything you add there must also be written here (and vice
// versa) — the Tier 3 diff tool keys off matching field names.

#include "niteraid/state_serializer.hpp"

#include <array>
#include <algorithm>
#include <cstdint>

namespace niteraid {

namespace {

void write_u16(std::vector<std::uint8_t>& buf, std::size_t offset, std::uint16_t value)
{
    buf.at(offset)     = static_cast<std::uint8_t>(value & 0xff);
    buf.at(offset + 1) = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

void write_u32(std::vector<std::uint8_t>& buf, std::size_t offset, std::uint32_t value)
{
    buf.at(offset)     = static_cast<std::uint8_t>(value & 0xff);
    buf.at(offset + 1) = static_cast<std::uint8_t>((value >> 8) & 0xff);
    buf.at(offset + 2) = static_cast<std::uint8_t>((value >> 16) & 0xff);
    buf.at(offset + 3) = static_cast<std::uint8_t>((value >> 24) & 0xff);
}

constexpr std::array<std::size_t, 5> kBankBases {
    0x4d3e, 0x4d58, 0x4d72, 0x4d8c, 0x4da6,
};

// Mirror our GameplayState enum onto the original DAT_2730_4dc4 values
// (Active=0, ScriptedSequence=1, Transition=2, GameOver=3,
// LevelComplete=4, FinaleComplete=5).
std::uint16_t gameplay_state_to_word(GameplayState state)
{
    return static_cast<std::uint16_t>(state);
}

}  // namespace

std::vector<std::uint8_t> serialize_world_state_dat2730(const WorldState& world)
{
    std::vector<std::uint8_t> buf(kStateDumpSize, 0u);

    // HUD counters
    const auto score_u = static_cast<std::uint32_t>(
        world.scores.score < 0 ? 0 : world.scores.score);
    write_u32(buf, 0x2056, score_u);
    write_u16(buf, 0x205a, world.scores.grounded_invader_resolutions);
    write_u16(buf, 0x205c, world.scores.enemy_kills);
    // 5008 advances 205e before 4e52; native retains the completed level for art.
    write_u16(buf, 0x205e, static_cast<std::uint16_t>(
        world.current_level + (world.screen == Screen::Intermission ? 1 : 0)));

    const bool bunker_assault_walk_active = std::any_of(
        world.objects.begin(), world.objects.end(), [](const Object& o) {
            return o.active && !o.pending_destroy && o.type == ObjectType::LandedInvader &&
                   o.assaulting;
        });

    // Top-level state machine
    const auto serialized_gameplay_state =
        bunker_assault_walk_active && world.gameplay_state == GameplayState::ScriptedSequence
            ? GameplayState::Active
            : world.gameplay_state;
    write_u16(buf, 0x4dc4, gameplay_state_to_word(serialized_gameplay_state));
    write_u32(buf, 0x4dc6, world.frame_tick);
    write_u32(buf, 0x1ee8, world.gameplay_rng_seed);
    write_u16(buf, 0x4dec, world.transition_freeze ? 1 : 0);
    write_u16(buf, 0x4de8, world.waves_exhausted ? 1 : 0);
    write_u16(buf, 0x4de4, world.fast_shots_enabled ? 1 : 0);
    write_u16(buf,
              0x4dea,
              (world.transition_armed ||
               (bunker_assault_walk_active && world.bunker_assault_entries == 0))
                  ? 1
                  : 0);

    // landed_invader_count — count active LandedInvader objects.
    std::uint16_t landed_count = 0;
    for (const auto& o : world.objects) {
        if (o.active && !o.pending_destroy && o.type == ObjectType::LandedInvader) {
            ++landed_count;
        }
    }
    write_u16(buf, 0x4dee, landed_count);

    write_u16(buf, 0x4df0, world.current_level < WorldState::kFinalGameplayLevel
                              ? world.finale_budget_seed : world.finale_spawn_budget_remaining);
    write_u16(buf, 0x4df2, world.finale_spawn_budget_total);

    // Control-panel state used by the menu state-machine capture suite.
    write_u16(buf, 0x1ff2, world.control_panel_from_gameplay ? 1 : 0);
    write_u16(
        buf,
        0x1ff4,
        world.confirmation_prompt == ConfirmationPromptAction::None ? 0 : 1);
    const auto prompt_string_id =
        world.confirmation_prompt == ConfirmationPromptAction::ReturnToTitle
            ? 0x0643
            : world.confirmation_prompt == ConfirmationPromptAction::QuitToDos
                  ? 0x065b
                  : 0;
    write_u16(buf, 0x2002, prompt_string_id);
    write_u16(buf, 0x2006, static_cast<std::uint16_t>(world.control_panel_row));

    // Live detection is retained separately from CONFIG.NTR preferences.
    write_u16(buf, 0xdea4, world.audio_config_words[config_internals::kOplHardware]);
    write_u16(buf, 0xdea2, world.audio_config_words[config_internals::kSourceHardware]);
    write_u16(buf, 0xdea6, world.audio_config_words[config_internals::kBlasterHardware]);
    write_u16(buf, 0xe1d0, world.audio_config_words[config_internals::kMouseHardware]);
    write_u16(buf, 0x1ff8, world.audio_config_words[3]);
    write_u16(buf, 0x1ffa, world.audio_config_words[4]);
    write_u16(buf, 0x1ffc, world.audio_config_words[5]);
    write_u16(buf, 0x1ffe, world.audio_config_words[7]);

    // Five wave-controller runtime blocks
    for (std::size_t bank = 0; bank < kBankBases.size(); ++bank) {
        const auto& rt = world.wave_banks[bank];
        const auto base = kBankBases[bank];
        write_u32(buf, base + 0x00, rt.start_tick);
        write_u32(buf, base + 0x04, rt.stop_tick);
        write_u16(buf, base + 0x08, rt.remaining_spawns);
        write_u16(buf, base + 0x0a, rt.min_delay);
        write_u16(buf, base + 0x0c, rt.max_delay);
        write_u16(buf, base + 0x0e, rt.paratrooper_cadence);
        write_u16(buf, base + 0x10, rt.import_flag ? 1 : 0);
        write_u32(buf, base + 0x12, rt.next_trigger_tick);
        write_u32(buf, base + 0x16, rt.last_paratrooper_tick);
    }

    return buf;
}

}  // namespace niteraid
