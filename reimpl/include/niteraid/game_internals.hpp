#pragma once

// Pure helpers used by Game and exposed for unit tests so test code can
// assert decompile-derived constants without going through the full
// Game tick loop. Anything that lives here must be a stateless function
// of its arguments (or a constexpr table).

#include "niteraid/game_types.hpp"
#include "niteraid/finale_particles.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace niteraid::internals {

inline constexpr int kAimFrameMin = 6;
inline constexpr int kAimFrameMax = 0x79;
inline constexpr int kAircraftARotorFrames = 4;
inline constexpr int kAircraftDRotorFrames = 5;
inline constexpr int kBannerFlybyAircraftSprite = 0x264;

constexpr int original_gameplay_draw_layer(const Object& object)
{
    switch (object.type) {
    case ObjectType::Aircraft:
    case ObjectType::SmartBomb:
        return 1;
    case ObjectType::Paratrooper:
    case ObjectType::GroundedTransition:
    case ObjectType::LandedInvader:
    case ObjectType::ResolutionParticle:
        return 2;
    case ObjectType::PlayerProjectile:
        return 3;
    case ObjectType::EnemyDeath:
        return 4;
    case ObjectType::AircraftDebris:
        // 0bf2 keeps the converted aircraft's layer; 08ea children use layer 2.
        return object.has_dropped_payload ? 2 : 1;
    default:
        return 0;
    }
}

constexpr int aircraft_rotor_frame(const Object& object, std::uint32_t simulation_tick)
{
    switch (object.aircraft_variant) {
    case AircraftVariant::A:
        if (object.presented_rotor_frame >= 0) {
            return object.presented_rotor_frame;
        }
        // 0da2/0e01 draw the old phase before incrementing +0x22.
        return (object.frame + kAircraftARotorFrames - 1) % kAircraftARotorFrames;
    case AircraftVariant::D:
        return object.frame;
    case AircraftVariant::B:
        return static_cast<int>(simulation_tick % 0x41u);
    case AircraftVariant::C:
        return static_cast<int>(simulation_tick % 0x1du);
    }
    return 0;
}

constexpr int finale_drop_sprite_id(const Object& object)
{
    if (object.assault_stage == 0) return 0x278;
    // 2730:07c4 selects one four-frame family per remaining-hit count.
    return 0x268 + (3 - std::min<int>(object.finale_hits_remaining, 3)) * 4 +
           std::clamp(object.frame, 0, 3);
}

struct ResolutionParticleSetup {
    std::uint8_t color;
    std::int32_t vx;
    std::int32_t vy;
    int lifetime;
};

constexpr bool original_motion_outside_bounds(const WorldPosition& position)
{
    return position.x <= -100.0f || position.y <= -40.0f ||
           position.x >= 320.0f || position.y >= 200.0f;
}

// 026e indexes 2594:0000, then 01e0 draws velocity X/Y and lifetime.
// Inputs are four successive 15-bit random values, not palette indices.
constexpr ResolutionParticleSetup resolution_particle_setup(
    const std::array<std::uint16_t, 4>& random)
{
    constexpr std::array<std::uint8_t, 12> colors {
        0x0c, 0x0d, 0x0f, 0x35, 0xfa, 0xe8, 0x22, 0x29, 0x6c, 0x6c, 0x6c, 0xa1};
    const auto velocity = [](std::uint16_t value) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) * 65536u / 32767u) - 32768;
    };
    return {colors[static_cast<std::size_t>(random[0]) * colors.size() / 32768u],
            velocity(random[1]), velocity(random[2]), 32 + random[3] * 8 / 32768};
}

// Compiled blitters 0x111..0x114 write at these offsets from the actor anchor.
inline constexpr std::array<int, 4> kNoChuteFrameOriginX {{1, 0, 0, 0}};
inline constexpr std::array<int, 4> kNoChuteFrameOriginY {{0, 1, 1, 1}};

constexpr bool bunker_assault_actor_draws_on_world_layer(const Object& object)
{
    return !object.assaulting || object.assault_stage == 0 || object.assault_stage == 3;
}

constexpr bool bunker_assault_actor_draws_in_doorway(const Object& object)
{
    return object.assaulting && (object.assault_stage == 1 || object.assault_stage == 2);
}

constexpr int bunker_door_sprite_index(const WorldState& world)
{
    if (world.bunker_special_effect_frames_remaining > 0) {
        return std::clamp(2 - (15 - world.bunker_special_effect_frames_remaining) / 5, 0, 2);
    }
    if (world.bunker_door_opening_frames_remaining > 0) {
        // Creation follows the outer-walk draw; the first door draw is next update.
        if (world.bunker_door_opening_frames_remaining == 15) return -1;
        return std::clamp((15 - world.bunker_door_opening_frames_remaining) / 5, 0, 2);
    }
    return world.bunker_special_effect_frames_remaining < 0 ? 2 : -1;
}

inline std::string finalized_high_score_name(std::string name, bool fast_shots_enabled)
{
    if (fast_shots_enabled) {
        return "I cheated!";
    }
    if (name.empty()) {
        return "Unknown Soldier";
    }
    return name;
}

inline constexpr std::size_t kHighScoreNameCapacity = 0x3f;
inline constexpr int kHighScoreNameMaxWidth = 0x69;
inline constexpr std::string_view kGamepadHighScoreCharacters =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789.-!?";

inline char cycle_gamepad_high_score_character(char current, int direction)
{
    auto position = kGamepadHighScoreCharacters.find(current);
    if (position == std::string_view::npos) {
        position = 0;
    }
    const auto count = static_cast<int>(kGamepadHighScoreCharacters.size());
    const auto next = (static_cast<int>(position) + direction % count + count) % count;
    return kGamepadHighScoreCharacters[static_cast<std::size_t>(next)];
}

// Live DAT_2730_2428 values after SetFontSpriteBase(0x9e). RunHighScoreNameEntry
// uses this table to reject a tentative append when the rendered name exceeds
// 105 pixels.
inline constexpr std::array<std::uint8_t, 95> kHighScoreGlyphAdvances {{
    5, 2, 4, 7, 6, 6, 7, 2, 4, 4, 6, 6, 3, 5, 2, 5,
    6, 3, 6, 6, 7, 6, 7, 7, 7, 7, 2, 3, 5, 5, 5, 7,
    7, 7, 7, 6, 7, 6, 6, 7, 7, 2, 7, 6, 6, 8, 7, 7,
    7, 8, 7, 6, 6, 7, 7, 8, 6, 6, 6, 4, 5, 4, 6, 6,
    4, 7, 7, 6, 7, 6, 6, 7, 7, 2, 7, 6, 6, 8, 7, 7,
    7, 8, 7, 6, 6, 7, 7, 8, 6, 6, 6, 6, 2, 6, 6,
}};

inline int high_score_name_width(std::string_view name)
{
    int width = 0;
    for (const unsigned char c : name) {
        if (c >= 0x20 && c <= 0x7e) {
            width += kHighScoreGlyphAdvances[c - 0x20];
        }
    }
    return width;
}

inline bool append_high_score_name_character(std::string& name, char character)
{
    const auto c = static_cast<unsigned char>(character);
    if (c <= 0x1f || c > 0x7e || c == ',' ||
        name.size() >= kHighScoreNameCapacity) {
        return false;
    }

    name.push_back(character);
    if (high_score_name_width(name) <= kHighScoreNameMaxWidth) {
        return true;
    }
    name.pop_back();
    return false;
}

struct CollisionBounds {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

inline constexpr std::array<Vec2, 127> kProjectileVelocityTable {{
    Vec2 {-1.00000000f, 0.00000000f},
    Vec2 {-0.99929810f, -2431.0f / 65536.0f},
    Vec2 {-0.99807739f, -4050.0f / 65536.0f},
    Vec2 {-0.99624634f, -0.08645630f},
    Vec2 {-0.99380493f, -7280.0f / 65536.0f},
    Vec2 {-0.99075317f, -0.13562012f},
    Vec2 {-0.98709106f, -0.16009521f},
    Vec2 {-0.98283386f, -0.18446350f},
    Vec2 {-0.97796631f, -0.20870972f},
    Vec2 {-0.97250366f, -0.23283386f},
    Vec2 {-0.96644592f, -0.25682068f},
    Vec2 {-0.95980835f, -0.28063965f},
    Vec2 {-0.95256042f, -0.30430603f},
    Vec2 {-0.94474792f, -0.32777405f},
    Vec2 {-0.93635559f, -0.35104370f},
    Vec2 {-0.92738342f, -0.37408447f},
    Vec2 {-0.91784668f, -0.39691162f},
    Vec2 {-0.90774536f, -0.41949463f},
    Vec2 {-0.89709473f, -0.44181824f},
    Vec2 {-0.88589478f, -0.46386719f},
    Vec2 {-0.87414551f, -0.48564148f},
    Vec2 {-0.86186218f, -0.50711060f},
    Vec2 {-0.84906006f, -0.52827454f},
    Vec2 {-0.83573914f, -0.54911804f},
    Vec2 {-0.82189941f, -0.56962585f},
    Vec2 {-0.80755615f, -0.58978271f},
    Vec2 {-0.79272461f, -0.60957336f},
    Vec2 {-0.77740479f, -0.62899780f},
    Vec2 {-0.76159668f, -0.64802551f},
    Vec2 {-0.74534607f, -0.66667175f},
    Vec2 {-0.72862244f, -0.68490601f},
    Vec2 {-0.71145630f, -0.70271301f},
    Vec2 {-0.69386292f, -0.72009277f},
    Vec2 {-0.67584229f, -0.73703003f},
    Vec2 {-0.65739441f, -0.75352478f},
    Vec2 {-0.63856506f, -0.76956177f},
    Vec2 {-0.61932373f, -0.78511047f},
    Vec2 {-0.59971619f, -0.80020142f},
    Vec2 {-0.57974243f, -0.81478882f},
    Vec2 {-0.55941772f, -0.82887268f},
    Vec2 {-0.53874207f, -0.84245300f},
    Vec2 {-0.51773071f, -0.85552979f},
    Vec2 {-0.49641418f, -0.86807251f},
    Vec2 {-0.47479248f, -0.88008118f},
    Vec2 {-0.45288086f, -0.89155579f},
    Vec2 {-0.43069458f, -0.90249634f},
    Vec2 {-0.40823364f, -0.91287231f},
    Vec2 {-0.38552856f, -0.92268372f},
    Vec2 {-0.36259460f, -0.93193054f},
    Vec2 {-0.33943176f, -0.94062805f},
    Vec2 {-0.31605530f, -0.94873047f},
    Vec2 {-0.29249573f, -0.95625305f},
    Vec2 {-0.26875305f, -0.96319580f},
    Vec2 {-0.24484253f, -0.96955872f},
    Vec2 {-0.22079468f, -0.97531128f},
    Vec2 {-0.19659424f, -0.98046875f},
    Vec2 {-0.17228699f, -0.98503113f},
    Vec2 {-0.14787292f, -0.98899841f},
    Vec2 {-0.12336731f, -0.99235535f},
    Vec2 {-0.09878540f, -0.99510193f},
    Vec2 {-4859.0f / 65536.0f, -0.99723816f},
    Vec2 {-3240.0f / 65536.0f, -0.99876404f},
    Vec2 {-1620.0f / 65536.0f, -0.99967957f},
    Vec2 {0.00000000f, -1.00000000f},
    Vec2 {1620.0f / 65536.0f, -0.99967957f},
    Vec2 {3240.0f / 65536.0f, -0.99876404f},
    Vec2 {4859.0f / 65536.0f, -0.99723816f},
    Vec2 {0.09878540f, -0.99510193f},
    Vec2 {0.12336731f, -0.99235535f},
    Vec2 {0.14787292f, -0.98899841f},
    Vec2 {0.17228699f, -0.98503113f},
    Vec2 {0.19659424f, -0.98046875f},
    Vec2 {0.22079468f, -0.97531128f},
    Vec2 {0.24484253f, -0.96955872f},
    Vec2 {0.26875305f, -0.96319580f},
    Vec2 {0.29249573f, -0.95625305f},
    Vec2 {0.31605530f, -0.94873047f},
    Vec2 {0.33943176f, -0.94062805f},
    Vec2 {0.36259460f, -0.93193054f},
    Vec2 {0.38552856f, -0.92268372f},
    Vec2 {0.40823364f, -0.91287231f},
    Vec2 {0.43069458f, -0.90249634f},
    Vec2 {0.45288086f, -0.89155579f},
    Vec2 {0.47479248f, -0.88008118f},
    Vec2 {0.49641418f, -0.86807251f},
    Vec2 {0.51773071f, -0.85552979f},
    Vec2 {0.53874207f, -0.84245300f},
    Vec2 {0.55941772f, -0.82887268f},
    Vec2 {0.57974243f, -0.81478882f},
    Vec2 {0.59971619f, -0.80020142f},
    Vec2 {0.61932373f, -0.78511047f},
    Vec2 {0.63856506f, -0.76956177f},
    Vec2 {0.65739441f, -0.75352478f},
    Vec2 {0.67584229f, -0.73703003f},
    Vec2 {0.69386292f, -0.72009277f},
    Vec2 {0.71145630f, -0.70271301f},
    Vec2 {0.72862244f, -0.68490601f},
    Vec2 {0.74534607f, -0.66667175f},
    Vec2 {0.76159668f, -0.64802551f},
    Vec2 {0.77740479f, -0.62899780f},
    Vec2 {0.79272461f, -0.60957336f},
    Vec2 {0.80755615f, -0.58978271f},
    Vec2 {0.82189941f, -0.56962585f},
    Vec2 {0.83573914f, -0.54911804f},
    Vec2 {0.84906006f, -0.52827454f},
    Vec2 {0.86186218f, -0.50711060f},
    Vec2 {0.87414551f, -0.48564148f},
    Vec2 {0.88589478f, -0.46386719f},
    Vec2 {0.89709473f, -0.44181824f},
    Vec2 {0.90774536f, -0.41949463f},
    Vec2 {0.91784668f, -0.39691162f},
    Vec2 {0.92738342f, -0.37408447f},
    Vec2 {0.93635559f, -0.35104370f},
    Vec2 {0.94474792f, -0.32777405f},
    Vec2 {0.95256042f, -0.30430603f},
    Vec2 {0.95980835f, -0.28063965f},
    Vec2 {0.96644592f, -0.25682068f},
    Vec2 {0.97250366f, -0.23283386f},
    Vec2 {0.97796631f, -0.20870972f},
    Vec2 {0.98283386f, -0.18446350f},
    Vec2 {0.98709106f, -0.16009521f},
    Vec2 {0.99075317f, -0.13562012f},
    Vec2 {0.99380493f, -7280.0f / 65536.0f},
    Vec2 {0.99624634f, -0.08645630f},
    Vec2 {0.99807739f, -4050.0f / 65536.0f},
    Vec2 {0.99929810f, -2431.0f / 65536.0f},
    Vec2 {1.00000000f, 0.00000000f},
}};

inline Vec2 projectile_velocity_for_frame(int frame)
{
    const auto index = static_cast<std::size_t>(
        std::clamp(frame, 0, static_cast<int>(kProjectileVelocityTable.size() - 1)));
    return kProjectileVelocityTable[index];
}

inline float lane_y_for_variant(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D: return 1.0f;
    case AircraftVariant::A: return 24.0f;
    case AircraftVariant::B: return 4.0f;
    case AircraftVariant::C: return 51.0f;
    }
    return 1.0f;
}

inline float aircraft_width_for_variant(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D: return 69.0f;
    case AircraftVariant::A: return 38.0f;
    case AircraftVariant::B: return 63.0f;
    case AircraftVariant::C: return 44.0f;
    }
    return 44.0f;
}

inline float aircraft_height_for_variant(AircraftVariant variant)
{
    // 57ba copies the seed resource's height without adding a bottom row.
    switch (variant) {
    case AircraftVariant::D: return 17.0f;
    case AircraftVariant::A: return 14.0f;
    case AircraftVariant::B: return 13.0f;
    case AircraftVariant::C: return 13.0f;
    }
    return 14.0f;
}

inline CollisionBounds centered_bounds(WorldPosition center, Vec2 extent)
{
    return {
        center.x - extent.x,
        center.y - extent.y,
        center.x + extent.x,
        center.y + extent.y,
    };
}

inline CollisionBounds bounds_from_top_left(double x, double y, double width, double height)
{
    return {x, y, x + width, y + height};
}

inline CollisionBounds remake_aircraft_fragment_collision_bounds(const Object& object)
{
    static constexpr std::array<float, 48> kFragmentWidth {{
        19.0f, 21.0f, 17.0f, 19.0f, 21.0f, 21.0f, 15.0f, 18.0f,
        17.0f, 19.0f, 17.0f, 21.0f, 17.0f, 19.0f, 17.0f, 19.0f,
        18.0f, 20.0f, 18.0f, 20.0f, 19.0f, 19.0f, 16.0f, 19.0f,
        12.0f, 14.0f, 11.0f, 12.0f, 12.0f, 14.0f, 12.0f, 12.0f,
        14.0f, 14.0f, 12.0f, 12.0f, 12.0f, 13.0f, 11.0f, 13.0f,
        12.0f, 13.0f, 12.0f, 13.0f, 12.0f, 13.0f, 11.0f, 11.0f,
    }};
    static constexpr std::array<float, 48> kFragmentHeight {{
        17.0f, 18.0f, 20.0f, 19.0f, 17.0f, 17.0f, 20.0f, 20.0f,
        17.0f, 19.0f, 18.0f, 19.0f, 17.0f, 21.0f, 16.0f, 19.0f,
        16.0f, 17.0f, 19.0f, 19.0f, 17.0f, 18.0f, 17.0f, 17.0f,
        12.0f, 12.0f, 13.0f, 13.0f, 12.0f, 12.0f, 12.0f, 12.0f,
        11.0f, 13.0f, 12.0f, 13.0f, 11.0f, 11.0f, 11.0f, 12.0f,
        12.0f, 11.0f, 12.0f, 13.0f, 12.0f, 12.0f, 13.0f, 13.0f,
    }};

    const int sprite_id = object.sprite_id + (std::max(0, object.frame) % 8);
    int dimension_index = -1;
    if (sprite_id >= 0x121 && sprite_id <= 0x138) {
        dimension_index = sprite_id - 0x121;
    } else if (sprite_id >= 0x143 && sprite_id <= 0x15a) {
        dimension_index = 24 + sprite_id - 0x143;
    }

    if (dimension_index < 0) {
        return centered_bounds(object.position, object.extent);
    }

    const auto index = static_cast<std::size_t>(dimension_index);
    return bounds_from_top_left(object.position.x, object.position.y,
                                kFragmentWidth[index], kFragmentHeight[index]);
}

inline CollisionBounds collision_bounds_for_object(const Object& object)
{
    static constexpr std::array<float, 8> kParatrooperFrameOriginX {{
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f,
    }};
    static constexpr std::array<float, 8> kParatrooperFrameOriginY {{
        0.0f, 3.0f, 7.0f, 11.0f, 12.0f, 10.0f, 9.0f, 11.0f,
    }};
    static constexpr std::array<float, 8> kParatrooperFrameWidth {{
        11.0f, 11.0f, 11.0f, 11.0f, 9.0f, 8.0f, 7.0f, 6.0f,
    }};
    static constexpr std::array<float, 8> kParatrooperFrameHeight {{
        16.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f, 15.0f,
    }};

    if (object.type == ObjectType::Aircraft ||
        (object.type == ObjectType::EnemyDeath && !object.has_dropped_payload)) {
        return bounds_from_top_left(object.position.x, object.position.y,
                                    aircraft_width_for_variant(object.aircraft_variant),
                                    aircraft_height_for_variant(object.aircraft_variant));
    }

    if (object.type == ObjectType::PlayerProjectile) {
        // 2309 initializes a 2x2 record; 543a's -1 draw offset is not collision state.
        return bounds_from_top_left(object.position.x, object.position.y,
                                    object.extent.x * 2.0f, object.extent.y * 2.0f);
    }

    if (object.type == ObjectType::AircraftDebris) {
        if (!object.has_dropped_payload) {
            // The aircraft kill callbacks retain the aircraft's collision box.
            return bounds_from_top_left(object.position.x, object.position.y,
                                        aircraft_width_for_variant(object.aircraft_variant),
                                        aircraft_height_for_variant(object.aircraft_variant));
        }
        // 08ea calls 57ba once with the fragment's initial record. Animation
        // does not change its collision box. These are NTR record dimensions.
        switch (object.sprite_id) {
        case 0x121: return bounds_from_top_left(object.position.x, object.position.y, 19, 17);
        case 0x129: return bounds_from_top_left(object.position.x, object.position.y, 17, 17);
        case 0x131: return bounds_from_top_left(object.position.x, object.position.y, 18, 16);
        case 0x143: return bounds_from_top_left(object.position.x, object.position.y, 12, 12);
        case 0x14b: return bounds_from_top_left(object.position.x, object.position.y, 14, 11);
        case 0x153: return bounds_from_top_left(object.position.x, object.position.y, 12, 12);
        default: return centered_bounds(object.position, object.extent);
        }
    }

    if (object.type == ObjectType::SmartBomb ||
        (object.type == ObjectType::EnemyDeath && object.has_dropped_payload)) {
        const bool armed = object.type == ObjectType::EnemyDeath
                               ? object.finale_drop : object.extent.y < 6.0f;
        if (armed) {
            return bounds_from_top_left(object.position.x, object.position.y, 17.0f, 4.0f);
        }

        // 1b9a initializes bounds from record 1d5 once. Later rotation frames
        // do not reinitialize them or add the compiled blitter's crop origin.
        return bounds_from_top_left(object.position.x, object.position.y, 20.0f, 14.0f);
    }

    if (object.type == ObjectType::Paratrooper && object.finale_drop) {
        // Both initializers call 57ba with resource 278 (record size 5x6).
        return bounds_from_top_left(object.position.x, object.position.y, 5.0f, 6.0f);
    }

    if ((object.type == ObjectType::Paratrooper ||
         object.type == ObjectType::GroundedTransition) &&
        object.parachute_lost) {
        // 0401 binds the initialized box; 034b changes the frame, not its bounds.
        return bounds_from_top_left(object.position.x, object.position.y,
                                    object.extent.x * 2.0f, object.extent.y * 2.0f);
    }

    if (object.type == ObjectType::Paratrooper) {
        return bounds_from_top_left(object.position.x, object.position.y,
                                    object.extent.x * 2.0f, object.extent.y * 2.0f);
    }

    if (object.type == ObjectType::GroundedTransition) {
        // 068a binds F7 once; 0559 retains its dimensions through all eight poses.
        return bounds_from_top_left(object.position.x, object.position.y,
                                    object.extent.x * 2.0f, object.extent.y * 2.0f);
    }

    if (object.type == ObjectType::LandedInvader) {
        return bounds_from_top_left(object.position.x, object.position.y, 4.0f, 7.0f);
    }

    return centered_bounds(object.position, object.extent);
}

inline WorldPosition collision_center_for_object(const Object& object)
{
    const auto bounds = collision_bounds_for_object(object);
    return {
        (bounds.left + bounds.right) * 0.5f,
        (bounds.top + bounds.bottom) * 0.5f,
    };
}

inline bool remake_aircraft_fragment_hits_object(const Object& fragment, const Object& object)
{
    const auto fragment_bounds = remake_aircraft_fragment_collision_bounds(fragment);
    const auto object_bounds = collision_bounds_for_object(object);
    return fragment_bounds.left < object_bounds.right &&
           fragment_bounds.right > object_bounds.left &&
           fragment_bounds.top < object_bounds.bottom &&
           fragment_bounds.bottom > object_bounds.top;
}

inline bool projectile_hits_deployed_paratrooper_body(const Object& projectile,
                                                      const Object& paratrooper)
{
    // 16c8:0401 compares both words of the 16.16 Y coordinate. A shot
    // strictly above Y+0xb cuts the chute; equality belongs to the body.
    return projectile.position.y >= paratrooper.position.y + 11.0f;
}

inline float aircraft_speed_for_variant(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D:
    case AircraftVariant::B: return 1.0f;
    case AircraftVariant::A: return 2.0f;
    case AircraftVariant::C: return 3.0f;
    }
    return 1.0f;
}

inline int score_for_aircraft(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D:
    case AircraftVariant::B: return 5;
    case AircraftVariant::A:
    case AircraftVariant::C: return 10;
    }
    return 0;
}

inline std::uint32_t smart_bomb_stage_duration(int stage)
{
    return stage == 4 ? 35u : 30u;
}

inline Vec2 smart_bomb_stage_velocity(int stage, int direction)
{
    const float dir = direction == 0 ? 1.0f : -1.0f;
    switch (stage) {
    case 1: return {0.69099426f * dir, 0.30900574f};
    case 2: return {0.41221619f * dir, 0.58778381f};
    case 3: return {0.19099426f * dir, 0.80900574f};
    case 4: return {0.0f, 1.0f};
    default: return {0.0f, 0.0f};
    }
}

inline int aircraft_debris_shell_delay(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D:
    case AircraftVariant::B: return 10;
    case AircraftVariant::A:
    case AircraftVariant::C: return 6;
    }
    return 6;
}

inline Vec2 remake_aircraft_fragment_velocity(Vec2 aircraft_velocity,
                                              float horizontal_impulse,
                                              float vertical_impulse,
                                              int fragment_index)
{
    const float scatter_direction = (fragment_index & 1) == 0 ? -1.0f : 1.0f;
    return {
        aircraft_velocity.x * 0.35f + scatter_direction * horizontal_impulse,
        aircraft_velocity.y + vertical_impulse,
    };
}

inline const std::array<std::uint32_t, 60>& finale_particle_burst_ticks()
{
    static const auto ticks = original_finale_particle_frame(0xff9652c1u, 2200).burst_ticks;
    return ticks;
}

inline bool object_type_allows_level_clear(ObjectType type)
{
    switch (type) {
    case ObjectType::None:
    case ObjectType::PlayerCannon:
    case ObjectType::WaveController:
    case ObjectType::PlayerProjectile:
    case ObjectType::LandedInvader:
    case ObjectType::Presenter:
        return true;
    case ObjectType::Aircraft:
    case ObjectType::EnemyDeath:
    case ObjectType::AircraftDebris:
    case ObjectType::SmartBomb:
    case ObjectType::Paratrooper:
    case ObjectType::GroundedTransition:
    case ObjectType::ResolutionParticle:
    case ObjectType::FinaleController:
        return false;
    }
    return false;
}

}  // namespace niteraid::internals
