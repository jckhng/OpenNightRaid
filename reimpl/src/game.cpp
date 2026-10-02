#include "niteraid/game.hpp"
#include "niteraid/game_internals.hpp"
#include "niteraid/presenter_timing.hpp"
#include "niteraid/shareware_ending.hpp"
#include "niteraid/survivor_presenter.hpp"
#include "niteraid/finale_presenter.hpp"
#include "niteraid/finale_particles.hpp"
#include "niteraid/pizza_presenter.hpp"
#include "niteraid/helicopter_presenter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace niteraid {

namespace {

using niteraid::internals::aircraft_debris_shell_delay;
using niteraid::internals::aircraft_speed_for_variant;
using niteraid::internals::aircraft_width_for_variant;
using niteraid::internals::append_high_score_name_character;
using niteraid::internals::collision_bounds_for_object;
using niteraid::internals::cycle_gamepad_high_score_character;
using niteraid::internals::finalized_high_score_name;
using niteraid::internals::kAimFrameMax;
using niteraid::internals::kAimFrameMin;
using niteraid::internals::kProjectileVelocityTable;
using niteraid::internals::lane_y_for_variant;
using niteraid::internals::object_type_allows_level_clear;
using niteraid::internals::projectile_hits_deployed_paratrooper_body;
using niteraid::internals::projectile_velocity_for_frame;
using niteraid::internals::score_for_aircraft;
using niteraid::internals::smart_bomb_stage_duration;
using niteraid::internals::smart_bomb_stage_velocity;

constexpr float kGameplayWidth = 320.0f;
constexpr float kGameplayHeight = 200.0f;
constexpr float kGroundY = 164.0f;
constexpr float kBunkerX = 160.0f;
constexpr float kBunkerOriginalX = 148.0f;
constexpr float kBunkerOriginalY = 166.0f;
constexpr float kBunkerOriginalWidth = 26.0f;
constexpr float kBunkerOriginalHeight = 12.0f;
constexpr float kParatrooperOriginalWidth = 10.0f;
constexpr float kParatrooperOriginalHeight = 15.0f;
    constexpr std::size_t kObjectTableCapacity = 200;
constexpr float kProjectileSpawnX = 160.0f;
constexpr float kProjectileSpawnY = 165.0f;
constexpr float kBunkerY = 180.0f;
constexpr float kBunkerThreatX = 155.0f;
constexpr float kBunkerDoorOuterX = 161.0f;
constexpr float kBunkerDoorInnerX = 155.0f;
constexpr float kFinaleControllerY = 0x1e;
constexpr float kFinaleControllerEntryX = 0x108;
constexpr int kInitialAimFrame = 6;
constexpr int kInitialAimAccumulator = 0;
constexpr int kNeutralAimAccumulator = 0x80;
constexpr int kKeyboardAimStep = 3;
constexpr std::uint32_t kInitialGameplayClock = 1;
constexpr int kLandedInvaderLimit = 3;
constexpr std::uint32_t kTransitionDelayTicks = 210;
constexpr std::uint32_t kPlayerFireGateTicks = 10;
constexpr std::uint16_t kSoundSourceMode = 2;
constexpr std::uint16_t kSoundBlasterMode = 3;
constexpr std::uint32_t kBunkerAssaultOuterStepTicks = 8;
constexpr std::uint32_t kBunkerAssaultInnerStepTicks = 5;
constexpr int kDeathFlashTicks = 10;
constexpr std::uint32_t kBunkerWhiteFlagTicks = 720;
constexpr std::uint32_t kOverrunGameOverTicks = 720;
constexpr int kLevelBannerTicks = 210;
constexpr std::uint32_t kParatrooperDeployStepTicks = 15;
constexpr float kParatrooperInitialFallSpeed = 0.5f;
constexpr float kParatrooperCanopyFallSpeed = 0.25f;
constexpr float kParatrooperFreeFallSpeed = 0.5f;
constexpr float kGroundedAnimationWidth = 6.0f;
constexpr float kGroundedAnimationHeight = 15.0f;
constexpr float kLandedInvaderMergeDistance = 6.0f;
constexpr float kLandedInvaderWidth = 4.0f;
constexpr float kSmartBombArmedWidth = 17.0f;
constexpr float kSmartBombArmedHeight = 4.0f;
constexpr float kSmartBombFallWidth = 20.0f;
constexpr float kSmartBombFallHeight = 14.0f;
constexpr float kProjectileMuzzleOffset = 20.0f;
constexpr float kFinaleControllerMinX = 40.0f;
constexpr float kFinaleControllerMaxX = 260.0f;
constexpr std::uint32_t kFinaleSpawnCadenceTicks = 0x118;
constexpr std::uint32_t kFinaleDropArmingTicks = 0x10;
constexpr std::uint32_t kFinaleDropAnimationTicks = 6;
constexpr float kFinaleSpawnOffsetX = 9.0f;
constexpr float kFinaleSpawnOffsetY = 8.0f;
constexpr std::uint16_t kSoundPlayerFire = 0x327;
constexpr std::uint16_t kSoundGroundedResolution = 0x329;
constexpr std::uint16_t kSoundSmartBombSpawn = 0x32f;
constexpr std::uint16_t kSoundSmartBombArm = 0x331;
constexpr std::uint16_t kSoundEnemyDeath = 0x333;
constexpr std::uint16_t kSoundBunkerWalkA = 0x33f;
constexpr std::uint16_t kSoundBunkerWalkB = 0x341;
constexpr std::uint16_t kSoundBunkerSpecialSetup = 0x337;
constexpr std::uint16_t kSoundBunkerAssault = 0x339;
constexpr std::uint16_t kSoundVehicleStop = 0x33b;
constexpr std::uint16_t kSoundVehicleStart = 0x33d;
constexpr std::uint16_t kSoundNoSurvivorFlyby = 0x357;
constexpr std::uint16_t kSoundPizzaVehicle = 0x343;
constexpr int kPizzaPresenterUpdates = 1866;
constexpr std::uint32_t kPizzaLowerDoorwayUpdate = 884;
constexpr std::uint32_t kPizzaRaiseDoorwayUpdate = 1222;
constexpr std::uint16_t kSoundPizzaArrival = 0x347;
constexpr std::uint16_t kSoundPizzaReply = 0x345;
constexpr std::uint16_t kSoundHelicopterFlight = 0x349;
constexpr std::uint16_t kSoundHelicopterParked = 0x34b;
constexpr std::uint16_t kSoundHelicopterDeparture = 0x34d;
constexpr std::uint16_t kSoundTerminalPrelude = 0x365;
constexpr std::uint16_t kSoundTerminalPulse = 0x367;
constexpr std::uint16_t kSoundTerminalExplosion = 0x369;
constexpr std::uint16_t kSoundFinaleController = 0x34f;
constexpr std::uint16_t kSoundFinaleDeparture = 0x351;
constexpr std::uint16_t kSoundFinaleProjectile = 0x353;
constexpr std::uint16_t kSoundFinaleBurst = 0x355;
constexpr std::size_t kConfigSize = 0x1de;
constexpr std::uint16_t kConfigVersion = 3;
constexpr std::size_t kConfigHighScoreOffset = 0x16;
constexpr std::size_t kHighScoreRecordSize = 0x4c;
constexpr std::size_t kHighScoreNameSize = 0x40;

constexpr std::uint32_t original_tick_at_video_frame(std::uint32_t frame)
{
    return original_timer_tick_at_simulation_frame(frame);
}

constexpr bool crossed_original_tick(std::uint32_t video_frame,
                                     std::uint32_t original_tick)
{
    if (video_frame == 0) {
        return original_tick == 0;
    }
    return original_tick_at_video_frame(video_frame - 1) < original_tick &&
           original_tick_at_video_frame(video_frame) >= original_tick;
}

constexpr std::uint16_t aircraft_sound_id(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D: return 0x32d;
    case AircraftVariant::A: return 0x32b;
    case AircraftVariant::B:
    case AircraftVariant::C: return 0x335;
    }
    return 0;
}

constexpr int aircraft_death_frame_limit(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D:
    case AircraftVariant::B: return 9;
    case AircraftVariant::A:
    case AircraftVariant::C: return 6;
    }
    return 6;
}

// Moved to niteraid/game_internals.hpp so unit tests can reach it.
// kProjectileVelocityTable was here.
#if 0
constexpr std::array<Vec2, 127> kProjectileVelocityTable {{
    Vec2 {-1.00000000f, 0.00000000f},
    Vec2 {-0.99929810f, -0.03709412f},
    Vec2 {-0.99807739f, -0.06179810f},
    Vec2 {-0.99624634f, -0.08645630f},
    Vec2 {-0.99380493f, -0.11108398f},
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
    Vec2 {-0.07414246f, -0.99723816f},
    Vec2 {-0.04943848f, -0.99876404f},
    Vec2 {-0.02471924f, -0.99967957f},
    Vec2 {0.00000000f, -1.00000000f},
    Vec2 {0.02471924f, -0.99967957f},
    Vec2 {0.04943848f, -0.99876404f},
    Vec2 {0.07414246f, -0.99723816f},
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
    Vec2 {0.99380493f, -0.11108398f},
    Vec2 {0.99624634f, -0.08645630f},
    Vec2 {0.99807739f, -0.06179810f},
    Vec2 {0.99929810f, -0.03709412f},
    Vec2 {1.00000000f, 0.00000000f},
}};
#endif

WaveRecord make_record(std::uint32_t start_tick,
                       std::uint16_t spawn_count,
                       std::uint16_t min_delay,
                       std::uint16_t max_delay,
                       std::uint16_t paratrooper_cadence,
                       std::uint32_t stop_tick = 0xffffffffu)
{
    return WaveRecord {
        .start_tick = start_tick,
        .stop_tick = stop_tick,
        .spawn_count = spawn_count,
        .min_delay = min_delay,
        .max_delay = max_delay,
        .paratrooper_cadence = paratrooper_cadence,
    };
}

float drop_offset_for_variant(AircraftVariant variant, int direction)
{
    const auto use_first = direction > 0;

    switch (variant) {
    case AircraftVariant::D:
        return use_first ? 27.0f : 28.0f;
    case AircraftVariant::A:
        return use_first ? 16.0f : 10.0f;
    case AircraftVariant::B:
        return use_first ? 0.0f : 45.0f;
    case AircraftVariant::C:
        return use_first ? 24.0f : 5.0f;
    }

    return 0.0f;
}

float aim_angle_radians(int frame)
{
    const auto clamped = std::clamp(frame, kAimFrameMin, kAimFrameMax);
    const auto t = static_cast<float>(clamped - kAimFrameMin) /
                   static_cast<float>(kAimFrameMax - kAimFrameMin);
    const auto degrees = 160.0f - (t * 140.0f);
    return degrees * 3.14159265f / 180.0f;
}

bool lands_on_bunker(const Object& paratrooper, const std::vector<Object>& objects)
{
    const auto player_it = std::find_if(objects.begin(), objects.end(), [](const Object& object) {
        return object.active && object.type == ObjectType::PlayerCannon;
    });

    if (player_it == objects.end()) {
        return std::abs(paratrooper.position.x - kBunkerX) <= (paratrooper.extent.x + 12.0f);
    }

    return std::abs(paratrooper.position.x - player_it->position.x) <=
           (paratrooper.extent.x + player_it->extent.x);
}

bool object_outside_original_cull_bounds(const Object& object)
{
    // 5378 keeps the fractional part of row 199, but excludes exact left/top
    // boundaries and all of column 320. All moving callbacks share this rule.
    return internals::original_motion_outside_bounds(object.position);
}

bool objects_overlap(const Object& left, const Object& right, float slop = 0.0f)
{
    const auto left_bounds = collision_bounds_for_object(left);
    const auto right_bounds = collision_bounds_for_object(right);
    return left_bounds.left < right_bounds.right + slop &&
           left_bounds.right + slop > right_bounds.left &&
           left_bounds.top < right_bounds.bottom + slop &&
           left_bounds.bottom + slop > right_bounds.top;
}

bool is_aircraft_debris(const Object& object)
{
    // 2730:0792 gives type 7 flags 0003, but type 6 death visuals flags 0000.
    return object.type == ObjectType::AircraftDebris;
}

bool is_debris_resolvable_invader(const Object& object)
{
    return object.type == ObjectType::Paratrooper || object.type == ObjectType::GroundedTransition ||
           object.type == ObjectType::LandedInvader;
}

bool landed_invader_hits_original_edge_resolution(const Object& object)
{
    return object.position.x < 0.0f || object.position.x >= (kGameplayWidth - kLandedInvaderWidth);
}

WorldPosition paratrooper_original_space_position(const Object& object)
{
    return object.position;
}

float drop_y_offset_for_variant(AircraftVariant variant)
{
    return variant == AircraftVariant::D ? 2.0f : 0.0f;
}

bool paratrooper_hits_bunker_zone(const Object& paratrooper, const std::vector<Object>& objects)
{
    const auto player_it = std::find_if(objects.begin(), objects.end(), [](const Object& object) {
        return object.active && !object.pending_destroy && object.type == ObjectType::PlayerCannon;
    });
    if (player_it == objects.end()) {
        return false;
    }

    const auto position = paratrooper_original_space_position(paratrooper);
    return position.x < kBunkerOriginalX + kBunkerOriginalWidth &&
           position.x + kParatrooperOriginalWidth > kBunkerOriginalX &&
           position.y < kBunkerOriginalY + kBunkerOriginalHeight &&
           position.y + kParatrooperOriginalHeight > kBunkerOriginalY;
}

Vec2 aircraft_debris_fragment_offset(AircraftVariant variant, int direction)
{
    const bool large_family = variant == AircraftVariant::D || variant == AircraftVariant::B;
    const float left_offset = large_family ? 44.0f : 10.0f;
    const float right_offset = large_family ? 28.0f : 1.0f;
    // Aircraft store DOS flag 0 as +1 and flag 1 as -1.
    return {direction > 0 ? left_offset : right_offset, 0.0f};
}

int scheduled_direction_from_period(std::uint32_t scheduled_tick, std::uint32_t period)
{
    return static_cast<int>(((scheduled_tick / period) & 1u) ^ 1u);
}

std::filesystem::path writable_config_path()
{
    return std::filesystem::path("reimpl") / "save" / "CONFIG.NTR";
}

std::array<std::filesystem::path, 2> readable_config_paths(const std::filesystem::path& config_path)
{
    if (!config_path.empty()) {
        return {config_path, config_path.parent_path() / "reimpl" / "save" / "CONFIG.NTR"};
    }
    return {
        writable_config_path(),
        std::filesystem::path("original") / "NITERAID" / "CONFIG.NTR",
    };
}

std::uint16_t read_u16_le(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

void write_u16_le(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void report_config_persistence_failure(const std::filesystem::path& path,
                                       const char* operation,
                                       const std::error_code& error)
{
    std::fprintf(stderr, "Night Raid: could not %s %s: %s\n", operation,
                 path.string().c_str(), error.message().c_str());
}

bool atomically_replace_file(const std::filesystem::path& temporary,
                             const std::filesystem::path& destination,
                             std::error_code& error)
{
#ifdef _WIN32
    if (!MoveFileExW(temporary.wstring().c_str(), destination.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    return true;
#else
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

bool write_config_atomically(const std::filesystem::path& destination,
                             const std::vector<std::uint8_t>& bytes)
{
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        report_config_persistence_failure(destination.parent_path(), "create the save directory", error);
        return false;
    }

    auto temporary = destination;
    temporary += ".tmp";
    if (std::filesystem::exists(temporary, error)) {
        if (error) {
            report_config_persistence_failure(temporary, "inspect the temporary save file", error);
            return false;
        }
        std::filesystem::remove(temporary, error);
        if (error) {
            report_config_persistence_failure(temporary, "remove the stale temporary save file", error);
            return false;
        }
    }
    if (error) {
        report_config_persistence_failure(temporary, "inspect the temporary save file", error);
        return false;
    }

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = std::make_error_code(std::errc::io_error);
            report_config_persistence_failure(temporary, "open the temporary save file", error);
            return false;
        }
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file) {
            error = std::make_error_code(std::errc::io_error);
            report_config_persistence_failure(temporary, "write the temporary save file", error);
            file.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
        file.close();
        if (file.fail()) {
            error = std::make_error_code(std::errc::io_error);
            report_config_persistence_failure(temporary, "close the temporary save file", error);
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    const auto size = std::filesystem::file_size(temporary, error);
    if (error || size != bytes.size()) {
        if (!error) {
            error = std::make_error_code(std::errc::io_error);
        }
        report_config_persistence_failure(temporary, "validate the temporary save file", error);
        std::filesystem::remove(temporary, error);
        return false;
    }

    error.clear();
    if (!atomically_replace_file(temporary, destination, error)) {
        report_config_persistence_failure(destination, "replace CONFIG.NTR", error);
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::string name_from_record(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    std::string name;
    for (std::size_t index = 0; index < kHighScoreNameSize; ++index) {
        const auto value = bytes[offset + index];
        if (value == 0) {
            break;
        }
        name.push_back(static_cast<char>(value));
    }

    return name;
}

std::array<HighScoreEntry, 6> default_high_scores()
{
    std::array<HighScoreEntry, 6> entries {};
    static constexpr std::array<std::string_view, 6> kDefaultNames {{
        "Argo Games - 1993",
        "",
        "Jason Blochowiak",
        "Don Glassford",
        "Dan Linton",
        "Robert Prince",
    }};

    for (std::size_t index = 0; index < entries.size(); ++index) {
        entries[index].name = std::string(kDefaultNames[index]);
        entries[index].score = 100;
        entries[index].stat_a = 0;
        entries[index].stat_b = 0;
        entries[index].stat_c = 0;
        entries[index].highlighted = false;
    }
    return entries;
}

bool control_panel_voice_row_enabled(const WorldState& world)
{
    // RefreshControlPanelAudioSelectors (1480:0887) enables row 1 only when
    // DAT_2730_1ff8 is Sound Source (2) or SoundBlaster (3).
    const auto sound_mode = world.audio_config_words[3];
    return sound_mode == kSoundSourceMode || sound_mode == kSoundBlasterMode;
}

int control_panel_choice_count(const WorldState& world, int row)
{
    switch (row) {
    case 0:
        return 4;
    case 1:
        return control_panel_voice_row_enabled(world) ? 3 : 0;
    case 2:
        return 2;
    default:
        return 1;
    }
}

constexpr std::array<int, 6> kControlPanelHitTops {{32, 52, 72, 111, 131, 151}};
constexpr int kControlPanelHitHeight = 20;

bool any_mouse_button(const InputState& input)
{
    return input.mouse_primary || input.mouse_secondary || input.mouse_middle;
}

int control_panel_next_selectable_row(const WorldState& world, int row, int delta)
{
    int candidate = row;
    while (true) {
        candidate += delta;
        if (candidate < 0 || candidate >= 6) {
            return row;
        }
        if (candidate >= 3 || control_panel_choice_count(world, candidate) > 0) {
            return candidate;
        }
    }
}

}  // namespace

Game::Game()
    : Game(std::nullopt)
{
}

Game::Game(std::optional<std::uint16_t> start_level)
    : Game(start_level, std::nullopt)
{
}

Game::Game(std::optional<std::uint16_t> start_level,
           std::optional<std::uint16_t> finale_budget_override,
           bool fast_shots,
           bool female_finale,
           bool enhanced_input_debounce,
           bool shareware_edition,
           std::optional<std::uint32_t> gameplay_seed_override,
           std::filesystem::path config_path)
    : config_path_(std::move(config_path)),
      finale_budget_override_(finale_budget_override),
      enhanced_input_debounce_(enhanced_input_debounce),
      shareware_edition_(shareware_edition)
{
    world_.fast_shots_enabled = fast_shots;
    world_.female_finale_enabled = female_finale;
    load_high_scores();
    if (gameplay_seed_override.has_value()) {
        world_.gameplay_rng_seed = *gameplay_seed_override;
    }

    wave_table_[0][0] = make_record(0, 10, 400, 500, 80);
    wave_table_[0][1] = make_record(500, 7, 400, 500, 80);
    wave_table_[0][2] = make_record(0, 10, 400, 500, 80);
    wave_table_[0][3] = make_record(600, 15, 400, 500, 70);
    wave_table_[0][8] = make_record(0, 5, 600, 800, 40);
    wave_table_[0][9] = make_record(0, 6, 600, 800, 40);
    wave_table_[0][10] = make_record(0, 7, 600, 800, 40);
    wave_table_[0][11] = make_record(0, 8, 600, 800, 40);

    wave_table_[1][1] = make_record(0, 5, 500, 1000, 70);
    wave_table_[1][2] = make_record(0, 5, 300, 300, 60);
    wave_table_[1][3] = make_record(600, 10, 250, 300, 50);
    wave_table_[1][4] = make_record(0, 10, 250, 300, 45);
    wave_table_[1][5] = make_record(0, 10, 250, 300, 40);
    wave_table_[1][6] = make_record(0, 12, 250, 300, 30);
    wave_table_[1][7] = make_record(0, 13, 250, 300, 30);
    wave_table_[1][8] = make_record(0, 15, 200, 300, 20);
    wave_table_[1][9] = make_record(0, 15, 200, 300, 20);
    wave_table_[1][10] = make_record(0, 15, 200, 300, 20);
    wave_table_[1][11] = make_record(0, 15, 200, 300, 20);

    wave_table_[2][4] = make_record(0, 16, 350, 500, 70);
    wave_table_[2][5] = make_record(0, 17, 300, 500, 70);
    wave_table_[2][6] = make_record(0, 10, 300, 500, 60);
    wave_table_[2][7] = make_record(0, 10, 300, 500, 60);
    wave_table_[2][8] = make_record(0, 5, 600, 800, 40);
    wave_table_[2][9] = make_record(0, 6, 600, 800, 40);
    wave_table_[2][10] = make_record(0, 7, 600, 800, 40);
    wave_table_[2][11] = make_record(0, 8, 600, 800, 40);

    wave_table_[3][4] = make_record(0, 10, 250, 300, 45);
    wave_table_[3][5] = make_record(0, 10, 250, 300, 40);
    wave_table_[3][6] = make_record(0, 12, 250, 300, 30);
    wave_table_[3][7] = make_record(0, 13, 250, 300, 30);
    wave_table_[3][8] = make_record(0, 15, 200, 300, 20);
    wave_table_[3][9] = make_record(0, 15, 200, 300, 20);
    wave_table_[3][10] = make_record(0, 15, 200, 300, 20);
    wave_table_[3][11] = make_record(0, 15, 200, 300, 20);

    wave_table_[4][3] = make_record(0, 3, 400, 500, 0);
    wave_table_[4][4] = make_record(0, 4, 400, 500, 0);
    wave_table_[4][5] = make_record(0, 5, 400, 500, 0);
    wave_table_[4][6] = make_record(0, 7, 400, 1000, 0);
    wave_table_[4][7] = make_record(0, 9, 150, 900, 0);
    wave_table_[4][8] = make_record(0, 20, 100, 400, 0);
    wave_table_[4][9] = make_record(0, 20, 80, 400, 0);
    wave_table_[4][10] = make_record(0, 20, 40, 200, 0);
    wave_table_[4][11] = make_record(0, 20, 30, 150, 0);

    if (start_level.has_value()) {
        this->start_level(*start_level);
    }
}

void Game::tick(const InputState& input)
{
    before_owner_page_.reset();
    if (world_.screen == Screen::Intermission && world_.survivor_live &&
        world_.survivor_live->owner_return_ready &&
        world_.survivor_live->phase == SurvivorPhase::Egress &&
        world_.survivor_live->leg == 4) {
        // The preceding page has already completed. Retire before the next
        // owner's clock increment, preserving normal round initialization.
        const auto slot = world_.survivor_live->controller_slot;
        world_.objects.at(slot).position = survivor_target_;
        retire_object(slot);
        world_.survivor_live.reset();
        finish_intermission();
    }
    if (world_.screen == Screen::ControlPanel) {
        const auto action = menu_key_action(input);
        if (action != MenuKeyAction::None) {
            control_panel_key_latch_ = action;
        }
    }
    const bool control_panel_owner_blocked = world_.screen == Screen::ControlPanel &&
        (world_.control_panel_pending_option_row >= 0 || world_.control_panel_pending_action_row >= 0 ||
         prompt_mouse_response_ != PromptMouseResponse::None);
    const bool keyboard_escape_edge = input.escape && !previous_input_.escape;
    const bool gamepad_back_edge = input.gamepad_back && !previous_input_.gamepad_back;
    const bool gamepad_start_edge = input.gamepad_start && !previous_input_.gamepad_start;
    const bool gamepad_back_uses_global_escape =
        world_.screen != Screen::ControlPanel && world_.screen != Screen::HighScoreEntry;
    const bool escape_action_edge =
        keyboard_escape_edge ||
        (gamepad_back_edge && gamepad_back_uses_global_escape) ||
        (gamepad_start_edge && world_.screen == Screen::Gameplay);
    const bool pause_clock =
        (world_.screen == Screen::ControlPanel && world_.control_panel_from_gameplay) ||
        (world_.screen == Screen::Gameplay && world_.gameplay_state == GameplayState::Active &&
         escape_action_edge);
    if (!pause_clock) {
        ++world_.frame_tick;
    }
    world_.sound_events.clear();
    world_.sound_stop_events.clear();
    world_.reset_sound_effects = false;
    if (world_.screen_fade_frames_remaining > 0) {
        --world_.screen_fade_frames_remaining;
    }

    // 1abe ends the 354e gameplay loop. The fatal presenter owns this interval;
    // neither modal/warp input nor additional actor/collision updates run here.
    if (world_.screen == Screen::Gameplay &&
        world_.gameplay_state == GameplayState::GameOver &&
        (world_.death_flash_frames_remaining > 0 || world_.death_palette_restored)) {
        update_death_flash();
        previous_input_ = input;
        return;
    }

    if (world_.screen == Screen::Gameplay &&
        input.control_modifier &&
        input.alt_modifier &&
        input.level_warp_digit >= 0 &&
        input.level_warp_digit <= 9) {
        const int one_based_level =
            input.level_warp_digit == 0 ? 10 : input.level_warp_digit;
        start_level(static_cast<std::uint16_t>(one_based_level - 1));
        previous_input_ = input;
        return;
    }

    if (world_.confirmation_prompt != ConfirmationPromptAction::None) {
        if (update_confirmation_prompt(input)) {
            previous_input_ = input;
            return;
        }
    } else if (escape_action_edge && !control_panel_owner_blocked) {
        if (world_.screen == Screen::Gameplay) {
            if (world_.bunker_assault_active || world_.gameplay_state == GameplayState::ScriptedSequence) {
                interrupt_bunker_assault();
                previous_input_ = input;
                return;
            }
            enter_control_panel(true, 3);
        } else if (world_.screen == Screen::ControlPanel) {
            control_panel_key_latch_ = MenuKeyAction::None;
            world_.control_panel_initial_hint = false;
            world_.control_panel_option_feedback = false;
            control_panel_prompt_return_row_ = world_.control_panel_row;
            world_.control_panel_row = 5;
            world_.control_panel_pending_action_row = 5;
            world_.control_panel_action_frames_remaining = 15;
        } else if (world_.screen == Screen::Title ||
                   world_.screen == Screen::Credits ||
                   world_.screen == Screen::AttractInterlude ||
                   world_.screen == Screen::HighScores) {
            enter_control_panel(false, 5);
        } else if (world_.screen == Screen::Intermission) {
            advance_level();
        } else {
            // Scripted terminal and name-entry screens own their input handling.
            // The original has no global Escape-to-quit prompt.
            goto dispatch_screen;
        }
        previous_input_ = input;
        return;
    }

dispatch_screen:
    switch (world_.screen) {
    case Screen::Title:
        update_title(input);
        break;
    case Screen::Credits:
        update_credits(input);
        break;
    case Screen::AttractInterlude:
        update_attract_interlude(input);
        break;
    case Screen::ControlPanel:
        update_control_panel(input);
        break;
    case Screen::Gameplay:
        update_gameplay_wrapper(input);
        break;
    case Screen::Intermission:
        update_intermission(input);
        break;
    case Screen::GameOver:
        update_game_over(input);
        break;
    case Screen::HighScoreEntry:
        update_high_score_entry(input);
        break;
    case Screen::HighScores:
        update_high_scores(input);
        break;
    case Screen::Finale:
        update_finale(input);
        break;
    case Screen::SharewareEnding:
        update_shareware_ending(input);
        break;
    }

    previous_input_ = input;
}

const WorldState& Game::world() const
{
    return world_;
}

void Game::prepare_gameplay_page()
{
    if (world_.screen == Screen::Intermission && world_.survivor_live) {
        auto& scene = *world_.survivor_live;
        auto& controller = world_.objects.at(scene.controller_slot);
        if (controller.active && scene.phase == SurvivorPhase::Rare && !scene.animation_complete) {
            scene.beam_variant = original_random_bounded(world_.gameplay_rng_seed, 2);
            if (controller.frame == 8) {
                controller.sound_id = 0;
            }
        }
        scene.owner_return_ready = world_.gameplay_state != GameplayState::Active;
        return;
    }
    // 1abe sets GameOver before 32d5's final 2e5e actor draw; 00f0 then retains it.
    const bool fatal_actor_page = world_.gameplay_state == GameplayState::GameOver &&
        world_.player_dead && world_.frame_tick == world_.transition_started_tick &&
        world_.death_flash_frames_remaining > 0 && !world_.fatal_retained_page;
    const bool active_actor_page = world_.gameplay_state == GameplayState::Active && !world_.player_dead;
    if (world_.screen != Screen::Gameplay || (!active_actor_page && !fatal_actor_page) ||
        world_.confirmation_prompt != ConfirmationPromptAction::None) {
        return;
    }
    // 54a9 clears the fire latch when the cannon is drawn, not on the next update.
    world_.muzzle_flash_frames_remaining = muzzle_flash_pending_draw_ ? 1 : 0;
    muzzle_flash_pending_draw_ = false;
    // 09da/0a3e/0da2/0e01 own +22 during drawing, not each 31a0 update.
    for (auto& object : world_.objects) {
        const bool aircraft_draw = object.type == ObjectType::Aircraft ||
            (object.type == ObjectType::AircraftDebris && !object.has_dropped_payload);
        if (!object.active || object.pending_destroy || !aircraft_draw) {
            continue;
        }
        if (object.aircraft_variant == AircraftVariant::D) {
            object.frame = (object.frame + 1) % internals::kAircraftDRotorFrames;
        } else if (object.aircraft_variant == AircraftVariant::A) {
            object.presented_rotor_frame = object.frame;
            object.frame = (object.frame + 1) % internals::kAircraftARotorFrames;
        }
    }
}

WorldState& Game::diagnostic_world()
{
    return world_;
}

void Game::enter_control_panel(bool from_gameplay, int initial_row)
{
    fire_buffered_ = false;
    control_panel_key_latch_ = MenuKeyAction::None;
    prompt_mouse_response_ = PromptMouseResponse::None;
    world_.screen = Screen::ControlPanel;
    world_.control_panel_row = std::clamp(initial_row, 0, 5);
    control_panel_mouse_y_ = static_cast<float>(kControlPanelHitTops[world_.control_panel_row]);
    world_.control_panel_dirty = false;
    world_.control_panel_from_gameplay = from_gameplay;
    world_.control_panel_initial_hint = true;
    world_.control_panel_option_feedback = false;
    world_.control_panel_pending_option_row = -1;
    world_.control_panel_action_frames_remaining = 0;
    world_.control_panel_pending_action_row = -1;
    control_panel_prompt_return_row_ = world_.control_panel_row;
    world_.screen_fade_frames_remaining = kScreenFadeFrames;
}

MenuKeyAction Game::menu_key_action(const InputState& input) const
{
    if (input.menu_key_action != MenuKeyAction::None &&
        (!enhanced_input_debounce_ || input.menu_key_action != MenuKeyAction::Ignored)) {
        return input.menu_key_action;
    }
    if (input.escape && !previous_input_.escape) {
        return MenuKeyAction::Escape;
    }
    if ((input.move_up && !previous_input_.move_up) || (input.move_left && !previous_input_.move_left) ||
        input.menu_navigation_up_repeat) {
        return MenuKeyAction::Up;
    }
    if ((input.move_down && !previous_input_.move_down) || (input.move_right && !previous_input_.move_right) ||
        input.menu_navigation_down_repeat) {
        return MenuKeyAction::Down;
    }
    if ((input.start && !previous_input_.start) || (input.accept && !previous_input_.accept) ||
        (!enhanced_input_debounce_ && input.start_repeat)) {
        return MenuKeyAction::Enter;
    }
    if (input.fire && !previous_input_.fire && !input.mouse_primary) {
        return MenuKeyAction::Space;
    }
    if (input.decline && !previous_input_.decline) {
        return MenuKeyAction::Decline;
    }
    return MenuKeyAction::None;
}

bool Game::update_confirmation_prompt(const InputState& input)
{
    bool confirm_edge = false;
    bool cancel_edge = false;
    if (prompt_mouse_response_ != PromptMouseResponse::None) {
        if (any_mouse_button(input)) {
            return true;
        }
        const auto response = std::exchange(prompt_mouse_response_, PromptMouseResponse::None);
        confirm_edge = response == PromptMouseResponse::Confirm;
        cancel_edge = response == PromptMouseResponse::Cancel;
    } else {
        if (input.mouse_primary) {
            prompt_mouse_response_ = PromptMouseResponse::Confirm;
            return true;
        }
        if (input.mouse_secondary) {
            prompt_mouse_response_ = PromptMouseResponse::Cancel;
            return true;
        }
        const auto action = std::exchange(control_panel_key_latch_, MenuKeyAction::None);
        confirm_edge = action == MenuKeyAction::Enter || action == MenuKeyAction::Confirm ||
            (input.gamepad_accept && !previous_input_.gamepad_accept);
        cancel_edge = action == MenuKeyAction::Escape || action == MenuKeyAction::Decline ||
            (input.gamepad_back && !previous_input_.gamepad_back);
    }

    if (confirm_edge) {
        const auto action = world_.confirmation_prompt;
        if (action == ConfirmationPromptAction::QuitToDos ||
            (enhanced_input_debounce_ && world_.control_panel_dirty)) {
            if (save_high_scores()) {
                world_.control_panel_dirty = false;
            }
        }
        world_.confirmation_prompt = ConfirmationPromptAction::None;
        if (action == ConfirmationPromptAction::QuitToDos) {
            if (shareware_edition_) {
                begin_shareware_ending(true);
            } else {
                world_.quit_requested = true;
            }
        } else if (action == ConfirmationPromptAction::ReturnToTitle) {
            reset_to_title();
        }
        return true;
    }

    if (cancel_edge) {
        world_.confirmation_prompt = ConfirmationPromptAction::None;
        world_.control_panel_row = control_panel_prompt_return_row_;
        return true;
    }

    return true;
}

void Game::update_title(const InputState& input)
{
    if (start_or_fire_pressed(input)) {
        enter_control_panel();
        return;
    }

    const bool advance_attract = input.menu && !previous_input_.menu;
    if (!advance_attract && --world_.title_frames_remaining > 0) {
        return;
    }

    if (!world_.credits_seen) {
        world_.credits_seen = true;
        world_.screen = Screen::Credits;
        world_.credits_frames_remaining = kCreditsScreenFrames;
    } else {
        world_.screen = Screen::AttractInterlude;
        world_.attract_interlude_frames_remaining = kAttractInterludeScreenFrames;
    }
    world_.screen_fade_frames_remaining = kScreenFadeFrames;
}

void Game::update_credits(const InputState& input)
{
    if (start_or_fire_pressed(input)) {
        enter_control_panel();
        return;
    }

    const bool advance_attract = input.menu && !previous_input_.menu;
    if (!advance_attract && --world_.credits_frames_remaining > 0) {
        return;
    }

    world_.screen = Screen::AttractInterlude;
    world_.attract_interlude_frames_remaining = kAttractInterludeScreenFrames;
    world_.screen_fade_frames_remaining = kScreenFadeFrames;
}

void Game::update_attract_interlude(const InputState& input)
{
    if (start_or_fire_pressed(input)) {
        enter_control_panel();
        return;
    }

    const bool advance_attract = input.menu && !previous_input_.menu;
    if (!advance_attract && --world_.attract_interlude_frames_remaining > 0) {
        return;
    }

    world_.screen = Screen::HighScores;
    world_.high_score_frames_remaining = kHighScoreAttractScreenFrames;
    world_.active_high_score_index = -1;
    world_.high_score_checked = false;
    world_.screen_fade_frames_remaining = kScreenFadeFrames;
}

void Game::dispatch_control_panel_action(int row)
{
    switch (row) {
    case 3:
        if (world_.control_panel_from_gameplay) {
            world_.confirmation_prompt = ConfirmationPromptAction::ReturnToTitle;
            return;
        }
        if (enhanced_input_debounce_ && world_.control_panel_dirty) {
            if (save_high_scores()) {
                world_.control_panel_dirty = false;
            }
        }
        start_new_game();
        return;
    case 4:
        if (enhanced_input_debounce_ && world_.control_panel_dirty) {
            if (save_high_scores()) {
                world_.control_panel_dirty = false;
            }
        }
        world_.screen = world_.control_panel_from_gameplay ? Screen::Gameplay : Screen::Title;
        if (world_.screen == Screen::Title) {
            world_.title_frames_remaining = kTitleScreenFrames;
        } else {
            fire_release_required_ = enhanced_input_debounce_;
        }
        world_.screen_fade_frames_remaining = kScreenFadeFrames;
        world_.control_panel_from_gameplay = false;
        return;
    case 5:
        world_.confirmation_prompt = ConfirmationPromptAction::QuitToDos;
        return;
    default:
        return;
    }
}

void Game::update_control_panel(const InputState& input)
{
    constexpr int kRowCount = 6;
    constexpr int kFirstActionRow = 3;
    // Row N (0..2) → audio_config_words index N + 3 per SaveConfigAndHighScores.
    constexpr int kRowConfigBase = 3;

    if (world_.control_panel_pending_option_row >= 0) {
        if (any_mouse_button(input)) {
            return;
        }
        world_.audio_config_words[kRowConfigBase + world_.control_panel_pending_option_row] =
            world_.control_panel_pending_option_value;
        world_.control_panel_pending_option_row = -1;
        return;
    }

    if (world_.control_panel_pending_action_row >= kFirstActionRow) {
        if (world_.control_panel_action_frames_remaining > 0 &&
            --world_.control_panel_action_frames_remaining > 0) {
            return;
        }
        if (any_mouse_button(input)) {
            return;
        }
        const int pending_row = world_.control_panel_pending_action_row;
        world_.control_panel_pending_action_row = -1;
        world_.control_panel_action_frames_remaining = 0;
        dispatch_control_panel_action(pending_row);
        return;
    }

    // 0dbd..0e2f uses table Y bounds, independent of X and the saved mouse preference.
    if (input.mouse_moved && std::isfinite(input.mouse_y) &&
        world_.audio_config_words[config_internals::kMouseHardware] != 0 &&
        std::floor(input.mouse_y) != control_panel_mouse_y_) {
        const float raw_y = std::floor(input.mouse_y);
        const float y = raw_y < kControlPanelHitTops.front() ? kControlPanelHitTops.front() :
            raw_y > kControlPanelHitTops.back() + kControlPanelHitHeight ? kControlPanelHitTops.back() : raw_y;
        for (int row = 0; row < kRowCount; ++row) {
            if (y < kControlPanelHitTops[row] || y >= kControlPanelHitTops[row] + kControlPanelHitHeight ||
                row == world_.control_panel_row || control_panel_choice_count(world_, row) == 0) {
                continue;
            }
            world_.control_panel_row = row;
            control_panel_mouse_y_ = raw_y;
            world_.control_panel_initial_hint = false;
            world_.control_panel_option_feedback = false;
            return;
        }
    }

    // RunControlPanelInputLoop (1480:0d87): UP/LEFT navigate cursor up,
    // DOWN/RIGHT navigate cursor down.
    const bool mouse_activate = any_mouse_button(input) &&
        world_.audio_config_words[config_internals::kMouseHardware] != 0;
    const auto key_action = mouse_activate ? MenuKeyAction::None :
        std::exchange(control_panel_key_latch_, MenuKeyAction::None);
    if (key_action == MenuKeyAction::Escape) {
        world_.control_panel_initial_hint = false;
        world_.control_panel_option_feedback = false;
        control_panel_prompt_return_row_ = world_.control_panel_row;
        world_.control_panel_row = 5;
        world_.control_panel_pending_action_row = 5;
        world_.control_panel_action_frames_remaining = 15;
        return;
    }
    const bool nav_up = key_action == MenuKeyAction::Up;
    const bool nav_down = key_action == MenuKeyAction::Down;
    if (nav_up &&
        world_.control_panel_row > 0) {
        world_.control_panel_row = control_panel_next_selectable_row(world_, world_.control_panel_row, -1);
        control_panel_mouse_y_ = static_cast<float>(kControlPanelHitTops[world_.control_panel_row]);
        world_.control_panel_initial_hint = false;
        world_.control_panel_option_feedback = false;
    }
    if (nav_down &&
        world_.control_panel_row + 1 < kRowCount) {
        world_.control_panel_row = control_panel_next_selectable_row(world_, world_.control_panel_row, 1);
        control_panel_mouse_y_ = static_cast<float>(kControlPanelHitTops[world_.control_panel_row]);
        world_.control_panel_initial_hint = false;
        world_.control_panel_option_feedback = false;
    }

    // RunControlPanelLoop (1480:0f7c): Enter/Space on audio rows runs
    // (current + 1) % max_count and marks dirty; on action rows it activates.
    const bool activate_edge = mouse_activate || key_action == MenuKeyAction::Enter ||
        key_action == MenuKeyAction::Space;

    const int row = world_.control_panel_row;
    if (activate_edge) {
        world_.control_panel_initial_hint = false;
        if (row < kFirstActionRow) {
            const int choices = control_panel_choice_count(world_, row);
            if (choices > 0) {
                auto& word = world_.audio_config_words[static_cast<std::size_t>(kRowConfigBase + row)];
                auto next = static_cast<std::uint16_t>((word + 1) % choices);
                while (row == 0 &&
                       ((next == kSoundSourceMode && world_.audio_config_words[config_internals::kSourceHardware] == 0) ||
                        (next == kSoundBlasterMode && world_.audio_config_words[config_internals::kBlasterHardware] == 0))) {
                    next = static_cast<std::uint16_t>((next + 1) % choices);
                }
                if (any_mouse_button(input)) {
                    world_.control_panel_pending_option_row = row;
                    world_.control_panel_pending_option_value = next;
                } else {
                    word = next;
                }
                world_.control_panel_dirty = true;
                world_.control_panel_option_feedback = true;
            }
        } else {
            world_.control_panel_option_feedback = false;
            control_panel_prompt_return_row_ = row;
            world_.control_panel_pending_action_row = row;
            world_.control_panel_action_frames_remaining = 15;
            return;
        }
    }

    if ((enhanced_input_debounce_ && input.menu && !previous_input_.menu) ||
        (input.gamepad_back && !previous_input_.gamepad_back)) {
        world_.control_panel_initial_hint = false;
        if (enhanced_input_debounce_ && world_.control_panel_dirty) {
            if (save_high_scores()) {
                world_.control_panel_dirty = false;
            }
        }
        world_.screen = world_.control_panel_from_gameplay ? Screen::Gameplay : Screen::Title;
        if (world_.screen == Screen::Title) {
            world_.title_frames_remaining = kTitleScreenFrames;
        } else {
            fire_release_required_ = enhanced_input_debounce_;
        }
        world_.screen_fade_frames_remaining = kScreenFadeFrames;
        world_.control_panel_from_gameplay = false;
    }
}

void Game::update_gameplay_wrapper(const InputState& input)
{
    if (world_.level_banner_frames_remaining > 0) {
        --world_.level_banner_frames_remaining;
    }
    process_player_input_and_modal_state(input);
    update_active_objects();
    check_object_collisions();
    evaluate_level_progression();

    if (world_.gameplay_state == GameplayState::GameOver) {
        if (world_.death_flash_frames_remaining > 0) {
            update_death_flash();
            return;
        }
        if (world_.bunker_explosion_frames_remaining > 0) {
            --world_.bunker_explosion_frames_remaining;
            if (world_.bunker_explosion_frames_remaining == 0 && world_.bunker_assault_entries >= kLandedInvaderLimit) {
                world_.bunker_white_flag = true;
                world_.bunker_terminal_explosion_frames_total = 0;
                world_.screen = Screen::GameOver;
                world_.game_over_frames_remaining = kOverrunGameOverTicks;
                return;
            }
            return;
        }
    }

    if (world_.gameplay_state == GameplayState::LevelComplete ||
        world_.gameplay_state == GameplayState::GameOver ||
        world_.gameplay_state == GameplayState::FinaleComplete) {
        if (world_.gameplay_state == GameplayState::LevelComplete) {
            // 32d5 presents the final wave-clear page before 3d32 enters intermission.
            before_owner_page_ = world_;
        }
        handle_level_transition_or_intermission();
    }
}

void Game::update_death_flash()
{
    if (!world_.fatal_retained_page && world_.frame_tick != world_.transition_started_tick) {
        // 00f0 runs after the fatal actor page, before loading terminal assets.
        // Retained video must not require keeping actor sound owners alive.
        world_.fatal_retained_page = RetainedPage {world_.objects, world_.transition_started_tick};
        // 5008 calls 1d6c:05e4 after actor cleanup, stopping unowned SFX too.
        world_.reset_sound_effects = true;
        for (auto& object : world_.objects) {
            if (object.active && object.type != ObjectType::PlayerCannon &&
                object.type != ObjectType::LandedInvader) {
                object.active = false;
                object.pending_destroy = false;
                object.sound_id = 0;
            }
        }
    }
    if (world_.death_flash_frames_remaining > 0) {
        --world_.death_flash_frames_remaining;
        if (world_.death_flash_frames_remaining == 1) {
            // 5008:5143 starts 369 before the final 09d/09b palette uploads,
            // not at the first rubble flip. Keep this event out of the handoff.
            play_sound(kSoundTerminalExplosion);
        } else if (world_.death_flash_frames_remaining == 0) {
            // 5008:516e restores 09b on the retained page before the next flip.
            world_.death_palette_restored = true;
        }
        return;
    }
    world_.death_palette_restored = false;
    world_.fatal_retained_page.reset();
    world_.transition_started_tick = world_.frame_tick;
    world_.screen = Screen::GameOver;
    world_.game_over_frames_remaining = kOverrunGameOverTicks;
}

void Game::update_intermission(const InputState& input)
{
    const bool live_flyby = world_.no_survivor_intermission_active && std::any_of(
        world_.objects.begin(), world_.objects.end(), [](const Object& object) {
            return object.active && !object.pending_destroy && object.type == ObjectType::Presenter &&
                   object.sprite_id == internals::kBannerFlybyAircraftSprite;
        });
    if (world_.survivor_live) {
        if (world_.survivor_live->owner_return_ready) {
            advance_survivor_phase();
        }
        update_active_objects();
        check_object_collisions();
        ++world_.survivor_intermission_frame;
        --world_.intermission_frames_remaining;
        return;
    } else if (live_flyby) {
        // 3d32 runs 32d5 until 554f post-decrements the old counter of zero.
        update_active_objects();
        check_object_collisions();
        ++world_.survivor_intermission_frame;
        --world_.intermission_frames_remaining;
        if (world_.gameplay_state == GameplayState::Active) {
            return;
        }
        // 32d5 presents the final page before 3d32 retires its aircraft.
        before_owner_page_ = world_;
        for (std::size_t slot = 0; slot < world_.objects.size(); ++slot) {
            const auto& object = world_.objects[slot];
            if (object.active && object.type == ObjectType::Presenter &&
                object.sprite_id == internals::kBannerFlybyAircraftSprite) {
                retire_object(slot);
                break;
            }
        }
    } else {
        if (world_.survivor_intermission_active || world_.no_survivor_intermission_active) {
            ++world_.survivor_intermission_frame;
            update_survivor_intermission_presenter();
        } else if (world_.milestone_intermission == MilestoneIntermission::Level4Pizza) {
            if (world_.intermission_frames_remaining <= 0) {
                finish_intermission();
                return;
            }
            if (world_.intermission_frames_remaining > kPizzaPresenterUpdates) {
                // Diagnostic captures retain a long lifetime but still advance the presenter.
                ++world_.milestone_intermission_frame;
            } else {
                world_.milestone_intermission_frame = static_cast<std::uint32_t>(
                    kPizzaPresenterUpdates - world_.intermission_frames_remaining);
            }
            update_milestone_intermission_audio();
            if (!world_.pizza_all_draws_diagnostic &&
                (world_.milestone_intermission_frame == kPizzaLowerDoorwayUpdate ||
                 world_.milestone_intermission_frame == kPizzaRaiseDoorwayUpdate)) {
                // These original doorway callbacks draw once without advancing their update owner.
                before_owner_page_ = world_;
                before_owner_page_->pizza_all_draws_diagnostic = true;
                before_owner_page_->milestone_intermission_frame =
                    original_pizza_presenter_frame(world_).draw_index - 1;
            }
            --world_.intermission_frames_remaining;
            return;
        } else if (world_.milestone_intermission != MilestoneIntermission::None) {
            ++world_.milestone_intermission_frame;
            update_milestone_intermission_audio();
        }

        if (--world_.intermission_frames_remaining > 0) {
            return;
        }
    }

    finish_intermission();
}

void Game::finish_intermission()
{
    if (world_.milestone_intermission == MilestoneIntermission::None) {
        if (world_.current_level == 3) {
            begin_milestone_intermission(MilestoneIntermission::Level4Pizza);
            return;
        }
        if (world_.current_level == 7) {
            begin_milestone_intermission(MilestoneIntermission::Level8Helicopter);
            return;
        }
    }

    if (shareware_edition_ && world_.current_level == 3 &&
        world_.milestone_intermission == MilestoneIntermission::Level4Pizza) {
        begin_shareware_ending(false);
        return;
    }

    advance_level();
}

void Game::begin_live_survivor_intermission()
{
    for (std::size_t slot = 0; slot < world_.objects.size() &&
         slot < world_.object_highwater.value_or(world_.objects.size()); ++slot) {
        auto& object = world_.objects[slot];
        if (object.active && object.type == ObjectType::LandedInvader) {
            object.sprite_id = 0x2ca;
        } else if (!object.active || object.type != ObjectType::PlayerCannon) {
            retire_object(slot);
        }
    }
    Object controller {};
    controller.active = true;
    controller.type = ObjectType::Presenter;
    controller.position = {264, 34};
    controller.extent = {};
    const auto slot = store_object(controller);
    world_.survivor_live = SurvivorLiveState {};
    world_.survivor_live->controller_slot = slot;
    survivor_scan_slot_ = 0;
    survivor_pickup_slot_.reset();
    world_.transition_armed = false;
    world_.waves_exhausted = false;
    move_survivor_controller(survivor_timing::kIngressTargets[0], 0x28e);
}

void Game::move_survivor_controller(WorldPosition target, int sprite)
{
    auto& scene = *world_.survivor_live;
    auto& controller = world_.objects.at(scene.controller_slot);
    survivor_target_ = target;
    constexpr double fixed_scale = 65536.0;
    const auto velocity = [](double from, double to) {
        const auto delta = static_cast<std::int32_t>(std::llround((to - from) * fixed_scale));
        return static_cast<float>(delta / survivor_timing::kMovementCounter) / fixed_scale;
    };
    controller.velocity = {static_cast<float>(velocity(controller.position.x, target.x)),
                           static_cast<float>(velocity(controller.position.y, target.y))};
    controller.frame = survivor_timing::kMovementCounter;
    controller.sprite_id = sprite;
    scene.animation_complete = false;
    scene.owner_return_ready = false;
    world_.gameplay_state = GameplayState::Active;
}

void Game::select_survivor_pickup()
{
    auto& scene = *world_.survivor_live;
    while (survivor_scan_slot_ < world_.objects.size() &&
           survivor_scan_slot_ < world_.object_highwater.value_or(world_.objects.size())) {
        const auto slot = survivor_scan_slot_++;
        const auto& trooper = world_.objects[slot];
        if (!trooper.active || trooper.type != ObjectType::LandedInvader) {
            continue;
        }
        survivor_pickup_slot_ = slot;
        scene.phase = SurvivorPhase::PickupMove;
        // 3a3b caches the integer words before the blocking approach.
        move_survivor_controller({std::floor(trooper.position.x) - 10,
                                  std::floor(trooper.position.y) - 28}, 0xffff);
        return;
    }
    scene.phase = SurvivorPhase::Egress;
    scene.leg = 0;
    move_survivor_controller(survivor_timing::kEgressTargets[0], 0x292);
}

void Game::advance_survivor_phase()
{
    auto& scene = *world_.survivor_live;
    scene.owner_return_ready = false;
    auto& controller = world_.objects.at(scene.controller_slot);
    if (scene.phase == SurvivorPhase::Ingress || scene.phase == SurvivorPhase::PickupMove ||
        scene.phase == SurvivorPhase::Egress) {
        controller.position = survivor_target_;
    }
    switch (scene.phase) {
    case SurvivorPhase::Ingress:
        if (++scene.leg < 5) {
            move_survivor_controller(survivor_timing::kIngressTargets[scene.leg], 0x28e + scene.leg);
        } else {
            select_survivor_pickup();
        }
        break;
    case SurvivorPhase::PickupMove: {
        const bool rare = original_random_bounded(world_.gameplay_rng_seed, 15) == 0;
        scene.phase = rare ? SurvivorPhase::Rare : SurvivorPhase::Common;
        scene.animation_complete = false;
        controller.frame = 0;
        controller.timer = 0;
        controller.sound_id = rare ? 0x361 : 0x363;
        if (!rare) {
            ++world_.finale_budget_seed;
        }
        retire_object(*survivor_pickup_slot_);
        survivor_pickup_slot_.reset();
        world_.gameplay_state = GameplayState::Active;
        break;
    }
    case SurvivorPhase::Rare:
        controller.sound_id = 0;
        controller.frame = 0;
        controller.timer = 0;
        scene.phase = SurvivorPhase::Aftermath;
        scene.animation_complete = false;
        world_.gameplay_state = GameplayState::Active;
        break;
    case SurvivorPhase::Common:
        controller.sound_id = 0;
        select_survivor_pickup();
        break;
    case SurvivorPhase::Aftermath:
        select_survivor_pickup();
        break;
    case SurvivorPhase::Egress:
        ++scene.leg;
        move_survivor_controller(survivor_timing::kEgressTargets[scene.leg], 0x292 - scene.leg);
        break;
    }
}

void Game::update_survivor_controller(Object& controller)
{
    auto& scene = *world_.survivor_live;
    if (scene.phase == SurvivorPhase::Ingress || scene.phase == SurvivorPhase::PickupMove ||
        scene.phase == SurvivorPhase::Egress) {
        controller.position.x += controller.velocity.x;
        controller.position.y += controller.velocity.y;
        if (controller.frame-- < 1) {
            world_.gameplay_state = GameplayState::ScriptedSequence;
        }
        return;
    }
    const auto old_timer = controller.timer++;
    if (scene.phase == SurvivorPhase::Aftermath) {
        // After counter 6 the adjacent original word is 0x105. At most five
        // updates remain in this batch, so only its retained timer advances.
        if (controller.frame > 5) {
            return;
        }
        if (old_timer < survivor_timing::kAftermathWaits.at(controller.frame)) {
            return;
        }
        controller.timer = 0;
        if (++controller.frame > 5) {
            scene.animation_complete = true;
            world_.gameplay_state = GameplayState::ScriptedSequence;
        } else if (controller.frame % 2 == 1) {
            play_sound(static_cast<std::uint16_t>(0x35b + controller.frame - 1));
        }
        return;
    }
    const bool common = scene.phase == SurvivorPhase::Common;
    if (old_timer > (common ? 6u : 5u)) {
        controller.timer = 0;
        if (++controller.frame > (common ? 20 : 16)) {
            scene.animation_complete = true;
            world_.gameplay_state = GameplayState::ScriptedSequence;
        }
    }
}

void Game::update_survivor_intermission_presenter()
{
    if (world_.no_survivor_intermission_active) {
        return;
    }
    const auto frame = original_survivor_presenter_frame(world_);
    world_.gameplay_rng_seed = frame.random_seed;
    if (frame.successful_pickup) {
        ++world_.finale_budget_seed;
    }
    for (const auto& command : frame.sounds) {
        switch (command.action) {
        case SurvivorSoundAction::Bind: bind_presenter_sound(command.sound); break;
        case SurvivorSoundAction::Stop: stop_presenter_sound(); break;
        case SurvivorSoundAction::Play: play_sound(command.sound); break;
        }
    }
}

void Game::begin_milestone_intermission(MilestoneIntermission milestone)
{
    if (world_.survivor_intermission_active) {
        retire_survivor_handoff_objects(world_);
    }
    world_.survivor_intermission_active = false;
    world_.survivor_native_presenter = false;
    world_.survivor_live.reset();
    world_.no_survivor_intermission_active = false;
    world_.milestone_intermission = milestone;
    world_.milestone_intermission_frame = 0;
    world_.milestone_timer_origin = world_.frame_tick;
    world_.pizza_all_draws_diagnostic = false;
    world_.bunker_walk_sound_toggle = false;

    switch (milestone) {
    case MilestoneIntermission::Level4Pizza:
        // Inclusive door timers and the retained 0xffffffff walk timer add
        // seven updates to the former estimate. Two extra draws take no update.
        world_.intermission_frames_remaining = original_timer_ticks_to_video_frames(kPizzaPresenterUpdates);
        bind_presenter_sound(kSoundPizzaVehicle);
        break;
    case MilestoneIntermission::Level8Helicopter:
        // Inclusive door callbacks and the retained 558c timer are significant.
        world_.intermission_frames_remaining = original_timer_ticks_to_video_frames(1675);
        bind_presenter_sound(kSoundHelicopterFlight);
        break;
    case MilestoneIntermission::None:
        break;
    }
}

void Game::update_milestone_intermission_audio()
{

    if (world_.milestone_intermission == MilestoneIntermission::Level4Pizza) {
        const auto state = original_pizza_presenter_frame(world_);
        if (state.stop_vehicle_sound) stop_presenter_sound();
        if (state.start_vehicle_sound) bind_presenter_sound(kSoundPizzaVehicle);
        for (const auto sound : state.sounds) play_sound(sound);
        if (state.walking_sound) {
            play_sound(world_.bunker_walk_sound_toggle ? kSoundBunkerWalkB : kSoundBunkerWalkA);
            world_.bunker_walk_sound_toggle = !world_.bunker_walk_sound_toggle;
        }
        return;
    }

    if (world_.milestone_intermission == MilestoneIntermission::Level8Helicopter) {
        const auto state = original_helicopter_presenter_frame(world_);
        if (!state.active) {
            stop_presenter_sound();
            return;
        }
        if (state.park_vehicle_sound) {
            stop_presenter_sound();
            if (audio_internals::composite_effects_enabled(world_)) bind_presenter_sound(kSoundHelicopterParked);
        }
        if (state.start_vehicle_sound) {
            stop_presenter_sound();
            bind_presenter_sound(kSoundHelicopterFlight);
        }
        for (const auto sound : state.sounds) play_sound(sound);
        if (state.walking_sound) {
            play_sound(world_.bunker_walk_sound_toggle ? kSoundBunkerWalkB : kSoundBunkerWalkA);
            world_.bunker_walk_sound_toggle = !world_.bunker_walk_sound_toggle;
        }
    }
}

void Game::update_game_over(const InputState& input)
{
    const auto timing = presenter_timing::terminal_audio_timing(world_);
    const bool terminal_sequence_active =
        world_.overrun_interrupt_residue ||
        (world_.bunker_white_flag &&
         world_.bunker_assault_entries >= kLandedInvaderLimit);
    if (terminal_sequence_active) {
        const auto elapsed = world_.frame_tick - world_.transition_started_tick;
        if (elapsed == timing.prelude) {
            play_sound(kSoundTerminalPrelude);
        }
        for (const auto pulse_frame : timing.pulses) {
            if (elapsed == pulse_frame) {
                play_sound(kSoundTerminalPulse);
            }
        }
        if (elapsed == timing.explosion) {
            play_sound(kSoundTerminalExplosion);
        }
    }

    if (world_.overrun_interrupt_residue) {
        const auto elapsed = world_.frame_tick - world_.transition_started_tick;
        if (elapsed < timing.retained) {
            if (world_.game_over_frames_remaining > 1) {
                --world_.game_over_frames_remaining;
            }
            return;
        }

        world_.overrun_interrupt_residue = false;
        world_.overrun_retained_page.reset();
        world_.bunker_assault_active = false;
        world_.bunker_assault_entries = kLandedInvaderLimit;
        world_.bunker_special_effect_frames_remaining = 0;
        world_.bunker_explosion_frames_remaining = 0;
        world_.bunker_terminal_explosion_frames_total = 0;
        world_.bunker_white_flag = true;
    }

    if (world_.player_dead && !world_.overrun_interrupt_residue) {
        const auto elapsed = world_.frame_tick - world_.transition_started_tick;
        const auto retained_frames =
            world_.bunker_white_flag && world_.bunker_assault_entries >= kLandedInvaderLimit
                ? timing.retained
                : 0u;
        if (elapsed >= retained_frames) {
            const auto presenter_frame = elapsed - retained_frames;
            const auto explosion_frames = presenter_timing::kTerminalExplosionVideoFrames;
            const auto flag_wave_start = presenter_timing::kTerminalFlagWaveStartVideoFrame;

            // RunGameplayWrapper resets the original input latch after the
            // destruction strips, then raises the flag and waits on any key.
            if (presenter_frame >= explosion_frames && any_keyboard_pressed(input)) {
                world_.game_over_input_latched = true;
            }
            if (presenter_frame >= flag_wave_start) {
                if (world_.game_over_input_latched &&
                    presenter_timing::terminal_exit_poll_frame(presenter_frame)) {
                    advance_from_game_over();
                }
                return;
            }
        }
    } else if (start_or_fire_pressed(input)) {
        advance_from_game_over();
        return;
    }

    if (world_.game_over_frames_remaining > 1) {
        --world_.game_over_frames_remaining;
    }
}

void Game::update_high_score_entry(const InputState& input)
{
    if (world_.active_high_score_index < 0 ||
        world_.active_high_score_index >= static_cast<int>(world_.high_scores.size())) {
        world_.screen = Screen::HighScores;
        world_.high_score_frames_remaining = 420;
        return;
    }

    auto& entry = world_.high_scores[static_cast<std::size_t>(world_.active_high_score_index)];

    const auto finalize_entry = [&]() {
        entry.name =
            finalized_high_score_name(std::move(entry.name), world_.fast_shots_enabled);
        entry.highlighted = false;
        world_.active_high_score_index = -1;
        world_.high_score_gamepad_active = false;
        if (enhanced_input_debounce_) {
            save_high_scores();
        }
        world_.screen = Screen::HighScores;
        world_.high_score_frames_remaining = 420;
    };

    if (input.escape && !previous_input_.escape) {
        entry.name.clear();
        finalize_entry();
        return;
    }

    const bool gamepad_back_edge =
        input.gamepad_back && !previous_input_.gamepad_back;
    if ((input.name_delete || input.backspace || gamepad_back_edge) &&
        !entry.name.empty()) {
        entry.name.pop_back();
    }

    const bool gamepad_up_edge =
        input.gamepad_name_up && !previous_input_.gamepad_name_up;
    const bool gamepad_down_edge =
        input.gamepad_name_down && !previous_input_.gamepad_name_down;
    if (gamepad_up_edge || gamepad_down_edge) {
        world_.high_score_gamepad_active = true;
        world_.high_score_gamepad_character = cycle_gamepad_high_score_character(
            world_.high_score_gamepad_character,
            gamepad_up_edge ? 1 : -1);
    }

    if (input.gamepad_accept && !previous_input_.gamepad_accept) {
        world_.high_score_gamepad_active = true;
        append_high_score_name_character(
            entry.name, world_.high_score_gamepad_character);
    }

    for (char c : input.text_input) {
        append_high_score_name_character(entry.name, c);
    }

    if ((input.name_submit && !previous_input_.name_submit) ||
        (input.start && !previous_input_.start) ||
        (input.gamepad_start && !previous_input_.gamepad_start)) {
        finalize_entry();
    }
}

void Game::update_high_scores(const InputState& input)
{
    if (start_or_fire_pressed(input)) {
        enter_control_panel();
        previous_input_ = input;
        return;
    }

    const bool advance_attract = input.menu && !previous_input_.menu;
    if (!advance_attract && --world_.high_score_frames_remaining > 0) {
        return;
    }

    reset_to_title();
    previous_input_ = {};
}

void Game::update_finale(const InputState& input)
{
    if (start_or_fire_pressed(input)) {
        advance_from_game_over();
        return;
    }

    ++world_.finale_presenter_frame;
    update_finale_presenter_audio();
    if (--world_.game_over_frames_remaining > 0) {
        return;
    }

    advance_from_game_over();
}

void Game::update_finale_presenter_audio()
{
    const auto frame = world_.finale_presenter_frame;
    if (frame == 81) play_sound(kSoundBunkerSpecialSetup);
    if (frame == 144) play_sound(kSoundBunkerAssault);
    if (original_finale_presenter_frame(world_).walking_sound) {
        // 404a calls 4021 on every draw with animation % 4 == 0 while moving.
        play_sound(world_.bunker_walk_sound_toggle ? kSoundBunkerWalkB : kSoundBunkerWalkA);
        world_.bunker_walk_sound_toggle = !world_.bunker_walk_sound_toggle;
    }
    if (frame == 591) {
        play_sound(kSoundVehicleStop);
    }
    const bool composite = audio_internals::composite_effects_enabled(world_);
    if (frame == (composite ? 591u : 600u)) play_sound(kSoundFinaleDeparture);
    if (frame == (composite ? 627u : 741u)) play_sound(kSoundVehicleStart);
    if (!composite && frame == presenter_timing::finale_egress_start(world_)) {
        bind_presenter_sound(kSoundFinaleController);
    }
    const auto particle_start = presenter_timing::finale_particle_start(world_);
    if (frame == particle_start) stop_presenter_sound();
    if (frame <= particle_start) return;

    const auto tick = frame - particle_start;
    const auto particles = original_finale_particle_frame(world_.finale_particle_rng_seed, tick);
    world_.gameplay_rng_seed = particles.random_seed;
    while (world_.finale_particle_sounds_spawned < particles.spawned) {
        play_sound(kSoundFinaleProjectile);
        ++world_.finale_particle_sounds_spawned;
    }
    while (world_.finale_particle_sounds_burst < particles.spawned) {
        const auto burst = particles.burst_ticks[world_.finale_particle_sounds_burst];
        if (burst == 0 || tick < burst) break;
        play_sound(kSoundFinaleBurst);
        ++world_.finale_particle_sounds_burst;
    }
}

void Game::begin_shareware_ending(bool exit_after)
{
    stop_presenter_sound();
    for (auto& object : world_.objects) {
        object.sound_id = 0;
    }
    world_.screen = Screen::SharewareEnding;
    world_.screen_fade_frames_remaining = 0;
    world_.shareware_ending_tick = 0;
    world_.shareware_ending_frame = 0;
    world_.shareware_ending_audio_cue = 0;
    world_.shareware_ending_waiting_for_input = false;
    world_.shareware_ending_exit_after = exit_after;
    world_.quit_requested = false;
    emit_shareware_ending_cues_for_frame(0);
}

void Game::update_shareware_ending(const InputState& input)
{
    // Diagnostic entry sets the presenter fields directly, so emit the same
    // frame-zero cues that begin_shareware_ending() emits in normal play.
    if (world_.shareware_ending_tick == 0 &&
        world_.shareware_ending_audio_cue == 0) {
        emit_shareware_ending_cues_for_frame(0);
    }

    if (world_.shareware_ending_waiting_for_input) {
        if (!any_keyboard_pressed(input)) {
            return;
        }
        if (world_.shareware_ending_exit_after) {
            world_.quit_requested = true;
        } else {
            advance_from_game_over();
        }
        return;
    }

    ++world_.shareware_ending_tick;
    constexpr auto animation_ticks =
        static_cast<std::uint32_t>(shareware_ending::kFrameCount) *
        shareware_ending::kTimerTicksPerFrame;
    if (world_.shareware_ending_tick >= animation_ticks) {
        world_.shareware_ending_waiting_for_input = true;
        return;
    }

    const auto frame = static_cast<std::uint16_t>(
        world_.shareware_ending_tick / shareware_ending::kTimerTicksPerFrame);
    if (frame == world_.shareware_ending_frame) {
        return;
    }
    world_.shareware_ending_frame = frame;
    emit_shareware_ending_cues_for_frame(frame);
}

void Game::emit_shareware_ending_cues_for_frame(std::uint16_t frame)
{
    while (world_.shareware_ending_audio_cue < shareware_ending::kAudioCues.size()) {
        const auto& cue =
            shareware_ending::kAudioCues[world_.shareware_ending_audio_cue];
        if (cue.frame_threshold > static_cast<std::uint16_t>(frame + 1)) {
            break;
        }
        if (cue.start) {
            play_sound(cue.logical_sound_id);
        } else {
            stop_sound(cue.logical_sound_id);
        }
        ++world_.shareware_ending_audio_cue;
    }
}

void Game::start_new_game()
{
    world_.scores = {};
    world_.finale_budget_seed = 0;
    world_.high_score_checked = false;
    world_.active_high_score_index = -1;
    world_.high_score_gamepad_character = 'A';
    world_.high_score_gamepad_active = false;
    for (auto& entry : world_.high_scores) {
        entry.highlighted = false;
    }
    start_level(0);
}

void Game::start_level(std::uint16_t level)
{
    world_.screen = Screen::Gameplay;
    world_.screen_fade_frames_remaining = 0;
    world_.gameplay_state = GameplayState::Active;
    world_.current_level = level;
    world_.waves_exhausted = false;
    world_.player_dead = false;
    world_.death_flash_frames_remaining = 0;
    world_.death_palette_restored = false;
    world_.fatal_retained_page.reset();
    world_.overrun_retained_page.reset();
    world_.transition_armed = false;
    world_.transition_freeze = false;
    world_.bunker_overrun_pending = false;
    world_.bunker_assault_active = false;
    world_.overrun_interrupt_residue = false;
    world_.survivor_intermission_active = false;
    world_.no_survivor_intermission_active = false;
    world_.survivor_intermission_failure_mask = 0;
    world_.survivor_intermission_frame = 0;
    world_.survivor_intermission_timer_origin = 0;
    world_.survivor_intermission_rng_seed = 1;
    world_.survivor_native_presenter = false;
    world_.survivor_live.reset();
    survivor_pickup_slot_.reset();
    world_.survivor_presenter_update_batches.clear();
    world_.milestone_intermission = MilestoneIntermission::None;
    world_.milestone_intermission_frame = 0;
    world_.finale_presenter_frame = 0;
    world_.finale_particle_sounds_spawned = 0;
    world_.finale_particle_sounds_burst = 0;
    world_.shareware_ending_tick = 0;
    world_.shareware_ending_frame = 0;
    world_.shareware_ending_audio_cue = 0;
    world_.shareware_ending_waiting_for_input = false;
    world_.shareware_ending_exit_after = false;
    world_.transition_started_tick = 0;
    world_.bunker_assault_step_tick = 0;
    world_.bunker_assault_cleanup_pending = false;
    world_.bunker_explosion_frames_remaining = 0;
    world_.bunker_terminal_explosion_frames_total = 0;
    world_.bunker_special_effect_frames_remaining = 0;
    world_.muzzle_flash_frames_remaining = 0;
    muzzle_flash_pending_draw_ = false;
    world_.bunker_door_opening_frames_remaining = 0;
    world_.bunker_native_choreography = false;
    world_.bunker_assault_direct_draw = false;
    world_.bunker_assault_entries = 0;
    world_.bunker_white_flag = false;
    world_.bunker_walk_sound_toggle = false;
    world_.game_over_input_latched = false;
    world_.level_banner_frames_remaining = kLevelBannerTicks;
    world_.frame_tick = kInitialGameplayClock;
    player_aim_accumulator_ = kInitialAimAccumulator;
    fire_release_required_ = enhanced_input_debounce_;
    fire_buffered_ = false;
    reset_object_table_and_seed_level_actors();
}

void Game::advance_level()
{
    // 5008 resets aim once per campaign; 354e resets only the round clock.
    const auto aim = player_aim_accumulator_;
    start_level(static_cast<std::uint16_t>(world_.current_level + 1));
    player_aim_accumulator_ = aim;
}

void Game::reset_to_title()
{
    control_panel_key_latch_ = MenuKeyAction::None;
    prompt_mouse_response_ = PromptMouseResponse::None;
    const auto high_scores = world_.high_scores;
    const auto gameplay_rng_seed = world_.gameplay_rng_seed;
    const auto audio_config = world_.audio_config_words;
    const bool credits_seen = world_.credits_seen;
    const bool fast_shots = world_.fast_shots_enabled;
    const bool female_finale = world_.female_finale_enabled;
    world_ = {};
    reusable_object_slot_.reset();
    world_.high_scores = high_scores;
    world_.gameplay_rng_seed = gameplay_rng_seed;
    world_.audio_config_words = audio_config;
    world_.credits_seen = credits_seen;
    world_.fast_shots_enabled = fast_shots;
    world_.female_finale_enabled = female_finale;
    world_.screen = Screen::Title;
    world_.confirmation_prompt = ConfirmationPromptAction::None;
    world_.control_panel_from_gameplay = false;
    world_.screen_fade_frames_remaining = kScreenFadeFrames;
    player_aim_accumulator_ = kNeutralAimAccumulator;
    fire_release_required_ = false;
    fire_buffered_ = false;
}

bool Game::check_and_insert_high_score()
{
    if (world_.high_score_checked) {
        return std::any_of(world_.high_scores.begin(), world_.high_scores.end(), [](const HighScoreEntry& entry) {
            return entry.highlighted;
        });
    }

    world_.high_score_checked = true;
    world_.high_score_gamepad_character = 'A';
    world_.high_score_gamepad_active = false;

    for (auto& entry : world_.high_scores) {
        entry.highlighted = false;
    }

    HighScoreEntry current {};
    current.name.clear();
    current.score = world_.scores.score;
    current.stat_a = world_.scores.grounded_invader_resolutions;
    current.stat_b = world_.scores.enemy_kills;
    current.stat_c = static_cast<std::uint16_t>(world_.current_level);
    current.highlighted = true;

    auto beats = [](const HighScoreEntry& left, const HighScoreEntry& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        // 15c3:0f73/0f7b/0f83 independently test signed words, not a sort key.
        return std::bit_cast<std::int16_t>(left.stat_a) > std::bit_cast<std::int16_t>(right.stat_a) ||
               std::bit_cast<std::int16_t>(left.stat_b) > std::bit_cast<std::int16_t>(right.stat_b) ||
               std::bit_cast<std::int16_t>(left.stat_c) > std::bit_cast<std::int16_t>(right.stat_c);
    };

    const auto insertion = std::find_if(world_.high_scores.begin(),
                                        world_.high_scores.end(),
                                        [&](const HighScoreEntry& entry) {
                                            return beats(current, entry);
                                        });
    if (insertion == world_.high_scores.end()) {
        world_.active_high_score_index = -1;
        return false;
    }

    world_.active_high_score_index =
        static_cast<int>(std::distance(world_.high_scores.begin(), insertion));
    std::move_backward(insertion, world_.high_scores.end() - 1, world_.high_scores.end());
    current.name.clear();
    *insertion = current;
    return true;
}

void Game::load_high_scores()
{
    world_.high_scores = default_high_scores();

    for (const auto& path : readable_config_paths(config_path_)) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            continue;
        }

        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
        if (bytes.size() < kConfigSize || bytes[0] != 'N' || bytes[1] != 'T' || bytes[2] != 'R' ||
            bytes[3] != 0 || read_u16_le(bytes, 4) != kConfigVersion) {
            continue;
        }

        // Exact layout from SaveConfigAndHighScores (1480:03df):
        //   0x06: DAT_2730_dea4   0x08: DAT_2730_dea2   0x0a: DAT_2730_dea6
        //   0x0c: DAT_2730_1ff8   0x0e: DAT_2730_1ffa   0x10: DAT_2730_1ffc
        //   0x12: DAT_2730_e1d0   0x14: DAT_2730_1ffe
        static constexpr std::array<std::size_t, 8> kConfigWordOffsets {
            0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14,
        };
        config_internals::Words saved;
        for (std::size_t index = 0; index < saved.size(); ++index) {
            saved[index] = read_u16_le(bytes, kConfigWordOffsets[index]);
        }
        world_.audio_config_words = config_internals::restore_preferences(world_.audio_config_words, saved);

        for (std::size_t index = 0; index < world_.high_scores.size(); ++index) {
            const auto record = kConfigHighScoreOffset + index * kHighScoreRecordSize;
            auto& entry = world_.high_scores[index];
            entry.name = name_from_record(bytes, record);
            const auto score_low = read_u16_le(bytes, record + 0x40);
            const auto score_high = read_u16_le(bytes, record + 0x42);
            entry.score = static_cast<std::int32_t>((static_cast<std::uint32_t>(score_high) << 16u) |
                                                    score_low);
            entry.stat_a = read_u16_le(bytes, record + 0x44);
            entry.stat_b = read_u16_le(bytes, record + 0x46);
            entry.stat_c = read_u16_le(bytes, record + 0x48);
            entry.highlighted = false;
        }
        return;
    }
}

bool Game::save_high_scores() const
{
    const auto path = config_path_.empty() ? writable_config_path() : config_path_;
    std::vector<std::uint8_t> bytes(kConfigSize, 0);
    bytes[0] = 'N';
    bytes[1] = 'T';
    bytes[2] = 'R';
    bytes[3] = 0;
    write_u16_le(bytes, 4, kConfigVersion);

    static constexpr std::array<std::size_t, 8> kConfigWordOffsets {
        0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14,
    };
    for (std::size_t index = 0; index < world_.audio_config_words.size(); ++index) {
        write_u16_le(bytes, kConfigWordOffsets[index], world_.audio_config_words[index]);
    }

    for (std::size_t index = 0; index < world_.high_scores.size(); ++index) {
        const auto record = kConfigHighScoreOffset + index * kHighScoreRecordSize;
        const auto& entry = world_.high_scores[index];
        const auto copy_len = std::min<std::size_t>(entry.name.size(), kHighScoreNameSize - 1);
        std::copy_n(entry.name.begin(), copy_len, bytes.begin() + static_cast<std::ptrdiff_t>(record));

        const auto score = static_cast<std::uint32_t>(std::max<std::int32_t>(0, entry.score));
        write_u16_le(bytes, record + 0x40, static_cast<std::uint16_t>(score & 0xffffu));
        write_u16_le(bytes, record + 0x42, static_cast<std::uint16_t>(score >> 16u));
        write_u16_le(bytes, record + 0x44, entry.stat_a);
        write_u16_le(bytes, record + 0x46, entry.stat_b);
        write_u16_le(bytes, record + 0x48, entry.stat_c);
    }

    return write_config_atomically(path, bytes);
}

void Game::advance_from_game_over()
{
    if (check_and_insert_high_score()) {
        world_.screen = Screen::HighScoreEntry;
        world_.high_score_frames_remaining = 900;
        return;
    }

    if (world_.screen == Screen::GameOver) {
        world_.screen = Screen::HighScores;
        world_.high_score_frames_remaining = kHighScoreAttractScreenFrames;
        world_.active_high_score_index = -1;
        world_.screen_fade_frames_remaining = kScreenFadeFrames;
        return;
    }

    reset_to_title();
    previous_input_ = {};
}

void Game::reset_object_table_and_seed_level_actors()
{
    world_.objects.clear();
    world_.object_highwater = 0;
    reusable_object_slot_.reset();
    next_presentation_id_ = 1;
    // DAT_2730_2628..4d38 contains 200 fixed 0x32-byte object records.
    world_.objects.reserve(kObjectTableCapacity);
    for (auto& bank : world_.wave_banks) {
        bank = {};
    }

    Object player {};
    player.active = true;
    player.type = ObjectType::PlayerCannon;
    player.position = {kBunkerX, kBunkerY};
    player.extent = {13.0f, 6.0f};
    player.frame = kInitialAimFrame;
    store_object(std::move(player));

    if (world_.current_level >= WorldState::kFinalGameplayLevel) {
        world_.finale_spawn_budget_remaining = finale_budget_override_.value_or(
            static_cast<std::uint16_t>(std::max<std::uint16_t>(3, world_.finale_budget_seed) * 3));
        world_.finale_spawn_budget_total = world_.finale_spawn_budget_remaining;
        spawn_finale_controller();
        return;
    }

    const auto level_index = std::min<std::size_t>(world_.current_level, WorldState::kNormalLevelCount - 1);

    world_.finale_spawn_budget_remaining = 0;
    world_.finale_spawn_budget_total = 0;

    for (std::size_t bank = 0; bank < WorldState::kWaveBankCount; ++bank) {
        import_wave_record(bank, wave_table_[bank][level_index]);
        // 29dc allocates a controller only after a nonempty 2944 import.
        if (world_.wave_banks[bank].remaining_spawns == 0) {
            continue;
        }
        Object controller {};
        controller.active = true;
        controller.type = ObjectType::WaveController;
        controller.wave_bank = bank;
        // 56f3 clears the record; 29dc does not initialize collision dimensions.
        controller.extent = {};
        store_object(std::move(controller));
    }
}

void Game::process_player_input_and_modal_state(const InputState& input)
{
    auto player_it = std::find_if(world_.objects.begin(), world_.objects.end(), [](const Object& object) {
        return object.type == ObjectType::PlayerCannon;
    });

    if (world_.gameplay_state != GameplayState::Active) {
        return;
    }

    if (world_.transition_armed) {
        // 2fe8 centers by one accumulator unit and ignores player controls.
        player_aim_accumulator_ += (player_aim_accumulator_ < kNeutralAimAccumulator) -
                                   (player_aim_accumulator_ > kNeutralAimAccumulator);
        if (player_it != world_.objects.end()) {
            player_it->frame = std::clamp(player_aim_accumulator_ >> 1, kAimFrameMin, kAimFrameMax);
        }
        return;
    }
    if (world_.transition_freeze) {
        return;
    }

    if (player_it != world_.objects.end()) {
        if (input.mouse_moved) {
            const float clamped =
                std::clamp(input.mouse_x, 0.0f, kGameplayWidth - 1.0f);
            player_aim_accumulator_ = static_cast<int>(
                std::lround(clamped * 255.0f / (kGameplayWidth - 1.0f)));
        }
        if (input.move_left) {
            player_aim_accumulator_ -= kKeyboardAimStep;
        }
        if (input.move_right) {
            player_aim_accumulator_ += kKeyboardAimStep;
        }
        player_aim_accumulator_ = std::clamp(player_aim_accumulator_, 0, 0xff);
        player_it->frame = std::clamp(player_aim_accumulator_ >> 1, kAimFrameMin, kAimFrameMax);
    }

    if (fire_release_required_ && !input.fire) {
        fire_release_required_ = false;
    }
    if (!fire_release_required_ && enhanced_input_debounce_ &&
        input.fire && !previous_input_.fire) {
        fire_buffered_ = true;
    }
    const bool fire_requested = input.fire || (enhanced_input_debounce_ && fire_buffered_);
    // 16c8:242e increments the cannon timer before testing its firing cadence.
    if (!fire_release_required_ && fire_requested && player_it != world_.objects.end() &&
        ((player_it->timer + 1) % kPlayerFireGateTicks) == 0) {
        spawn_player_projectile();
        fire_buffered_ = false;
    }
}

void Game::update_active_objects()
{
    if (!world_.object_highwater) {
        world_.object_highwater = world_.objects.size();
    }
    static constexpr std::array kVariants {
        AircraftVariant::D,
        AircraftVariant::A,
        AircraftVariant::B,
        AircraftVariant::C,
    };

    if (!world_.bunker_assault_active && !world_.survivor_live) {
        for (std::size_t index = 0; index < world_.objects.size(); ++index) {
            auto& object = world_.objects[index];
            if (!object.active || object.pending_destroy || object.type != ObjectType::LandedInvader ||
                !landed_invader_hits_original_edge_resolution(object)) {
                continue;
            }

            object.pending_destroy = true;
            spawn_resolution_particles(object);
            add_to_score(2);
            ++world_.scores.grounded_invader_resolutions;
            retire_object(index);
        }
    }

    const auto landed_invader_count = static_cast<int>(std::count_if(
        world_.objects.begin(), world_.objects.end(), [](const Object& object) {
            return object.active && !object.pending_destroy && object.type == ObjectType::LandedInvader;
        }));

    if (world_.gameplay_state == GameplayState::Active && !world_.bunker_assault_active &&
        world_.bunker_overrun_pending &&
        landed_invader_count < kLandedInvaderLimit) {
        world_.bunker_overrun_pending = false;
    }

    if (world_.gameplay_state == GameplayState::Active && !world_.bunker_assault_active &&
        landed_invader_count >= kLandedInvaderLimit &&
        !world_.bunker_overrun_pending) {
        world_.bunker_overrun_pending = true;
    }

    if (world_.gameplay_state == GameplayState::Active && !world_.bunker_assault_active &&
        world_.bunker_overrun_pending &&
        landed_invader_count >= kLandedInvaderLimit && !has_airborne_hostiles()) {
        // The last 32d5 page precedes 4e52's banner draw at this same tick.
        before_owner_page_ = world_;
        world_.bunker_overrun_pending = false;
        world_.bunker_assault_active = true;
        world_.gameplay_state = GameplayState::ScriptedSequence;
        world_.overrun_interrupt_residue = false;
        world_.transition_freeze = true;
        world_.transition_armed = false;
        world_.transition_started_tick = 0;
        world_.bunker_assault_entries = 0;
        world_.bunker_assault_step_tick = world_.frame_tick;
        world_.bunker_assault_cleanup_pending = true;
        world_.bunker_explosion_frames_remaining = 0;
        world_.bunker_terminal_explosion_frames_total = 0;
        world_.bunker_special_effect_frames_remaining = 0;
        world_.bunker_white_flag = false;

        world_.bunker_native_choreography = true;
        world_.bunker_door_opening_frames_remaining = 0;
        world_.bunker_assault_direct_draw = false;

        for (auto& object : world_.objects) {
            if (!object.active) {
                continue;
            }

            if (object.type == ObjectType::PlayerCannon) {
                continue;
            }

            if (object.type == ObjectType::LandedInvader) {
                continue;
            }

            object.pending_destroy = true;
        }
    }

    // 4e52 presents the banner before 4cfb removes excess landed troopers.
    if (world_.bunker_assault_active && world_.bunker_assault_cleanup_pending &&
        world_.frame_tick > world_.bunker_assault_step_tick) {
        world_.bunker_assault_cleanup_pending = false;
        if (world_.overrun_cleanup_rng_seed) {
            world_.gameplay_rng_seed = *world_.overrun_cleanup_rng_seed;
        }
        std::vector<std::size_t> landed_invaders;
        for (std::size_t index = 0; index < world_.objects.size(); ++index) {
            const auto& object = world_.objects[index];
            if (!object.active || object.pending_destroy || object.type != ObjectType::LandedInvader) {
                continue;
            }

            landed_invaders.push_back(index);
        }

        // 16c8:4c46 compares the integer X coordinate against the bunker object's
        // original-space X, not its collision center.
        std::sort(landed_invaders.begin(), landed_invaders.end(), [this](std::size_t left, std::size_t right) {
            return std::abs(std::floor(world_.objects[left].position.x) - kBunkerOriginalX) <
                   std::abs(std::floor(world_.objects[right].position.x) - kBunkerOriginalX);
        });

        for (std::size_t index = kLandedInvaderLimit; index < landed_invaders.size(); ++index) {
            const auto object_index = landed_invaders[index];
            const auto object = world_.objects[object_index];
            world_.objects[object_index].pending_destroy = true;
            spawn_bunker_resolution_particles(object);
            add_to_score(2);
            ++world_.scores.grounded_invader_resolutions;
            retire_object(object_index);
        }
    }

    const bool bunker_assault_active = world_.bunker_assault_active ||
        (!world_.survivor_live && world_.gameplay_state == GameplayState::ScriptedSequence);
    const bool landed_overrun_active = landed_invader_count >= kLandedInvaderLimit;
    bool has_active_assaulter = std::any_of(world_.objects.begin(),
                                            world_.objects.end(),
                                            [](const Object& object) {
                                                return object.active && !object.pending_destroy &&
                                                       object.type == ObjectType::LandedInvader &&
                                                       object.assaulting && object.assault_stage != 2;
                                            });

    if (bunker_assault_active) {
        world_.bunker_assault_direct_draw = false;
        // 4cfb keeps running 32d5 until the cleanup particles have finished.
        const bool cleanup_particles_active = std::any_of(
            world_.objects.begin(), world_.objects.end(), [](const Object& object) {
                return object.active && !object.pending_destroy &&
                       object.type == ObjectType::ResolutionParticle;
            });
        if (!has_active_assaulter && !world_.bunker_assault_cleanup_pending &&
            !cleanup_particles_active) {
            for (auto& object : world_.objects) {
                if (object.active && !object.pending_destroy &&
                    object.type == ObjectType::LandedInvader && object.assaulting &&
                    object.assault_stage == 2) {
                    object.pending_destroy = true;
                    break;
                }
            }

            Object* next_attacker = nullptr;
            for (auto& object : world_.objects) {
                if (!object.active || object.pending_destroy ||
                    object.type != ObjectType::LandedInvader || object.assaulting) {
                    continue;
                }

                if (next_attacker == nullptr ||
                    std::abs(object.position.x - kBunkerOriginalX) <
                        std::abs(next_attacker->position.x - kBunkerOriginalX)) {
                    next_attacker = &object;
                }
            }

            if (next_attacker != nullptr) {
                next_attacker->assaulting = true;
                next_attacker->assault_stage = 0;
                next_attacker->sprite_id = next_attacker->position.x >= kBunkerX ? 0x101 : 0x0fb;
                next_attacker->timer = 0;
                next_attacker->frame = 0;
                has_active_assaulter = true;
                world_.bunker_assault_step_tick = world_.frame_tick;
            }
        }
    } else if (!world_.survivor_live) {
        for (auto& object : world_.objects) {
            if (object.active && !object.pending_destroy && object.type == ObjectType::LandedInvader) {
                object.assaulting = false;
            }
        }
    }

    for (std::size_t index = 0; index < world_.objects.size(); ++index) {
        if (!world_.objects[index].active || world_.objects[index].pending_destroy) {
            continue;
        }

        auto& object = world_.objects[index];
        const auto type_before_update = object.type;
        if (world_.survivor_live && index == world_.survivor_live->controller_slot) {
            update_survivor_controller(object);
            continue;
        }
        if (world_.survivor_live && object.type == ObjectType::LandedInvader) {
            if (object.assaulting && object.frame > 0) {
                if (++object.timer >= static_cast<std::uint32_t>(object.frame)) {
                    object.timer = 0;
                    object.position.x += object.velocity.x;
                    ++object.last_drop_tick;
                }
                if (object.position.x == object.velocity.y) {
                    object.velocity = {};
                    world_.gameplay_state = GameplayState::ScriptedSequence;
                }
            }
            continue;
        }
        if (object.type == ObjectType::Presenter && world_.no_survivor_intermission_active &&
            object.sprite_id == internals::kBannerFlybyAircraftSprite) {
            object.position.x += object.velocity.x;
            object.position.y += object.velocity.y;
            if (object.frame-- < 1) {
                world_.gameplay_state = GameplayState::ScriptedSequence;
            }
            continue;
        }
        if (object.type == ObjectType::WaveController) {
            const auto bank_index = object.wave_bank;
            const auto& bank = world_.wave_banks[bank_index];
            if (world_.gameplay_state != GameplayState::Active || bunker_assault_active ||
                world_.transition_armed || world_.waves_exhausted ||
                world_.frame_tick >= bank.stop_tick) {
                retire_object(index);
                continue;
            }
            update_wave_controller(bank_index,
                                   bank_index < kVariants.size() ? kVariants[bank_index] : AircraftVariant::D);
            continue;
        }
        bool should_spawn_paratrooper = false;
        WorldPosition paratrooper_position {};
        float paratrooper_drift = 0.0f;
        AircraftVariant paratrooper_source_variant = AircraftVariant::D;
        std::size_t paratrooper_source_bank = 0;

        if (object.type == ObjectType::AircraftDebris && object.has_dropped_payload) {
            object.velocity.y += 0.03125f;
        }

        if (object.type == ObjectType::Paratrooper && object.finale_drop && object.assault_stage != 0) {
            // 1e25 recovers from hit knockback before moving, capped at +0.5.
            object.velocity.y = std::min(0.5f, object.velocity.y + 0.125f);
        }

        if (object.type == ObjectType::ResolutionParticle) {
            // 01ae post-decrements lifetime before 5324/5663 gravity and movement.
            if (object.assault_stage-- == 0) {
                retire_object(index);
                continue;
            }
            object.velocity.y += 0.03125f;
        }

        // 0559 and the stationary type-11 record retain velocity without moving.
        if (object.type != ObjectType::SmartBomb && object.type != ObjectType::LandedInvader &&
            !(object.type == ObjectType::GroundedTransition && !object.parachute_lost)) {
            object.position.x += object.velocity.x;
            object.position.y += object.velocity.y;
        }
        const bool deployed_trooper = object.type == ObjectType::Paratrooper &&
                                     !object.finale_drop && !object.parachute_lost && object.assault_stage == 1;
        if (object.type != ObjectType::SmartBomb && object.type != ObjectType::ResolutionParticle && !deployed_trooper &&
            !(object.type == ObjectType::LandedInvader &&
              (!object.assaulting || object.assault_stage == 3 || object.assault_stage == 2))) {
            ++object.timer;
        }

        if (object.type == ObjectType::PlayerCannon && world_.transition_armed &&
            !bunker_assault_active && object.timer > kTransitionDelayTicks) {
            // 242e completes the clear wait during the updater, before 31d7.
            world_.gameplay_state = world_.current_level >= WorldState::kFinalGameplayLevel
                                        ? GameplayState::FinaleComplete
                                        : GameplayState::LevelComplete;
        }

        if (object.type == ObjectType::PlayerCannon &&
            (world_.no_survivor_intermission_active || world_.survivor_live)) {
            // 242e still observes depleted banks while 4dec suppresses firing.
            world_.waves_exhausted = std::all_of(world_.wave_banks.begin(), world_.wave_banks.end(),
                [](const RuntimeWaveState& bank) { return bank.remaining_spawns == 0; });
        }

        if (object.type == ObjectType::Paratrooper && object.finale_drop) {
            if (object.assault_stage == 0) {
                object.velocity = {};
                if (object.timer >= kFinaleDropArmingTicks) {
                    object.assault_stage = 1;
                    object.parachute_lost = true;
                    object.finale_hits_remaining = 3;
                    object.velocity.y = kParatrooperFreeFallSpeed;
                    object.timer = 0;
                    object.frame = 0;
                    object.extent = {2.5f, 3.0f};
                }
            } else {
                if (object_outside_original_cull_bounds(object)) {
                    retire_object(index);
                    continue;
                }
                if (object.timer >= kFinaleDropAnimationTicks) {
                    object.timer = 0;
                    object.frame = (object.frame + 1) % 4;
                }

                if (std::floor(object.position.y) > 0xae) {
                    if (object.finale_hits_remaining != 0) {
                        trigger_bunker_hit_from_enemy(object);
                    } else {
                        object.position.x += 2.0f;
                        object.position.y += 3.0f;
                        object.velocity.y = -1.0f;
                        object.pending_destroy = true;
                        spawn_resolution_particles(object);
                        add_to_score(2);
                        ++world_.scores.grounded_invader_resolutions;
                        retire_object(index);
                    }
                    continue;
                }
            }
        }

        if (object.type == ObjectType::Paratrooper && !object.finale_drop &&
            !object.parachute_lost && object.assault_stage == 0) {
            const auto old_timer = object.timer - 1u;
            if (old_timer % kParatrooperDeployStepTicks == 0) {
                object.velocity.x *= 0.5f;
                ++object.frame;
                if (object.frame == 2) {
                    object.velocity.y = kParatrooperCanopyFallSpeed;
                }
                if (object.frame > 4) {
                    object.frame = 0;
                    object.timer = 0;
                    object.assault_stage = 1;
                }
            }

        }

        if ((object.type == ObjectType::Aircraft ||
             (object.type == ObjectType::AircraftDebris && !object.has_dropped_payload) ||
             object.type == ObjectType::PlayerProjectile ||
             object.type == ObjectType::EnemyDeath || object.type == ObjectType::Paratrooper ||
             (object.type == ObjectType::GroundedTransition && object.parachute_lost) ||
             object.type == ObjectType::ResolutionParticle) &&
            object_outside_original_cull_bounds(object)) {
            retire_object(index);
            continue;
        }

        if (!bunker_assault_active && object.type == ObjectType::Aircraft) {
            auto& bank = world_.wave_banks[object.wave_bank];
            if (bank.paratrooper_cadence > 0 &&
                world_.frame_tick % bank.paratrooper_cadence == 0 &&
                world_.frame_tick != bank.last_paratrooper_tick) {
                object.last_drop_tick = world_.frame_tick;
                bank.last_paratrooper_tick = world_.frame_tick;
                const auto local_offset =
                    drop_offset_for_variant(object.aircraft_variant, object.direction);
                // The original variant callbacks release the paratrooper before
                // invoking the shared aircraft movement helper.
                const auto drop_x = object.position.x - object.velocity.x + local_offset;
                should_spawn_paratrooper = true;
                paratrooper_position = {
                    drop_x,
                    object.position.y + drop_y_offset_for_variant(object.aircraft_variant),
                };
                paratrooper_drift = object.velocity.x * 0.5f;
                paratrooper_source_variant = object.aircraft_variant;
                paratrooper_source_bank = object.wave_bank;
            }
        }

        if (!bunker_assault_active && object.type == ObjectType::SmartBomb) {
            if (object.extent.y < 6.0f) {
                // Armed callback 16c8:1b9a moves first, then transitions only
                // when the countdown's value before decrement was zero.
                object.position.x += object.velocity.x;
                object.position.y += object.velocity.y;
                if (object_outside_original_cull_bounds(object)) {
                    retire_object(index);
                    continue;
                }
                const auto old_timer = object.timer--;
                if (old_timer == 0) {
                    object.frame = 0;
                    object.timer = smart_bomb_stage_duration(0);
                    object.position.x -= 3.0f;
                    object.position.y -= 10.0f;
                    object.extent = {kSmartBombFallWidth * 0.5f, kSmartBombFallHeight * 0.5f};
                    object.sound_id = kSoundSmartBombArm;
                } else {
                    // 1982/19e2 retain an object-local draw-before-increment
                    // exhaust phase, like the aircraft A overlay. One normal
                    // presentation follows each simulation update here.
                    object.frame = (object.frame + 1) % 3;
                }
            } else {
                // Falling callback 16c8:1af5 tests the bunker line before
                // changing stage velocity and invoking shared movement.
                if (std::floor(object.position.y) > 159.0f) {
                    trigger_bunker_hit_from_enemy(object);
                    continue;
                }

                const auto old_timer = object.timer--;
                if (old_timer == 0 && object.frame < 4) {
                    ++object.frame;
                    object.timer = smart_bomb_stage_duration(object.frame);
                    object.velocity = smart_bomb_stage_velocity(object.frame, object.direction);
                }
                object.position.x += object.velocity.x;
                object.position.y += object.velocity.y;
                if (object_outside_original_cull_bounds(object)) {
                    retire_object(index);
                    continue;
                }
            }
        }

        if (object.type == ObjectType::FinaleController) {
            update_finale_controller(object);
            if (world_.objects[index].pending_destroy) {
                retire_object(index);
                continue;
            }
        }

        if (object.type == ObjectType::EnemyDeath) {
            if ((object.timer % 2) == 1) {
                ++object.frame;
            }
            const int frame_limit = object.assault_stage > 0 ? object.assault_stage : 6;
            if (object.frame > frame_limit) {
                retire_object(index);
            }
            continue;
        }

        if (object.type == ObjectType::AircraftDebris) {
            if (object.has_dropped_payload) {
                if (object.timer > 5) {
                    object.timer = 0;
                    ++object.frame;
                }
                if (object_outside_original_cull_bounds(object)) {
                    retire_object(index);
                }
            } else {
                if ((object.timer % 2) == 1) {
                    ++object.frame;
                    if (object.timer > static_cast<std::uint32_t>(object.assault_stage)) {
                        const auto shell = object;
                        object.pending_destroy = true;
                        scatter_aircraft_debris(shell);
                        // Allocate all children before 567f releases the
                        // shell. Allocation can invalidate the object reference.
                        retire_object(index);
                    }
                }
            }
            continue;
        }

        if (object.type == ObjectType::GroundedTransition && object.parachute_lost &&
            object.timer >= kFinaleDropAnimationTicks) {
            object.timer = 0;
            object.frame = (object.frame + 1) % 4;
        }

        if (object.type == ObjectType::Paratrooper && !object.finale_drop &&
            object.position.y >= kGroundY) {
            if (object.parachute_lost) {
                for (std::size_t other_index = 0; other_index < world_.objects.size(); ++other_index) {
                    auto& other = world_.objects[other_index];
                    if (!other.active || other.pending_destroy || other.type != ObjectType::LandedInvader) {
                        continue;
                    }

                    const bool same_ground_row = std::abs(other.position.y - kGroundY - 8.0f) < 16.0f;
                    const bool overlaps_landed_invader =
                        std::abs(other.position.x - object.position.x) < kLandedInvaderMergeDistance;
                    if (!same_ground_row || !overlaps_landed_invader) {
                        continue;
                    }

                    other.pending_destroy = true;
                    spawn_resolution_particles(other, object);
                    add_to_score(2);
                    ++world_.scores.grounded_invader_resolutions;
                    retire_object(other_index);
                    break;
                }
                retire_object(index);
                continue;
            } else if (lands_on_bunker(object, world_.objects)) {
                object.pending_destroy = true;
                spawn_bunker_resolution_particles(object);
                add_to_score(2);
                ++world_.scores.grounded_invader_resolutions;
                retire_object(index);
                continue;
            } else {
                object.position.y = kGroundY;
                object.type = ObjectType::GroundedTransition;
                object.extent = {kGroundedAnimationWidth * 0.5f, kGroundedAnimationHeight * 0.5f};
                object.timer = 0;
                object.frame = 0;
            }
        }

        if (object.type == ObjectType::GroundedTransition && object.parachute_lost &&
            std::floor(object.position.y) > 0xae) {
            // 034b resolves only this actor, using the integer ground threshold.
            object.position.x += 2.0f;
            object.position.y += 3.0f;
            object.velocity.y = -1.0f;
            object.pending_destroy = true;
            spawn_resolution_particles(object);
            add_to_score(2);
            ++world_.scores.grounded_invader_resolutions;
            retire_object(index);
            continue;
        }

        // 0559 tests the old timer. Installing it on landing does not run the
        // new callback again in the same update.
        if (type_before_update == ObjectType::GroundedTransition &&
            object.type == ObjectType::GroundedTransition && !object.parachute_lost &&
            ((object.timer - 1u) % kParatrooperDeployStepTicks) == 0) {
            ++object.frame;
        }

        if (object.type == ObjectType::GroundedTransition && !object.parachute_lost &&
            object.frame > 7) {
            object.type = ObjectType::LandedInvader;
            // The original conversion retains the high word after applying the
            // three-pixel footprint shift, discarding the fixed-point fraction.
            object.position.x = std::floor(object.position.x + 3.0f);
            object.position.y += 8.0f;
            object.extent = {kLandedInvaderWidth * 0.5f, 3.5f};
            object.timer = 0;
            object.frame = 0;
            object.assaulting = false;
            object.assault_stage = 0;

            if (landed_invader_hits_original_edge_resolution(object)) {
                object.pending_destroy = true;
                spawn_resolution_particles(object);
                add_to_score(2);
                ++world_.scores.grounded_invader_resolutions;
                retire_object(index);
                continue;
            }

            for (std::size_t other_index = 0; other_index < world_.objects.size(); ++other_index) {
                if (other_index == index) {
                    continue;
                }

                auto& other = world_.objects[other_index];
                if (!other.active || other.pending_destroy || other.type != ObjectType::LandedInvader) {
                    continue;
                }

                const bool same_ground_row = std::abs(other.position.y - object.position.y) < 2.0f;
                const bool overlaps_landed_invader =
                    std::abs(other.position.x - object.position.x) < kLandedInvaderMergeDistance;
                if (!same_ground_row || !overlaps_landed_invader) {
                    continue;
                }

                object.pending_destroy = true;
                spawn_resolution_particles(object, other);
                add_to_score(2);
                ++world_.scores.grounded_invader_resolutions;
                retire_object(index);
                break;
            }
            if (!world_.objects[index].active) {
                continue;
            }
        }

        if (object.type == ObjectType::LandedInvader) {
            if (!bunker_assault_active && landed_invader_hits_original_edge_resolution(object) &&
                !object.assaulting) {
                object.pending_destroy = true;
                spawn_resolution_particles(object);
                add_to_score(2);
                ++world_.scores.grounded_invader_resolutions;
                retire_object(index);
                continue;
            }

            if (object.assaulting) {
                object.velocity.x = 0.0f;
                if (object.assault_stage == 0) {
                    if (object.timer >= kBunkerAssaultOuterStepTicks) {
                        object.position.x += object.position.x < kBunkerDoorOuterX ? 1.0f : -1.0f;
                        object.timer = 0;
                        ++object.frame;
                    }
                    if (object.position.x == kBunkerDoorOuterX) {
                        if (world_.bunker_assault_entries == 0) {
                            object.assault_stage = 3;
                            world_.bunker_door_opening_frames_remaining = 15;
                            world_.bunker_special_effect_frames_remaining = -1;
                            play_sound(kSoundBunkerSpecialSetup);
                        } else {
                            before_owner_page_ = world_;
                            object.assault_stage = 1;
                            object.sprite_id = 0x101;
                            object.position.y -= 2.0f;
                            world_.bunker_assault_direct_draw = true;
                        }
                    }
                } else if (object.assault_stage == 3) {
                    if (--world_.bunker_door_opening_frames_remaining == 0) {
                        // 3e8b's final draw still uses 404a at Y=172; 4cfb
                        // then selects 40b2 and redraws at Y=171 without a tick.
                        before_owner_page_ = world_;
                        object.assault_stage = 1;
                        object.sprite_id = 0x101;
                        object.position.y -= 2.0f;
                        world_.bunker_assault_direct_draw = true;
                    }
                } else if (object.assault_stage == 1) {
                    if (object.timer >= kBunkerAssaultInnerStepTicks) {
                        object.position.x -= 1.0f;
                        object.timer = 0;
                        ++object.frame;
                    }
                }
                // 404a invokes the walking sound on every matching draw, not
                // only when animation changes. Arrival clears velocity first.
                const bool moving = (object.assault_stage == 0 && object.position.x != kBunkerDoorOuterX) ||
                    (object.assault_stage == 1 && object.position.x != kBunkerDoorInnerX &&
                     !world_.bunker_assault_direct_draw);
                if (moving && object.frame % 4 == 0) {
                    play_sound(world_.bunker_walk_sound_toggle ? kSoundBunkerWalkB : kSoundBunkerWalkA);
                    world_.bunker_walk_sound_toggle = !world_.bunker_walk_sound_toggle;
                }
            }

            if (object.assaulting && object.assault_stage == 1 &&
                object.position.x <= kBunkerDoorInnerX) {
                object.velocity.x = 0.0f;
                object.assault_stage = 2;
                ++world_.bunker_assault_entries;
                world_.transition_armed = false;
                has_active_assaulter = false;
                world_.bunker_assault_step_tick = world_.frame_tick;

                if (world_.bunker_assault_entries >= kLandedInvaderLimit) {
                    world_.bunker_special_effect_frames_remaining = 15;
                    play_sound(kSoundBunkerAssault);
                }
            }
        }

        if (should_spawn_paratrooper) {
            spawn_paratrooper(paratrooper_position,
                              paratrooper_drift,
                              paratrooper_source_variant,
                              paratrooper_source_bank);
        }
    }

    for (std::size_t index = 0; index < world_.objects.size(); ++index) {
        if (world_.objects[index].pending_destroy) {
            retire_object(index);
            world_.objects[index] = {};
        }
    }

    if (world_.gameplay_state == GameplayState::Active && !world_.bunker_assault_active &&
        world_.bunker_overrun_pending) {
        const auto current_landed_invaders = static_cast<int>(std::count_if(
            world_.objects.begin(), world_.objects.end(), [](const Object& object) {
                return object.active && !object.pending_destroy && object.type == ObjectType::LandedInvader;
            }));
        if (current_landed_invaders < kLandedInvaderLimit) {
            world_.bunker_overrun_pending = false;
        }
    }

}

void Game::check_object_collisions()
{
    if (world_.gameplay_state != GameplayState::Active) {
        return;
    }

    // 31d7 dispatches sources in slot order, not separate type-priority passes.
    // Callbacks can append objects; never keep references across dispatch.
    for (std::size_t index = 0; index < world_.objects.size(); ++index) {
        const auto& object = world_.objects[index];
        if (!object.active || object.pending_destroy) {
            continue;
        }
        if (object.type == ObjectType::PlayerProjectile ||
            (object.type == ObjectType::GroundedTransition && object.parachute_lost &&
             !object.finale_drop)) {
            check_source_collisions(index);
        } else if (is_aircraft_debris(object)) {
            check_debris_collisions(index);
        } else if (object.type == ObjectType::PlayerCannon) {
            // 31d7 dispatches stationary cannon contacts after 31a0; newborn
            // particles must not run an updater in their birth tick.
            auto cannon = object;
            cannon.position = {kBunkerOriginalX, kBunkerOriginalY};
            for (std::size_t target = 0; target < world_.objects.size(); ++target) {
                const auto& trooper = world_.objects[target];
                if (!trooper.active || trooper.pending_destroy || trooper.finale_drop ||
                    trooper.type != ObjectType::Paratrooper || trooper.parachute_lost ||
                    !paratrooper_hits_bunker_zone(trooper, world_.objects)) {
                    continue;
                }
                dispatch_collision_target(target, cannon);
                if (world_.objects[target].pending_destroy) {
                    retire_object(target);
                }
            }
        }
    }
}

void Game::check_source_collisions(std::size_t source_index)
{
    for (std::size_t target_index = 0; target_index < world_.objects.size(); ++target_index) {
        const auto& object = world_.objects[target_index];
        if (!object.active || object.pending_destroy || target_index == source_index) {
            continue;
        }

        // 31d7 keeps scanning after 0168 retires the source. A later allocation
        // can replace that same slot, changing its geometry and callback.
        if (!objects_overlap(world_.objects[source_index], object)) {
            continue;
        }

        dispatch_collision_target(target_index, world_.objects[source_index]);
        if (world_.objects[target_index].pending_destroy) {
            retire_object(target_index);
        }
        // The inner callback can retire/reuse either slot before the outer
        // callback reads its impact, including 031f -> 026e particle allocation.
        if (world_.objects[source_index].active) {
            dispatch_collision_target(source_index, world_.objects[target_index]);
            if (world_.objects[source_index].pending_destroy) {
                retire_object(source_index);
            }
        }
    }
}

void Game::dispatch_collision_target(std::size_t target_index, const Object& impact)
{
    auto& object = world_.objects[target_index];
    if (resolve_airborne_enemy_hit(object)) {
        return;
    }
    if (object.type == ObjectType::PlayerProjectile) {
        retire_object(target_index);
        return;
    }

    const bool damaging_impact = impact.type == ObjectType::PlayerProjectile ||
                                is_aircraft_debris(impact);
    if (object.type == ObjectType::FinaleController && object.assault_stage == 0) {
        // 20ee checks secondary-type flags even if 0168 retired the impact.
        if (!damaging_impact) {
            return;
        }
        object.timer >>= 1u;
        spawn_finale_drop({object.position.x + kFinaleSpawnOffsetX,
                           object.position.y + kFinaleSpawnOffsetY}, true);
        return;
    }

    if (object.type == ObjectType::Paratrooper && object.finale_drop) {
        hit_finale_drop(object, impact);
        return;
    }

    if (object.type == ObjectType::Paratrooper ||
        (object.type == ObjectType::GroundedTransition && !object.parachute_lost)) {
        if (object.parachute_lost && !damaging_impact) {
            return;
        }
        if (projectile_hits_deployed_paratrooper_body(impact, object) ||
            object.parachute_lost) {
            object.pending_destroy = true;
            spawn_resolution_particles(object, impact);
            add_to_score(2);
            ++world_.scores.grounded_invader_resolutions;
        } else {
            object.type = ObjectType::GroundedTransition;
            object.parachute_lost = true;
            object.position.x += 3.0f;
            object.position.y += 11.0f;
            object.velocity.y = kParatrooperFreeFallSpeed;
            object.extent = {2.5f, 3.0f};
            object.frame = 0;
        }
        return;
    }

    if (object.type == ObjectType::GroundedTransition && object.parachute_lost &&
        damaging_impact) {
        object.pending_destroy = true;
        spawn_resolution_particles(object, impact);
        add_to_score(2);
        ++world_.scores.grounded_invader_resolutions;
    }
}

void Game::check_debris_collisions(std::size_t debris_index)
{
    const auto debris = world_.objects[debris_index];
    for (std::size_t target_index = 0; target_index < world_.objects.size(); ++target_index) {
        auto& object = world_.objects[target_index];
        if (!object.active || object.pending_destroy || target_index == debris_index ||
            (!is_debris_resolvable_invader(object) && object.type != ObjectType::Aircraft &&
             object.type != ObjectType::SmartBomb && object.type != ObjectType::FinaleController &&
             object.type != ObjectType::PlayerProjectile)) {
            continue;
        }

        if (!objects_overlap(debris, object, 0.0f)) {
            continue;
        }

        if (object.type == ObjectType::PlayerProjectile) {
            // The inner projectile's 0168 callback retires it on any impact.
            retire_object(target_index);
            continue;
        }

        if (resolve_airborne_enemy_hit(object)) {
            continue;
        }

        if (object.type == ObjectType::FinaleController) {
            if (object.assault_stage == 0) {
                object.timer >>= 1u;
                spawn_finale_drop({object.position.x + kFinaleSpawnOffsetX,
                                   object.position.y + kFinaleSpawnOffsetY}, true);
            }
            continue;
        }

        if (object.type == ObjectType::Paratrooper && object.finale_drop) {
            hit_finale_drop(object, debris);
            if (world_.objects[target_index].pending_destroy) {
                retire_object(target_index);
            }
            continue;
        }

        if ((object.type == ObjectType::Paratrooper || object.type == ObjectType::GroundedTransition) &&
            !object.parachute_lost) {
            // 0401 uses the impact Y for debris too: chute contacts do not kill.
            dispatch_collision_target(target_index, debris);
            if (world_.objects[target_index].pending_destroy) {
                retire_object(target_index);
            }
            continue;
        }

        object.pending_destroy = true;
        spawn_resolution_particles(object, debris);
        add_to_score(2);
        ++world_.scores.grounded_invader_resolutions;
        retire_object(target_index);
    }
}

bool Game::resolve_airborne_enemy_hit(Object& object)
{
    if (object.type == ObjectType::Aircraft) {
        const auto score = score_for_aircraft(object.aircraft_variant);
        convert_aircraft_to_debris(object);
        add_to_score(score);
    } else if (object.type == ObjectType::SmartBomb) {
        // 1c23 converts the bomb in place and selects art by its update callback,
        // not its animation frame. Preserve the original actor slot and bounds.
        const bool armed = object.extent.y < 6.0f;
        object.type = ObjectType::EnemyDeath;
        object.has_dropped_payload = true;
        object.finale_drop = armed;
        object.sprite_id = 0x1c7;
        object.timer = 0;
        object.frame = 0;
        object.assault_stage = 6;
        object.sound_id = 0;
        play_sound(kSoundEnemyDeath);
        add_to_score(10);
    } else {
        return false;
    }
    ++world_.scores.enemy_kills;
    return true;
}

void Game::hit_finale_drop(Object& object, const Object& impact)
{
    // 1fe9 has no collision callback until 1f21 installs 1da3 after 16 updates.
    if (object.assault_stage == 0) return;
    const auto previous = object.finale_hits_remaining--;
    if (previous != 0) {
        // Instruction audit: long multiplies, not RNG calls. X changes position,
        // not horizontal velocity; /IBCD changes only the vertical multiplier.
        object.position.x += 3.0 * impact.velocity.x;
        object.velocity.y += (world_.fast_shots_enabled ? 2.0f : 5.0f) * impact.velocity.y;
        return;
    }
    object.pending_destroy = true;
    spawn_resolution_particles(object, impact);
    add_to_score(2);
    ++world_.scores.grounded_invader_resolutions;
}

void Game::evaluate_level_progression()
{
    if (world_.gameplay_state == GameplayState::GameOver) {
        return;
    }

    if (world_.bunker_assault_active || world_.gameplay_state == GameplayState::ScriptedSequence) {
        if (world_.bunker_special_effect_frames_remaining > 0 &&
            world_.frame_tick > world_.bunker_assault_step_tick) {
            --world_.bunker_special_effect_frames_remaining;
            if (world_.bunker_special_effect_frames_remaining == 0) {
                if (world_.bunker_assault_entries >= kLandedInvaderLimit) {
                    world_.bunker_white_flag = true;
                    world_.transition_started_tick = world_.frame_tick;
                    world_.reset_sound_effects = true;
                    world_.player_dead = true;
                    world_.transition_freeze = false;
                    world_.transition_armed = false;
                    world_.bunker_assault_active = false;
                    world_.overrun_interrupt_residue = false;
                    world_.screen = Screen::GameOver;
                    world_.game_over_frames_remaining = kOverrunGameOverTicks;
                    world_.bunker_explosion_frames_remaining = 0;
                    world_.bunker_terminal_explosion_frames_total = 0;
                    world_.gameplay_state = GameplayState::GameOver;
                } else {
                    world_.player_dead = true;
                    world_.transition_freeze = false;
                    world_.transition_armed = false;
                    world_.bunker_assault_active = false;
                    world_.overrun_interrupt_residue = false;
                    world_.gameplay_state = GameplayState::ScriptedSequence;
                }
            }
        }

        if (world_.bunker_explosion_frames_remaining > 0) {
            --world_.bunker_explosion_frames_remaining;
            if (world_.bunker_explosion_frames_remaining == 0) {
                world_.bunker_special_effect_frames_remaining = 0;
                world_.bunker_terminal_explosion_frames_total = 0;
                world_.bunker_white_flag = true;
                world_.transition_started_tick = world_.frame_tick;
                world_.player_dead = true;
                world_.transition_freeze = false;
                world_.transition_armed = false;
                world_.bunker_assault_active = false;
                world_.overrun_interrupt_residue = false;
                world_.screen = Screen::GameOver;
                world_.game_over_frames_remaining = kOverrunGameOverTicks;
                world_.gameplay_state = GameplayState::GameOver;
            }
        }

        if (world_.gameplay_state == GameplayState::GameOver) {
            return;
        }

        if (world_.bunker_white_flag &&
            world_.frame_tick - world_.transition_started_tick >= kBunkerWhiteFlagTicks) {
            world_.player_dead = true;
            world_.transition_freeze = false;
            world_.transition_armed = false;
            world_.bunker_assault_active = false;
            world_.overrun_interrupt_residue = false;
            world_.screen = Screen::GameOver;
            world_.game_over_frames_remaining = kOverrunGameOverTicks;
            world_.bunker_special_effect_frames_remaining = 0;
            world_.bunker_explosion_frames_remaining = 0;
            world_.bunker_terminal_explosion_frames_total = 0;
            world_.gameplay_state = GameplayState::GameOver;
        }
        return;
    }

    const auto landed_count = static_cast<int>(std::count_if(
        world_.objects.begin(), world_.objects.end(), [](const Object& object) {
            return object.active && !object.pending_destroy && object.type == ObjectType::LandedInvader;
        }));

    const bool waves_depleted =
        world_.current_level >= WorldState::kFinalGameplayLevel
            ? (world_.finale_spawn_budget_remaining == 0 &&
               std::none_of(world_.objects.begin(), world_.objects.end(), [](const Object& object) {
                   return object.active && !object.pending_destroy && object.type == ObjectType::FinaleController;
               }))
            : std::all_of(world_.wave_banks.begin(), world_.wave_banks.end(), [](const RuntimeWaveState& bank) {
                  return bank.remaining_spawns == 0;
              });
    world_.waves_exhausted = waves_depleted;

    const bool should_arm_transition = waves_depleted && object_table_is_clearable();

    if (!should_arm_transition) {
        world_.transition_armed = false;
        world_.transition_freeze = false;
        world_.transition_started_tick = 0;
        return;
    }

    if (!world_.transition_armed) {
        world_.transition_armed = true;
        world_.transition_freeze = waves_depleted;
        world_.transition_started_tick = world_.frame_tick;
        for (auto& object : world_.objects) {
            if (object.active && object.type == ObjectType::PlayerCannon) {
                object.timer = 0;
            }
        }
    }
}

void Game::handle_level_transition_or_intermission()
{
    if (world_.gameplay_state == GameplayState::GameOver) {
        if (world_.screen == Screen::GameOver) {
            return;
        }
        world_.screen = Screen::GameOver;
        world_.game_over_frames_remaining = 300;
        return;
    }

    if (world_.gameplay_state == GameplayState::FinaleComplete) {
        for (auto& object : world_.objects) {
            if (object.active && object.type != ObjectType::PlayerCannon) {
                object.active = false;
                object.sound_id = 0;
            }
        }
        world_.screen = Screen::Finale;
        world_.finale_presenter_frame = 0;
        world_.finale_particle_rng_seed = world_.gameplay_rng_seed;
        world_.finale_presenter_timer_origin = world_.frame_tick;
        world_.finale_hardware_tick_schedule.clear();
        world_.finale_particle_sounds_spawned = 0;
        world_.finale_particle_sounds_burst = 0;
        world_.bunker_walk_sound_toggle = false;
        world_.game_over_frames_remaining =
            static_cast<int>(presenter_timing::finale_presenter_frames(world_));
        bind_presenter_sound(kSoundFinaleController);
        return;
    }

    const auto carried_landed_invaders = static_cast<std::uint16_t>(std::count_if(
        world_.objects.begin(), world_.objects.end(), [](const Object& object) {
            return object.active && !object.pending_destroy && object.type == ObjectType::LandedInvader;
        }));
    world_.screen = Screen::Intermission;
    world_.transition_freeze = true;
    world_.survivor_intermission_active = carried_landed_invaders > 0;
    world_.no_survivor_intermission_active = carried_landed_invaders == 0;
    world_.survivor_intermission_failure_mask = 0;
    world_.survivor_intermission_frame = 0;
    world_.survivor_intermission_timer_origin = world_.frame_tick;
    world_.survivor_native_presenter = world_.survivor_intermission_active;
    world_.survivor_presenter_update_batches.clear();
    world_.milestone_intermission = MilestoneIntermission::None;
    world_.milestone_intermission_frame = 0;
    if (world_.survivor_intermission_active) {
        world_.survivor_intermission_rng_seed = world_.gameplay_rng_seed;
        world_.survivor_intermission_failure_mask = original_survivor_failure_mask(
            world_.survivor_intermission_rng_seed, carried_landed_invaders);
        world_.intermission_frames_remaining = static_cast<int>(
            original_survivor_duration(
                carried_landed_invaders, world_.survivor_intermission_failure_mask));
        begin_live_survivor_intermission();
    } else {
        // 00bd visits inactive records too and tests the changing high-water.
        for (std::size_t slot = 0; slot < world_.objects.size() &&
             slot < world_.object_highwater.value_or(world_.objects.size()); ++slot) {
            const auto& object = world_.objects[slot];
            if (!object.active || object.type != ObjectType::PlayerCannon) {
                retire_object(slot);
            }
        }
        auto& flyby = ensure_presenter_sound_owner();
        flyby.position = {kGameplayWidth, 18};
        flyby.velocity = {-1, 0};
        flyby.extent = {};
        flyby.frame = static_cast<int>(presenter_timing::kNoSurvivorFlybyVideoFrames - 1);
        flyby.timer = 0;
        flyby.sprite_id = internals::kBannerFlybyAircraftSprite;
        flyby.assault_stage = 1;
        flyby.sound_id = kSoundNoSurvivorFlyby;
        world_.gameplay_state = GameplayState::Active;
        world_.waves_exhausted = false;
        world_.transition_armed = false;
        world_.intermission_frames_remaining =
            static_cast<int>(presenter_timing::kNoSurvivorFlybyVideoFrames);
    }
}

void Game::import_wave_record(std::size_t bank_index, const WaveRecord& record)
{
    auto& bank = world_.wave_banks[bank_index];
    bank.start_tick = record.start_tick;
    bank.stop_tick = record.stop_tick;
    bank.remaining_spawns = record.spawn_count;
    bank.min_delay = record.min_delay;
    bank.max_delay = record.max_delay;
    bank.paratrooper_cadence = record.paratrooper_cadence;
    bank.import_flag = record.spawn_count > 0;
    if (record.spawn_count == 0) {
        return;
    }
    bank.start_tick = world_.frame_tick + record.start_tick;
    if (bank.stop_tick != 0xffffffffu) {
        bank.stop_tick = world_.frame_tick + record.stop_tick;
    }
    const auto initial_trigger_tick =
        bank.start_tick + original_random_bounded(world_.gameplay_rng_seed, record.min_delay);
    bank.next_trigger_tick = initial_trigger_tick;
    bank.live_next_trigger_tick = initial_trigger_tick;
    bank.last_paratrooper_tick = 0;
}

void Game::update_wave_controller(std::size_t bank_index, AircraftVariant variant)
{
    auto& bank = world_.wave_banks[bank_index];

    if (world_.bunker_overrun_pending) {
        // The original controllers pause their countdown, not the deadline.
        ++bank.live_next_trigger_tick;
        return;
    }

    if (world_.frame_tick < bank.start_tick) {
        return;
    }

    if (world_.frame_tick < bank.live_next_trigger_tick) {
        return;
    }

    if (bank_index == 4) {
        const int direction_flag = original_random_bounded(world_.gameplay_rng_seed, 2);
        Object bomb {};
        bomb.active = true;
        bomb.type = ObjectType::SmartBomb;
        bomb.direction = direction_flag;
        bomb.position = direction_flag == 0 ? Vec2 {-kSmartBombArmedWidth, 49.0f}
                                            : Vec2 {kGameplayWidth, 49.0f};
        bomb.velocity = {direction_flag == 0 ? 1.0f : -1.0f, 0.0f};
        bomb.extent = {kSmartBombArmedWidth * 0.5f, kSmartBombArmedHeight * 0.5f};
        bomb.sound_id = kSoundSmartBombSpawn;
        bomb.timer = 0x6a + original_random_bounded(world_.gameplay_rng_seed, 0x6a);
        store_object(std::move(bomb));
    } else {
        int direction_flag = 0;
        switch (bank_index) {
        case 0:
            direction_flag = static_cast<int>((world_.frame_tick / 400u) & 1u);
            break;
        case 1:
            direction_flag = scheduled_direction_from_period(bank.live_next_trigger_tick, 250u);
            break;
        case 2:
            direction_flag = scheduled_direction_from_period(bank.live_next_trigger_tick, 400u);
            break;
        case 3:
            direction_flag = scheduled_direction_from_period(bank.live_next_trigger_tick, 250u);
            break;
        default:
            break;
        }
        spawn_aircraft(variant, direction_flag);
    }

    // 54f7 reschedules even a depleted bank until the cannon sets 4de8.
    if (bank.remaining_spawns != 0) {
        --bank.remaining_spawns;
    }
    bank.import_flag = false;
    const auto delay = next_wave_delay(bank);
    bank.live_next_trigger_tick = world_.frame_tick + delay;
}

void Game::update_finale_controller(Object& object)
{
    if (object.assault_stage == 1) {
        object.velocity = {};
        object.position.x += 1.0f;
        if (object.position.x > 0x107) {
            object.pending_destroy = true;
        } else {
            object.sprite_id = std::clamp(static_cast<int>((0x108 - object.position.x) / 0x14), 0, 4) + 0x28e;
        }
        return;
    }

    if (object.assault_stage == 2) {
        object.position.x -= 1.0f;
        if (object.position.x < 0xa4) {
            object.sprite_id = 0x293;
            object.assault_stage = 0;
            object.velocity = {-1.0f, 0.0f};
            object.timer = 0;
        } else {
            object.sprite_id = static_cast<int>((0x108 - object.position.x) / 0x14) + 0x28e;
        }
        return;
    }

    if (object.position.x < kFinaleControllerMinX) {
        object.velocity.x = 1.0f;
        object.position.x += object.velocity.x;
    } else if (object.position.x > kFinaleControllerMaxX) {
        object.velocity.x = -1.0f;
        object.position.x += object.velocity.x;
    }

    if (world_.finale_spawn_budget_remaining > 0 && object.timer % kFinaleSpawnCadenceTicks == 0) {
        const auto spawn_position =
            WorldPosition {object.position.x + kFinaleSpawnOffsetX, object.position.y + kFinaleSpawnOffsetY};
        --world_.finale_spawn_budget_remaining;
        spawn_finale_drop(spawn_position, false);
    }

    if (world_.finale_spawn_budget_remaining == 0) {
        object.assault_stage = 1;
        object.velocity = {};
        object.sprite_id = 0x292;
    }
}

void Game::spawn_aircraft(AircraftVariant variant, int direction_flag)
{
    Object aircraft {};
    aircraft.active = true;
    aircraft.type = ObjectType::Aircraft;
    aircraft.aircraft_variant = variant;
    aircraft.wave_bank = [&]() -> std::size_t {
        switch (variant) {
        case AircraftVariant::D:
            return 0;
        case AircraftVariant::A:
            return 1;
        case AircraftVariant::B:
            return 2;
        case AircraftVariant::C:
            return 3;
        }
        return 0;
    }();
    aircraft.direction = direction_flag == 0 ? 1 : -1;
    const auto width = aircraft_width_for_variant(variant);
    const auto half_width = width * 0.5f;
    aircraft.position = aircraft.direction > 0
                            ? Vec2 {-width, lane_y_for_variant(variant)}
                            : Vec2 {kGameplayWidth, lane_y_for_variant(variant)};
    aircraft.velocity = {aircraft.direction > 0 ? aircraft_speed_for_variant(variant)
                                                : -aircraft_speed_for_variant(variant),
                         0.0f};
    aircraft.extent = {half_width, 7.0f};
    aircraft.sound_id = aircraft_sound_id(variant);
    aircraft.last_drop_tick = world_.frame_tick;
    store_object(std::move(aircraft));
}

void Game::spawn_paratrooper_from_aircraft(const Object& aircraft)
{
    const auto local_offset = drop_offset_for_variant(aircraft.aircraft_variant, aircraft.direction);
    const auto drop_x = aircraft.position.x + local_offset;

    spawn_paratrooper({drop_x,
                       aircraft.position.y + drop_y_offset_for_variant(aircraft.aircraft_variant)},
                      aircraft.velocity.x * 0.5f,
                      aircraft.aircraft_variant,
                      aircraft.wave_bank);
}

void Game::spawn_paratrooper(const WorldPosition& position,
                             float drift_x,
                             AircraftVariant source_variant,
                             std::size_t source_bank)
{
    Object paratrooper {};
    paratrooper.active = true;
    paratrooper.type = ObjectType::Paratrooper;
    paratrooper.aircraft_variant = source_variant;
    paratrooper.wave_bank = source_bank;
    paratrooper.position = position;
    paratrooper.velocity = {drift_x, kParatrooperInitialFallSpeed};
    paratrooper.direction = drift_x < 0.0f ? -1 : 1;
    paratrooper.extent = {kParatrooperOriginalWidth * 0.5f, kParatrooperOriginalHeight * 0.5f};
    paratrooper.parachute_lost = false;
    store_object(std::move(paratrooper));
}

void Game::spawn_finale_drop(const WorldPosition& position, bool immediate_fall)
{
    Object drop {};
    drop.active = true;
    drop.type = ObjectType::Paratrooper;
    drop.position = position;
    drop.velocity = {};
    drop.extent = {3.0f, 7.5f};
    drop.finale_drop = true;
    drop.assault_stage = 0;
    drop.timer = 0;
    drop.frame = 0;
    if (immediate_fall) {
        drop.assault_stage = 1;
        drop.parachute_lost = true;
        drop.velocity.y = kParatrooperFreeFallSpeed;
        drop.extent = {2.5f, 3.0f};
    }
    store_object(std::move(drop));
}

void Game::spawn_enemy_death(const Object& source)
{
    play_sound(kSoundEnemyDeath);

    Object death {};
    death.active = true;
    death.type = ObjectType::EnemyDeath;
    death.aircraft_variant = source.aircraft_variant;
    death.position = source.position;
    death.velocity = source.velocity;
    death.extent = source.extent;
    death.direction = source.direction;
    death.sprite_id = 0x1c7;
    death.timer = 0;
    death.frame = 0;
    death.has_dropped_payload = source.type == ObjectType::SmartBomb;
    death.finale_drop = source.type == ObjectType::SmartBomb && source.frame == 0;
    death.assault_stage = source.type == ObjectType::Aircraft
                              ? aircraft_death_frame_limit(source.aircraft_variant)
                              : 6;
    store_object(std::move(death));
}

void Game::convert_aircraft_to_debris(Object& aircraft)
{
    const auto source = aircraft;
    aircraft.type = ObjectType::AircraftDebris;
    aircraft.timer = 0;
    // 0bf2/0f09/1221/152d retain the source frame and aircraft draw callback.
    aircraft.assault_stage = aircraft_debris_shell_delay(aircraft.aircraft_variant) - 1;
    aircraft.has_dropped_payload = false;
    spawn_enemy_death(source);
}

void Game::spawn_aircraft_fragment(const Object& shell, int index)
{
    Object fragment {};
    fragment.active = true;
    fragment.type = ObjectType::AircraftDebris;
    const bool large_family = shell.aircraft_variant == AircraftVariant::D ||
                              shell.aircraft_variant == AircraftVariant::B;
    if (index == 0) {
        fragment.sprite_id = large_family ? 0x121 : 0x143;
    } else {
        const auto family = original_random_bounded(world_.gameplay_rng_seed, large_family ? 3 : 4);
        const bool use_small_family = large_family ? family == 1 : family != 1;
        const int base = use_small_family ? 0x143 : 0x121;
        fragment.sprite_id = base + 8 + original_random_bounded(world_.gameplay_rng_seed, 2) * 8;
    }
    fragment.aircraft_variant = shell.aircraft_variant;
    fragment.direction = shell.direction;
    fragment.position = shell.position;
    const auto offset = aircraft_debris_fragment_offset(shell.aircraft_variant, shell.direction);
    fragment.position.x += offset.x;
    fragment.position.y += offset.y;
    fragment.velocity = shell.velocity;

    const float horizontal_impulse = original_random_fixed(world_.gameplay_rng_seed, 3, 0x8000) / 65536.0f;
    const float vertical_impulse = original_random_fixed(world_.gameplay_rng_seed, 1, -0x4000) / 65536.0f;
    // 08ea also offsets the initial X by five times the generated X delta.
    fragment.position.x += 5.0f * horizontal_impulse;
    fragment.velocity.x += horizontal_impulse;
    fragment.velocity.y += vertical_impulse;

    fragment.extent = {3.0f, 3.0f};
    fragment.timer = 0;
    fragment.frame = 0;
    fragment.assault_stage = 0;
    fragment.has_dropped_payload = true;
    store_object(std::move(fragment));
}

void Game::scatter_aircraft_debris(const Object& shell)
{
    for (int index = 0; index < 4; ++index) {
        spawn_aircraft_fragment(shell, index);
    }
}

void Game::spawn_resolution_particles(const Object& source)
{
    spawn_resolution_particles(source, source);
}

void Game::spawn_bunker_resolution_particles(const Object& source)
{
    // The original's stationary type-2 cannon is the second argument to 026e.
    Object cannon {};
    cannon.type = ObjectType::PlayerCannon;
    spawn_resolution_particles(source, cannon);
}

void Game::spawn_resolution_particles(const Object& source, const Object& impact)
{
    play_sound(kSoundGroundedResolution);

    // Allocating child slots must not keep reading a source reference that may
    // be invalidated by vector growth.
    const bool hit_cannon = impact.type == ObjectType::PlayerCannon;
    auto source_position = hit_cannon ? source.position : impact.position;
    const auto source_velocity = impact.velocity;
    std::optional<std::size_t> impact_slot;
    for (std::size_t slot = 0; slot < world_.objects.size(); ++slot) {
        if (&world_.objects[slot] == &impact) {
            impact_slot = slot;
            break;
        }
    }
    if (hit_cannon || &source == &impact) {
        // 026e offsets only self-resolution/cannon hits. Other collisions use
        // the impact actor's position; 01e0 always inherits its velocity.
        source_position.x += 5.0f;
        source_position.y += 12.0f;
    }

    for (int index = 0; index < 8; ++index) {
        std::array<std::uint16_t, 4> random_values {};
        for (auto& value : random_values) {
            value = original_random_next(world_.gameplay_rng_seed);
        }
        const auto setup = internals::resolution_particle_setup(random_values);
        Object particle {};
        particle.active = true;
        particle.type = ObjectType::ResolutionParticle;
        particle.position = source_position;
        particle.extent = {0.0f, 0.0f};
        particle.frame = setup.color;
        particle.sprite_id = setup.color;
        particle.assault_stage = setup.lifetime;
        const auto slot = store_object(std::move(particle));
        // 56f3 clears the allocated record before 01e0 reads live impact
        // velocity. Reusing that slot changes this and subsequent children.
        const auto velocity = impact_slot ? world_.objects[*impact_slot].velocity : source_velocity;
        world_.objects[slot].velocity = {velocity.x + setup.vx / 65536.0f,
                                        velocity.y + setup.vy / 65536.0f};
    }
}

void Game::spawn_finale_controller()
{
    Object controller {};
    controller.active = true;
    controller.type = ObjectType::FinaleController;
    controller.position = {kFinaleControllerEntryX, kFinaleControllerY};
    controller.velocity = {};
    controller.extent = {14.0f, 8.0f};
    controller.direction = -1;
    controller.sprite_id = 0x28e;
    controller.assault_stage = 2;
    store_object(std::move(controller));
}

void Game::spawn_player_projectile()
{
    auto player_it = std::find_if(world_.objects.begin(), world_.objects.end(), [](const Object& object) {
        return object.type == ObjectType::PlayerCannon;
    });

    if (player_it == world_.objects.end()) {
        return;
    }

    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.frame = player_it->frame;
    projectile.velocity = projectile_velocity_for_frame(player_it->frame);
    if (world_.fast_shots_enabled) {
        projectile.velocity.x *= 2.0f;
        projectile.velocity.y *= 2.0f;
    }
    projectile.position = {kProjectileSpawnX + static_cast<double>(projectile.velocity.x) * kProjectileMuzzleOffset,
                           kProjectileSpawnY + static_cast<double>(projectile.velocity.y) * kProjectileMuzzleOffset};
    projectile.extent = {1.0f, 1.0f};
    world_.muzzle_flash_position = {projectile.position.x - projectile.velocity.x - 1.0f,
                                    projectile.position.y - projectile.velocity.y - 1.0f};
    world_.muzzle_flash_frames_remaining = 1;
    muzzle_flash_pending_draw_ = true;
    store_object(std::move(projectile));
    add_to_score(-1);
    play_sound(kSoundPlayerFire);
}

void Game::add_to_score(int amount)
{
    world_.scores.score = std::max<std::int32_t>(0, world_.scores.score + amount);
}

std::size_t Game::store_object(Object object)
{
    if (!world_.object_highwater) {
        world_.object_highwater = world_.objects.size();
    }
    assign_presentation_id(object);
    if (reusable_object_slot_) {
        const auto index = *reusable_object_slot_;
        reusable_object_slot_.reset();
        if (index < world_.objects.size() && !world_.objects[index].active) {
            world_.objects[index] = std::move(object);
            world_.object_highwater = std::max(*world_.object_highwater, index + 1);
            return index;
        }
    }
    const auto free_slot = std::find_if(
        world_.objects.begin(), world_.objects.end(),
        [](const Object& candidate) { return !candidate.active; });
    if (free_slot != world_.objects.end()) {
        const auto index = static_cast<std::size_t>(free_slot - world_.objects.begin());
        *free_slot = std::move(object);
        world_.object_highwater = std::max(*world_.object_highwater, index + 1);
        return index;
    }
    world_.objects.push_back(std::move(object));
    world_.object_highwater = world_.objects.size();
    return world_.objects.size() - 1;
}

void Game::retire_object(std::size_t index)
{
    // 567f retains secondary identity/geometry and remembers the lowest slot
    // freed since 0040 last consumed the cache, not the oldest table hole.
    if (!world_.object_highwater) {
        world_.object_highwater = world_.objects.size();
    }
    auto& object = world_.objects[index];
    object.active = false;
    object.pending_destroy = false;
    if (object.type == ObjectType::ResolutionParticle) {
        object.frame = 0xffff;
        object.sprite_id = -1;
    }
    object.sound_id = 0;
    if (index + 1 == *world_.object_highwater) {
        --*world_.object_highwater;
    }
    if (!reusable_object_slot_ || index < *reusable_object_slot_) {
        reusable_object_slot_ = index;
    }
}

void Game::assign_presentation_id(Object& object)
{
    object.presentation_id = next_presentation_id_++;
    if (next_presentation_id_ == 0) {
        next_presentation_id_ = 1;
    }
}

void Game::play_sound(std::uint16_t sound_id)
{
    world_.sound_events.push_back(sound_id);
}

void Game::stop_sound(std::uint16_t sound_id)
{
    world_.sound_stop_events.push_back(sound_id);
}

Object& Game::ensure_presenter_sound_owner()
{
    const auto existing = std::find_if(
        world_.objects.begin(), world_.objects.end(), [](const Object& object) {
            return object.active && object.type == ObjectType::Presenter;
        });
    if (existing != world_.objects.end()) {
        return *existing;
    }

    Object presenter {};
    presenter.active = true;
    presenter.type = ObjectType::Presenter;
    store_object(std::move(presenter));
    return *std::find_if(
        world_.objects.begin(), world_.objects.end(), [](const Object& object) {
            return object.active && object.type == ObjectType::Presenter;
        });
}

void Game::bind_presenter_sound(std::uint16_t sound_id)
{
    ensure_presenter_sound_owner().sound_id = sound_id;
}

void Game::stop_presenter_sound()
{
    const auto owner = std::find_if(
        world_.objects.begin(), world_.objects.end(), [](const Object& object) {
            return object.active && object.type == ObjectType::Presenter;
        });
    if (owner != world_.objects.end()) {
        owner->sound_id = 0;
    }
}

void Game::interrupt_bunker_assault()
{
    world_.overrun_retained_page = RetainedPage {world_.objects, world_.frame_tick};
    // 4cfb releases its current assaulter after Escape ends the blocking walk.
    for (std::size_t index = 0; index < world_.objects.size(); ++index) {
        const auto& object = world_.objects[index];
        if (object.active && !object.pending_destroy &&
            object.type == ObjectType::LandedInvader && object.assaulting) {
            retire_object(index);
            break;
        }
    }

    world_.reset_sound_effects = true;
    world_.fatal_retained_page.reset();
    world_.player_dead = true;
    world_.bunker_overrun_pending = false;
    world_.transition_freeze = false;
    world_.transition_armed = false;
    world_.transition_started_tick = world_.frame_tick;
    world_.overrun_interrupt_residue = true;
    world_.death_flash_frames_remaining = 0;
    world_.screen = Screen::GameOver;
    world_.death_palette_restored = false;
    world_.game_over_frames_remaining = kOverrunGameOverTicks;
    world_.game_over_input_latched = false;
    world_.gameplay_state = GameplayState::GameOver;
}

void Game::trigger_bunker_hit_from_enemy(Object& source)
{
    if (world_.gameplay_state == GameplayState::GameOver) {
        source.pending_destroy = true;
        return;
    }

    source.pending_destroy = true;

    world_.player_dead = true;
    world_.bunker_overrun_pending = false;
    world_.bunker_assault_active = false;
    world_.overrun_interrupt_residue = false;
    world_.bunker_white_flag = false;
    world_.bunker_assault_cleanup_pending = false;
    world_.transition_freeze = false;
    world_.transition_armed = false;
    world_.transition_started_tick = world_.frame_tick;
    world_.bunker_assault_step_tick = world_.frame_tick;
    world_.bunker_special_effect_frames_remaining = 0;
    world_.bunker_assault_entries = 0;
    world_.bunker_explosion_frames_remaining = 0;
    world_.bunker_terminal_explosion_frames_total = 0;
    world_.death_flash_frames_remaining = kDeathFlashTicks;
    world_.death_palette_restored = false;
    world_.fatal_retained_page.reset();
    world_.overrun_retained_page.reset();
    world_.screen = Screen::Gameplay;
    world_.game_over_frames_remaining = 300;
    world_.game_over_input_latched = false;
    world_.gameplay_state = GameplayState::GameOver;
}

void Game::trigger_player_death()
{
    world_.fatal_retained_page.reset();
    world_.overrun_retained_page.reset();
    world_.player_dead = true;
    world_.bunker_assault_active = false;
    world_.overrun_interrupt_residue = false;
    world_.bunker_white_flag = false;
    world_.transition_freeze = false;
    world_.transition_armed = false;
    world_.transition_started_tick = world_.frame_tick;
    world_.bunker_explosion_frames_remaining = 0;
    world_.bunker_terminal_explosion_frames_total = 0;
    world_.bunker_special_effect_frames_remaining = 0;
    world_.death_flash_frames_remaining = 0;
    world_.screen = Screen::GameOver;
    world_.death_palette_restored = false;
    world_.game_over_frames_remaining = 300;
    world_.game_over_input_latched = false;
    world_.gameplay_state = GameplayState::GameOver;
    play_sound(kSoundTerminalExplosion);
}

bool Game::start_or_fire_pressed(const InputState& input) const
{
    return (input.start && !previous_input_.start) || (input.fire && !previous_input_.fire);
}

bool Game::any_keyboard_pressed(const InputState& input) const
{
    return (input.move_left && !previous_input_.move_left) ||
           (input.move_right && !previous_input_.move_right) ||
           (input.move_up && !previous_input_.move_up) ||
           (input.move_down && !previous_input_.move_down) ||
           (input.fire && !previous_input_.fire) ||
           (input.start && !previous_input_.start) ||
           (input.name_submit && !previous_input_.name_submit) ||
           (input.backspace && !previous_input_.backspace) ||
           (input.accept && !previous_input_.accept) ||
           (input.menu && !previous_input_.menu) ||
           (input.escape && !previous_input_.escape) ||
           (input.decline && !previous_input_.decline) ||
           (input.gamepad_accept && !previous_input_.gamepad_accept) ||
           (input.gamepad_back && !previous_input_.gamepad_back) ||
           (input.gamepad_start && !previous_input_.gamepad_start) ||
           (input.gamepad_name_up && !previous_input_.gamepad_name_up) ||
           (input.gamepad_name_down && !previous_input_.gamepad_name_down) ||
           !input.text_input.empty();
}

bool Game::has_airborne_hostiles() const
{
    return std::any_of(world_.objects.begin(), world_.objects.end(), [](const Object& object) {
        return object.active && !object.pending_destroy &&
               (object.type == ObjectType::Aircraft || object.type == ObjectType::SmartBomb ||
                object.type == ObjectType::Paratrooper || object.type == ObjectType::GroundedTransition ||
                object.type == ObjectType::FinaleController);
    });
}

bool Game::object_table_is_clearable() const
{
    return std::all_of(world_.objects.begin(), world_.objects.end(), [](const Object& object) {
        return !object.active || object.pending_destroy || object_type_allows_level_clear(object.type);
    });
}

std::uint32_t Game::next_wave_delay(const RuntimeWaveState& bank)
{
    return bank.min_delay + original_random_bounded(
        world_.gameplay_rng_seed, static_cast<std::uint16_t>(bank.max_delay - bank.min_delay));
}

const std::array<std::array<WaveRecord, WorldState::kNormalLevelCount>, WorldState::kWaveBankCount>&
Game::wave_table() const
{
    return wave_table_;
}

}  // namespace niteraid
