// Regression coverage for the original type-7 collision boundary described
// in research/wave6-debris-collision-audit-2026-09-11.md.
//
// The original collision loop gates dispatch on the outer actor's type flags,
// then calls the inner actor's callback after an AABB overlap. The recovered
// aircraft callbacks accept that source; type 6 has no collision-source flag.
// These tests use only public Game state and tick(), and intentionally do not
// assert RNG draws or enhanced persistent-debris behavior.

#include "niteraid/game.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

using niteraid::AircraftVariant;
using niteraid::Game;
using niteraid::GameplayState;
using niteraid::InputState;
using niteraid::Object;
using niteraid::ObjectType;
using niteraid::Screen;
using niteraid::Vec2;
using niteraid::WorldState;

TEST_CASE("wave controllers retain zero dimensions at the captured projectile contact")
{
    Game game(std::uint16_t {1});
    auto& world = game.diagnostic_world();
    for (const auto& object : world.objects) {
        if (object.type == ObjectType::WaveController) {
            CHECK_EQ(object.extent.x, 0.0f);
            CHECK_EQ(object.extent.y, 0.0f);
        }
    }
    for (auto& bank : world.wave_banks) {
        bank.next_trigger_tick = 10000;
        bank.live_next_trigger_tick = 10000;
    }
    world.frame_tick = 1574;
    Object shot {};
    shot.active = true;
    shot.type = ObjectType::PlayerProjectile;
    // Previous position and velocity from original update 6923, wave two.
    shot.position = {4.57470703125, 3.69921875};
    shot.velocity = {-0.6938629150390625f, -0.7200927734375f};
    shot.extent = {1, 1};
    const auto slot = world.objects.size();
    world.objects.push_back(shot);
    world.object_highwater = world.objects.size();
    const auto seed = world.gameplay_rng_seed;
    const auto highwater = *world.object_highwater;

    game.tick(InputState {});

    CHECK(world.objects[slot].active);
    CHECK(!world.objects[slot].pending_destroy);
    CHECK_EQ(world.objects[slot].position.x, 3.8808441162109375);
    CHECK_EQ(world.objects[slot].position.y, 2.9791259765625);
    CHECK_EQ(world.gameplay_rng_seed, seed);
    CHECK_EQ(*world.object_highwater, highwater);
}

TEST_CASE("projectile contact uses strict raw edges rather than a padded draw box")
{
    constexpr double kFixedUnit = 1.0 / 65536.0;
    for (const double offset : {0.0, -kFixedUnit, kFixedUnit}) {
        Game game(std::uint16_t {0});
        auto& world = game.diagnostic_world();
        world.objects.clear();
        world.object_highwater.reset();
        world.wave_banks = {};
        Object aircraft {};
        aircraft.active = true;
        aircraft.type = ObjectType::Aircraft;
        aircraft.aircraft_variant = AircraftVariant::D;
        aircraft.position = {100, 40};
        Object shot {};
        shot.active = true;
        shot.type = ObjectType::PlayerProjectile;
        shot.position = {169.0 + offset, 45};
        shot.extent = {1, 1};
        world.objects = {shot, aircraft};
        game.tick(InputState {});
        CHECK_EQ(world.scores.enemy_kills, offset < 0 ? 1u : 0u);
    }
}

namespace {

void isolate_collision_world(Game& game)
{
    auto& world = game.diagnostic_world();
    world.screen = Screen::Gameplay;
    world.gameplay_state = GameplayState::Active;
    world.objects.clear();
    world.wave_banks = {};
    world.finale_spawn_budget_remaining = 0;
    world.finale_spawn_budget_total = 0;
    world.waves_exhausted = false;
    world.transition_armed = false;
    world.transition_freeze = false;
    world.bunker_assault_active = false;
    world.bunker_overrun_pending = false;
}

Object make_fragment(Vec2 position)
{
    Object fragment {};
    fragment.active = true;
    fragment.type = ObjectType::AircraftDebris;
    fragment.aircraft_variant = AircraftVariant::D;
    fragment.position = position;
    // A falling fragment receives +1/32 Y acceleration during actor update.
    // Cancel it here so the overlap probe does not depend on actor motion.
    fragment.velocity = {0.0f, -0.03125f};
    fragment.extent = {3.0f, 3.0f};
    fragment.sprite_id = 0x121;
    fragment.frame = 0;
    fragment.has_dropped_payload = true;
    return fragment;
}

Object make_scattering_shell(Vec2 position, AircraftVariant variant = AircraftVariant::C)
{
    Object shell {};
    shell.active = true;
    shell.type = ObjectType::AircraftDebris;
    shell.aircraft_variant = variant;
    shell.position = position;
    shell.velocity = {0.0f, 1.0f};
    shell.direction = 1;
    shell.assault_stage = 0;
    shell.has_dropped_payload = false;
    return shell;
}

Object make_aircraft(AircraftVariant variant, Vec2 position)
{
    Object aircraft {};
    aircraft.active = true;
    aircraft.type = ObjectType::Aircraft;
    aircraft.aircraft_variant = variant;
    aircraft.position = position;
    aircraft.velocity = {};
    aircraft.direction = 1;
    aircraft.extent = {34.5f, 7.0f};
    return aircraft;
}

Object make_smart_bomb(bool falling, Vec2 position)
{
    Object bomb {};
    bomb.active = true;
    bomb.type = ObjectType::SmartBomb;
    bomb.position = position;
    bomb.velocity = {};
    bomb.direction = 0;
    bomb.timer = 1;
    bomb.extent = falling ? Vec2 {10.0f, 7.0f} : Vec2 {8.5f, 2.0f};
    return bomb;
}

Object make_type6_source(Vec2 position)
{
    Object death {};
    death.active = true;
    death.type = ObjectType::EnemyDeath;
    death.position = position;
    death.velocity = {};
    death.extent = {10.0f, 10.0f};
    death.assault_stage = 6;
    death.timer = 1;
    return death;
}

Object make_expiring_enemy_death()
{
    auto death = make_type6_source({10.0f, 20.0f});
    death.frame = death.assault_stage;
    death.timer = 0;
    return death;
}

Object make_culled_fragment()
{
    return make_fragment({1000.0f, 100.0f});
}

Object make_culled_projectile()
{
    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = {1000.0f, 100.0f};
    projectile.extent = {1.5f, 1.5f};
    return projectile;
}

Object make_type6_negative_target(ObjectType type, Vec2 position)
{
    Object target {};
    target.active = true;
    target.type = type;
    target.position = position;
    target.velocity = {};
    switch (type) {
    case ObjectType::Aircraft:
        target.aircraft_variant = AircraftVariant::C;
        target.extent = {22.0f, 7.0f};
        break;
    case ObjectType::SmartBomb:
        target.timer = 1;
        target.extent = {10.0f, 7.0f};
        break;
    case ObjectType::Paratrooper:
        target.timer = 100;
        target.extent = {3.0f, 7.5f};
        break;
    case ObjectType::GroundedTransition:
        target.extent = {2.5f, 3.0f};
        break;
    case ObjectType::LandedInvader:
        target.extent = {2.0f, 3.5f};
        break;
    default:
        break;
    }
    return target;
}

int live_count(const WorldState& world, ObjectType type)
{
    return static_cast<int>(std::count_if(
        world.objects.begin(), world.objects.end(), [type](const Object& object) {
            return object.active && !object.pending_destroy && object.type == type;
        }));
}

int aircraft_score(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D:
    case AircraftVariant::B:
        return 5;
    case AircraftVariant::A:
    case AircraftVariant::C:
        return 10;
    }
    return 0;
}

}  // namespace

TEST_CASE("enemy death advances on the first update using old timer parity")
{
    for (const int frame_limit : {6, 9}) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        auto death = make_type6_source({100.0f, 100.0f});
        death.timer = 0;
        death.frame = 0;
        death.assault_stage = frame_limit;
        world.objects.push_back(death);

        game.tick(InputState {});
        CHECK_EQ(world.objects[0].timer, 1u);
        CHECK_EQ(world.objects[0].frame, 1);

        game.tick(InputState {});
        CHECK_EQ(world.objects[0].timer, 2u);
        CHECK_EQ(world.objects[0].frame, 1);
    }
}

TEST_CASE("fragment movement retains odd fixed units through 256 over multiple updates")
{
    constexpr double fixed_unit = 1.0 / 65536.0;
    constexpr double initial_x = 256.0 - fixed_unit;
    constexpr double initial_y = 100.0 + fixed_unit;
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();
    auto fragment = make_fragment({0, 0});
    fragment.position = {static_cast<decltype(fragment.position.x)>(initial_x),
                         static_cast<decltype(fragment.position.y)>(initial_y)};
    fragment.velocity.x = 1.0f / 65536.0f;
    world.objects.push_back(fragment);

    for (int update = 1; update <= 8; ++update) {
        game.tick(InputState {});
        CHECK(world.objects[0].active);
        CHECK_EQ(world.objects[0].position.x, initial_x + update * fixed_unit);
        CHECK_EQ(world.objects[0].position.y, initial_y + update * (update - 1) / 64.0);
        CHECK_EQ(world.objects[0].velocity.x, fixed_unit);
        CHECK_EQ(world.objects[0].velocity.y, (update - 1) / 32.0);
    }
}

TEST_CASE("fragment factory inherits fractional shell coordinates without narrowing")
{
    constexpr double fixed_unit = 1.0 / 65536.0;
    constexpr double parent_x = 256.0 + fixed_unit;
    constexpr double parent_y = 100.0 + fixed_unit;
    constexpr double first_impulse = -30692.0 / 65536.0;
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();
    world.gameplay_rng_seed = 1;
    auto shell = make_scattering_shell({0, 0});
    shell.position = {static_cast<decltype(shell.position.x)>(parent_x),
                      static_cast<decltype(shell.position.y)>(parent_y)};
    shell.velocity = {};
    world.objects.push_back(shell);
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    CHECK_EQ(world.objects.size(), std::size_t {5});
    if (world.objects.size() != 5) {
        return;
    }
    CHECK(!world.objects[0].active);
    const auto& child = world.objects[1];
    CHECK(child.active);
    CHECK_EQ(child.timer, 1u);
    CHECK_EQ(child.velocity.x, first_impulse);
    // C-shell offset, five impulse steps in the factory, then one live update.
    CHECK_EQ(child.position.x, parent_x + 10.0 + 6.0 * first_impulse);
    CHECK_EQ(child.position.y, parent_y + child.velocity.y);
}

TEST_CASE("all aircraft fragments retain original inherited horizontal velocity")
{
    constexpr float kFirstImpulse = -30692.0f / 65536.0f;
    for (const auto variant : {AircraftVariant::A, AircraftVariant::B,
                               AircraftVariant::C, AircraftVariant::D}) {
        for (const float aircraft_x : {-1.0f, 1.0f}) {
            Game game(std::uint16_t {12});
            isolate_collision_world(game);
            auto& world = game.diagnostic_world();
            world.gameplay_rng_seed = 1;
            auto shell = make_scattering_shell({100.0f, 40.0f}, variant);
            shell.velocity = {aircraft_x, 0.0f};
            world.objects.push_back(shell);

            game.tick(InputState {});

            CHECK_EQ(world.objects.size(), std::size_t {5});
            if (world.objects.size() != 5) {
                continue;
            }
            CHECK_EQ(world.objects[1].velocity.x, aircraft_x + kFirstImpulse);
        }
    }
}

TEST_CASE("fragment animation cannot expand or shrink its live collision box")
{
    for (const int frame : {0, 1, 2}) {
        for (const float target_x : {118.5f, 119.0f, 120.0f}) {
            Game game(std::uint16_t {12});
            isolate_collision_world(game);
            auto& world = game.diagnostic_world();
            auto fragment = make_fragment({100.0f, 100.0f});
            fragment.frame = frame;
            fragment.timer = 1;
            world.objects.push_back(fragment);
            world.objects.push_back(make_aircraft(AircraftVariant::D, {target_x, 100.0f}));

            game.tick(InputState {});

            // Resource 0x121 binds a 19-pixel width, regardless of its draw pose.
            CHECK_EQ(world.scores.enemy_kills, target_x < 119.0f ? 1u : 0u);
        }
    }
}

TEST_CASE("type-7 dispatch distinguishes touching from one fixed unit overlap")
{
    constexpr double fixed_unit = 1.0 / 65536.0;
    for (const double overlap : {0.0, fixed_unit}) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        world.objects.push_back(make_fragment({249, 100}));
        auto aircraft = make_aircraft(AircraftVariant::A, {268, 100});
        aircraft.position.x = static_cast<decltype(aircraft.position.x)>(268.0 - overlap);
        world.objects.push_back(aircraft);

        game.tick(InputState {});

        CHECK_EQ(world.scores.enemy_kills, overlap == 0.0 ? 0 : 1);
        CHECK_EQ(world.objects[1].type == ObjectType::AircraftDebris, overlap != 0.0);
    }
}

TEST_CASE("type-7 wreckage kills all four aircraft families and creates secondary shells")
{
    for (const auto variant : {AircraftVariant::A, AircraftVariant::B,
                               AircraftVariant::C, AircraftVariant::D}) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        const auto position = Vec2 {100.0f, 100.0f};
        world.objects.push_back(make_fragment(position));
        const auto target_index = world.objects.size();
        auto aircraft = make_aircraft(variant, position);
        aircraft.frame = 3;
        world.objects.push_back(aircraft);
        world.objects.shrink_to_fit();

        game.tick(InputState {});

        // 0bf2/0f09/1221/152d clone the aircraft into type 6, then mutate the
        // impacted slot to type 7 and award the variant's aircraft score.
        CHECK(world.objects[target_index].active);
        CHECK(!world.objects[target_index].pending_destroy);
        CHECK(world.objects[target_index].type == ObjectType::AircraftDebris);
        CHECK_EQ(world.objects[target_index].frame, 3);
        CHECK_EQ(live_count(world, ObjectType::AircraftDebris), 2);
        CHECK_EQ(live_count(world, ObjectType::EnemyDeath), 1);
        const auto death = std::find_if(world.objects.begin(), world.objects.end(),
                                        [](const Object& object) {
                                            return object.active && object.type == ObjectType::EnemyDeath;
                                        });
        CHECK(death != world.objects.end());
        if (death != world.objects.end()) {
            const int frame_limit = variant == AircraftVariant::D || variant == AircraftVariant::B ? 9 : 6;
            CHECK_EQ(death->assault_stage, frame_limit);
            CHECK_EQ(death->frame, 0);
        }
        CHECK_EQ(world.scores.score, aircraft_score(variant));
        CHECK_EQ(world.scores.enemy_kills, 1);

        const auto clock = world.frame_tick;
        const auto seed = world.gameplay_rng_seed;
        const auto sounds = world.sound_events;
        game.prepare_gameplay_page();
        const int drawn_phase = variant == AircraftVariant::D ? 4 :
                                variant == AircraftVariant::A ? 0 : 3;
        CHECK_EQ(world.objects[target_index].frame, drawn_phase);
        CHECK_EQ(world.objects[0].frame, 0);
        CHECK_EQ(world.frame_tick, clock);
        CHECK_EQ(world.gameplay_rng_seed, seed);
        CHECK(world.sound_events == sounds);

        // The original slot remains a collision source, but the converted
        // target is no longer an aircraft, so a second tick cannot re-award it.
        game.tick(InputState {});
        CHECK_EQ(world.scores.score, aircraft_score(variant));
        CHECK_EQ(world.scores.enemy_kills, 1);
        CHECK_EQ(live_count(world, ObjectType::AircraftDebris), 2);
    }
}

TEST_CASE("one type-7 source survives multiple overlapping aircraft kills")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();
    const auto position = Vec2 {100.0f, 100.0f};
    world.objects.push_back(make_fragment(position));
    world.objects.push_back(make_aircraft(AircraftVariant::D, position));
    world.objects.push_back(make_aircraft(AircraftVariant::A, position));
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    CHECK(world.objects[0].active);
    CHECK(!world.objects[0].pending_destroy);
    CHECK(world.objects[0].type == ObjectType::AircraftDebris);
    CHECK_EQ(live_count(world, ObjectType::Aircraft), 0);
    CHECK_EQ(live_count(world, ObjectType::AircraftDebris), 3);
    CHECK_EQ(live_count(world, ObjectType::EnemyDeath), 2);
    CHECK_EQ(world.scores.score, 15);
    CHECK_EQ(world.scores.enemy_kills, 2);

    game.tick(InputState {});
    CHECK_EQ(world.scores.score, 15);
    CHECK_EQ(world.scores.enemy_kills, 2);
    CHECK_EQ(live_count(world, ObjectType::AircraftDebris), 3);
}

TEST_CASE("type-7 wreckage kills armed and falling smart bombs in place")
{
    for (const bool falling : {false, true}) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        const auto position = Vec2 {100.0f, 100.0f};
        world.objects.push_back(make_fragment(position));
        const auto target_index = world.objects.size();
        world.objects.push_back(make_smart_bomb(falling, position));
        world.objects.shrink_to_fit();

        game.tick(InputState {});

        // 1c23 converts the smart-bomb target itself to type 6 for a non-cannon
        // qualifying impact. It does not require a player projectile source.
        CHECK(world.objects[target_index].active);
        CHECK(!world.objects[target_index].pending_destroy);
        CHECK(world.objects[target_index].type == ObjectType::EnemyDeath);
        CHECK_EQ(live_count(world, ObjectType::EnemyDeath), 1);
        CHECK_EQ(live_count(world, ObjectType::AircraftDebris), 1);
        CHECK_EQ(world.scores.score, 10);
        CHECK_EQ(world.scores.enemy_kills, 1);

        game.tick(InputState {});
        CHECK_EQ(world.scores.score, 10);
        CHECK_EQ(world.scores.enemy_kills, 1);
    }
}

TEST_CASE("type-7 aircraft collision is independent of slot order and allocation")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();
    const auto position = Vec2 {100.0f, 100.0f};
    const auto target_index = world.objects.size();
    world.objects.push_back(make_aircraft(AircraftVariant::C, position));
    const auto source_index = world.objects.size();
    world.objects.push_back(make_fragment(position));
    // The aircraft callback allocates a type-6 effect. Force that append to
    // exercise the index/copy protection rather than relying on vector slack.
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    CHECK(world.objects[source_index].active);
    CHECK(!world.objects[source_index].pending_destroy);
    CHECK(world.objects[source_index].type == ObjectType::AircraftDebris);
    CHECK(world.objects[target_index].active);
    CHECK(world.objects[target_index].type == ObjectType::AircraftDebris);
    CHECK_EQ(live_count(world, ObjectType::Aircraft), 0);
    CHECK_EQ(live_count(world, ObjectType::AircraftDebris), 2);
    CHECK_EQ(live_count(world, ObjectType::EnemyDeath), 1);
    CHECK_EQ(world.scores.score, 10);
    CHECK_EQ(world.scores.enemy_kills, 1);

    game.tick(InputState {});
    CHECK_EQ(world.scores.score, 10);
    CHECK_EQ(world.scores.enemy_kills, 1);
}

TEST_CASE("type-7 source ignores non-overlap, edge-touch, and pending aircraft targets")
{
    struct Placement {
        Vec2 source;
        Vec2 target;
        bool pending;
    };

    // Record 0x121 is 19x17 in the recovered graphics records. The edge case
    // is strict AABB contact at x=100, not an assumed 6x6 fragment rectangle.
    const std::array placements {
        Placement {Vec2 {10.0f, 100.0f}, Vec2 {100.0f, 100.0f}, false},
        Placement {Vec2 {81.0f, 100.0f}, Vec2 {100.0f, 100.0f}, false},
        Placement {Vec2 {100.0f, 100.0f}, Vec2 {100.0f, 100.0f}, true},
    };

    for (const auto& placement : placements) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        world.objects.push_back(make_fragment(placement.source));
        const auto target_index = world.objects.size();
        auto target = make_aircraft(AircraftVariant::D, placement.target);
        target.pending_destroy = placement.pending;
        world.objects.push_back(target);
        world.objects.shrink_to_fit();

        game.tick(InputState {});

        CHECK(!world.objects[target_index].pending_destroy);
        if (placement.pending) {
            // update_active_objects clears pending actors before collision
            // dispatch; the old target slot is now an available free slot.
            CHECK(!world.objects[target_index].active);
        } else {
            CHECK(world.objects[target_index].active);
            CHECK(world.objects[target_index].type == ObjectType::Aircraft);
        }
        CHECK_EQ(world.scores.score, 0);
        CHECK_EQ(world.scores.enemy_kills, 0);
        CHECK_EQ(live_count(world, ObjectType::EnemyDeath), 0);
        CHECK_EQ(live_count(world, ObjectType::AircraftDebris), 1);
    }
}

TEST_CASE("type-6 death animation is not a collision source")
{
    const std::array target_types {
        ObjectType::Aircraft,
        ObjectType::SmartBomb,
        ObjectType::Paratrooper,
        ObjectType::GroundedTransition,
        ObjectType::LandedInvader,
    };

    for (const auto target_type : target_types) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        const auto position = Vec2 {100.0f, 100.0f};
        world.objects.push_back(make_type6_source(position));
        const auto target_index = world.objects.size();
        world.objects.push_back(make_type6_negative_target(target_type, position));

        game.tick(InputState {});

        // The type table gives type 6 flags 0x0000. The parachute collision
        // path also gates on the other actor's bit 0x0002, so type 6 cannot
        // damage either aircraft-family or grounded actors.
        CHECK(world.objects[0].active);
        CHECK(!world.objects[0].pending_destroy);
        CHECK(world.objects[0].type == ObjectType::EnemyDeath);
        CHECK(world.objects[target_index].active);
        CHECK(!world.objects[target_index].pending_destroy);
        CHECK(world.objects[target_index].type == target_type);
        CHECK_EQ(world.scores.score, 0);
        CHECK_EQ(world.scores.enemy_kills, 0);
        CHECK_EQ(live_count(world, ObjectType::AircraftDebris), 0);
    }
}

TEST_CASE("collision sources dispatch in slot order rather than projectile priority")
{
    for (const bool projectile_first : {false, true}) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        world.objects.push_back(make_aircraft(AircraftVariant::C, {120.0f, 100.0f}));

        Object projectile {};
        projectile.active = true;
        projectile.type = ObjectType::PlayerProjectile;
        projectile.position = {118.5f, 100.0f};
        projectile.extent = {1.5f, 1.5f};
        const auto debris = make_fragment({100.0f, 100.0f});
        world.objects.push_back(projectile_first ? projectile : debris);
        world.objects.push_back(projectile_first ? debris : projectile);
        world.objects.shrink_to_fit();

        // The shot overlaps both actors, but the 19-pixel fragment does not
        // reach the aircraft. An earlier fragment retires the shot via 0168
        // before the shot can dispatch its own pass against the aircraft.
        game.tick(InputState {});

        CHECK_EQ(live_count(world, ObjectType::PlayerProjectile), 0);
        CHECK_EQ(live_count(world, ObjectType::Aircraft), projectile_first ? 0 : 1);
        CHECK_EQ(world.scores.score, projectile_first ? 10 : 0);
        CHECK_EQ(world.scores.enemy_kills, projectile_first ? 1 : 0);
    }
}

TEST_CASE("new wreckage only dispatches when its slot has not been visited")
{
    for (const bool projectile_first : {false, true}) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        const auto first_aircraft = make_aircraft(AircraftVariant::C, {100.0f, 100.0f});
        Object projectile {};
        projectile.active = true;
        projectile.type = ObjectType::PlayerProjectile;
        projectile.position = {120.0f, 100.0f};
        projectile.extent = {1.5f, 1.5f};
        world.objects.push_back(projectile_first ? projectile : first_aircraft);
        world.objects.push_back(projectile_first ? first_aircraft : projectile);
        world.objects.push_back(make_aircraft(AircraftVariant::D, {140.0f, 100.0f}));
        world.objects.shrink_to_fit();

        // The shot misses the second aircraft, but the converted first shell
        // overlaps it. 31d7 does not revisit a shell in an earlier slot.
        game.tick(InputState {});

        CHECK_EQ(live_count(world, ObjectType::Aircraft), projectile_first ? 0 : 1);
        CHECK_EQ(world.scores.score, projectile_first ? 15 : 10);
        CHECK_EQ(world.scores.enemy_kills, projectile_first ? 2 : 1);
    }
}

TEST_CASE("projectile retirement makes its slot reusable in the same collision pass")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();

    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = {119.0f, 99.0f};
    projectile.extent = {1.5f, 1.5f};
    world.objects.push_back(projectile);
    world.objects.push_back(make_aircraft(AircraftVariant::C, {100.0f, 100.0f}));
    world.objects.push_back(make_aircraft(AircraftVariant::D, {140.0f, 100.0f}));
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    // 0168 retires slot 0 after C's callback; D's type-6 clone then reuses it.
    CHECK_EQ(world.objects.size(), std::size_t {4});
    if (world.objects.size() < 4) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(!world.objects[0].pending_destroy);
    CHECK(world.objects[0].type == ObjectType::EnemyDeath);
    CHECK(world.objects[0].aircraft_variant == AircraftVariant::D);
    CHECK(world.objects[1].type == ObjectType::AircraftDebris);
    CHECK(world.objects[2].type == ObjectType::AircraftDebris);
    CHECK(world.objects[3].type == ObjectType::EnemyDeath);
    CHECK(world.objects[3].aircraft_variant == AircraftVariant::C);
    CHECK_EQ(live_count(world, ObjectType::EnemyDeath), 2);
    CHECK_EQ(world.scores.score, 15);
    CHECK_EQ(world.scores.enemy_kills, 2);
}

TEST_CASE("falling trooper contact reads the retired projectile slot during particle allocation")
{
    Game game(std::uint16_t {0});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();
    world.gameplay_rng_seed = 4220896064u;
    world.objects.resize(38);
    world.object_highwater = world.objects.size();

    // Preserve the captured holes and cached retirement without other contacts.
    for (auto& object : world.objects) {
        object = make_type6_source({20.0f, 20.0f});
    }
    for (const auto slot : {21, 29, 30, 32, 33, 34, 35, 36}) {
        world.objects[slot].active = false;
    }
    world.objects[3] = make_culled_projectile();

    Object trooper {};
    trooper.active = true;
    trooper.type = ObjectType::GroundedTransition;
    trooper.parachute_lost = true;
    trooper.position = {287.640625, 63.25};
    trooper.velocity = {-0.015625f, 0.5f};
    trooper.extent = {2.5f, 3.0f};
    trooper.timer = 3;
    world.objects[8] = trooper;

    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = {285.25048828125, 68.687408447265625};
    projectile.velocity = {0.792724609375f, -0.6095733642578125f};
    projectile.extent = {1.0f, 1.0f};
    projectile.frame = 100;
    world.objects[9] = projectile;
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    struct ExpectedParticle {
        std::size_t slot;
        Vec2 velocity;
        int lifetime;
    };
    constexpr std::array<ExpectedParticle, 8> expected {{
        {3, {0.8521881103515625f, -1.0406341552734375f}, 32},
        {9, {0.0682525634765625f, 0.1433258056640625f}, 34},
        {21, {-0.0290374755859375f, 0.153289794921875f}, 32},
        {29, {-0.0689849853515625f, 0.33807373046875f}, 36},
        {30, {-0.3111724853515625f, -0.0397491455078125f}, 34},
        {32, {0.114166259765625f, 0.0238189697265625f}, 33},
        {33, {0.26531982421875f, 0.0077667236328125f}, 34},
        {34, {0.5150146484375f, 0.598907470703125f}, 33},
    }};
    CHECK(!world.objects[8].active);
    CHECK(!world.objects[35].active);
    CHECK_EQ(live_count(world, ObjectType::ResolutionParticle), 8);
    CHECK_EQ(world.scores.score, 2);
    CHECK_EQ(world.scores.grounded_invader_resolutions, 1u);
    CHECK_EQ(world.gameplay_rng_seed, 432743456u);
    for (const auto& item : expected) {
        const auto& particle = world.objects[item.slot];
        CHECK(particle.active);
        CHECK(particle.type == ObjectType::ResolutionParticle);
        CHECK_EQ(particle.position.x, 286.043212890625);
        CHECK_EQ(particle.position.y, 68.07783508300781);
        CHECK_EQ(particle.velocity.x, item.velocity.x);
        CHECK_EQ(particle.velocity.y, item.velocity.y);
        CHECK_EQ(particle.assault_stage, item.lifetime);
    }
}

TEST_CASE("projectile collision appends its first death clone before retirement")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();

    auto aircraft = make_aircraft(AircraftVariant::C, {100.0f, 100.0f});
    aircraft.extent = {22.0f, 6.5f};
    world.objects.push_back(aircraft);
    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = {119.0f, 99.0f};
    projectile.extent = {1.5f, 1.5f};
    world.objects.push_back(projectile);
    world.objects.push_back(make_aircraft(AircraftVariant::D, {140.0f, 100.0f}));
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    // The first clone is allocated while the projectile still occupies slot 1.
    CHECK_EQ(world.objects.size(), std::size_t {4});
    if (world.objects.size() < 4) {
        return;
    }
    CHECK(!world.objects[1].active);
    CHECK(!world.objects[1].pending_destroy);
    CHECK(world.objects[3].active);
    CHECK(!world.objects[3].pending_destroy);
    CHECK(world.objects[3].type == ObjectType::EnemyDeath);
    CHECK(world.objects[3].aircraft_variant == AircraftVariant::C);
    CHECK_EQ(world.objects[3].extent.x, 22.0f);
    CHECK_EQ(world.objects[3].extent.y, 6.5f);
    CHECK_EQ(world.scores.score, 10);
    CHECK_EQ(world.scores.enemy_kills, 1);
}

TEST_CASE("wide projectile record continues the original inner collision scan")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();

    auto first = make_aircraft(AircraftVariant::C, {95.0f, 100.0f});
    first.extent = {4.0f, 5.0f};
    auto second = make_aircraft(AircraftVariant::D, {108.0f, 100.0f});
    second.extent = {4.0f, 5.0f};
    auto later = make_aircraft(AircraftVariant::C, {113.0f, 100.0f});
    later.extent = {4.0f, 5.0f};
    world.objects.push_back(first);
    world.objects.push_back(second);

    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = {100.0f, 100.0f};
    projectile.extent = {5.0f, 5.0f};
    world.objects.push_back(projectile);
    world.objects.push_back(make_type6_source({250.0f, 150.0f}));
    world.objects.push_back(later);
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    CHECK_EQ(world.objects.size(), std::size_t {7});
    if (world.objects.size() < 7) {
        return;
    }
    CHECK(world.objects[2].active);
    CHECK(world.objects[2].type == ObjectType::EnemyDeath);
    CHECK(world.objects[2].aircraft_variant == AircraftVariant::D);
    CHECK(world.objects[4].type == ObjectType::AircraftDebris);
    CHECK(world.objects[5].type == ObjectType::EnemyDeath);
    CHECK(world.objects[6].type == ObjectType::EnemyDeath);
    CHECK_EQ(world.scores.score, 25);
    CHECK_EQ(world.scores.enemy_kills, 3);
}

TEST_CASE("debris collision retires an overlapping projectile immediately")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();
    world.objects.push_back(make_fragment({100.0f, 100.0f}));

    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = {100.0f, 100.0f};
    projectile.extent = {1.5f, 1.5f};
    world.objects.push_back(projectile);
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    CHECK(!world.objects[1].active);
    CHECK(!world.objects[1].pending_destroy);
    // 567f clears primary activity/callbacks, not the secondary type or bbox.
    CHECK(world.objects[1].type == ObjectType::PlayerProjectile);
    CHECK_EQ(world.objects[1].position.x, projectile.position.x);
    CHECK_EQ(world.objects[1].position.y, projectile.position.y);
    CHECK_EQ(world.objects[1].extent.x, projectile.extent.x);
    CHECK_EQ(world.objects[1].extent.y, projectile.extent.y);
    CHECK_EQ(live_count(world, ObjectType::AircraftDebris), 1);
}

TEST_CASE("projectile continues across both already-visited aircraft targets")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();

    world.objects.push_back(make_aircraft(AircraftVariant::C, {100.0f, 100.0f}));
    world.objects.push_back(make_aircraft(AircraftVariant::D, {100.0f, 100.0f}));

    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = {100.0f, 100.0f};
    projectile.extent = {1.5f, 1.5f};
    world.objects.push_back(projectile);
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    // The aircraft occupy slots 0 and 1; the later projectile in slot 2 must
    // keep scanning after its first 0168 retirement callback.
    CHECK_EQ(world.objects.size(), std::size_t {4});
    if (world.objects.size() < 4) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::AircraftDebris);
    CHECK(world.objects[1].active);
    CHECK(world.objects[1].type == ObjectType::AircraftDebris);
    CHECK(world.objects[2].active);
    CHECK(world.objects[2].type == ObjectType::EnemyDeath);
    CHECK(world.objects[2].aircraft_variant == AircraftVariant::D);
    CHECK(world.objects[3].active);
    CHECK(world.objects[3].type == ObjectType::EnemyDeath);
    CHECK(world.objects[3].aircraft_variant == AircraftVariant::C);
    CHECK_EQ(live_count(world, ObjectType::PlayerProjectile), 0);
    CHECK_EQ(world.scores.score, 15);
    CHECK_EQ(world.scores.enemy_kills, 2);
}

TEST_CASE("projectile reuse prefers its cached slot over an older hole")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();

    // Keep two old holes so the first target clone consumes slot 0. After the
    // projectile in slot 4 is retired, slot 1 remains an older hole and must
    // not win over the newly cached slot 4.
    world.objects.resize(2);
    world.objects.push_back(make_aircraft(AircraftVariant::C, {100.0f, 100.0f}));
    world.objects.push_back(make_aircraft(AircraftVariant::D, {100.0f, 100.0f}));

    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = {100.0f, 100.0f};
    projectile.extent = {1.5f, 1.5f};
    world.objects.push_back(projectile);
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    CHECK_EQ(world.objects.size(), std::size_t {5});
    if (world.objects.size() < 5) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::EnemyDeath);
    CHECK(world.objects[0].aircraft_variant == AircraftVariant::C);
    CHECK(!world.objects[1].active);
    CHECK(world.objects[2].active);
    CHECK(world.objects[2].type == ObjectType::AircraftDebris);
    CHECK(world.objects[3].active);
    CHECK(world.objects[3].type == ObjectType::AircraftDebris);
    CHECK(world.objects[4].active);
    CHECK(world.objects[4].type == ObjectType::EnemyDeath);
    CHECK(world.objects[4].aircraft_variant == AircraftVariant::D);
    CHECK_EQ(live_count(world, ObjectType::PlayerProjectile), 0);
    CHECK_EQ(world.scores.score, 15);
    CHECK_EQ(world.scores.enemy_kills, 2);
}

TEST_CASE("level warp clears the retired-slot cursor before allocation")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();

    // Seed a cached retired slot at index 4 through the public tick path.
    world.objects.resize(5);
    world.objects[0] = make_aircraft(AircraftVariant::C, {100.0f, 100.0f});
    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = {100.0f, 100.0f};
    projectile.extent = {1.5f, 1.5f};
    world.objects[4] = projectile;
    game.tick(InputState {});

    InputState level_warp {};
    level_warp.control_modifier = true;
    level_warp.alt_modifier = true;
    level_warp.level_warp_digit = 1;
    game.tick(level_warp);

    isolate_collision_world(game);
    world.objects.resize(5);
    world.objects[2] = make_fragment({100.0f, 100.0f});
    world.objects[3] = make_aircraft(AircraftVariant::C, {100.0f, 100.0f});

    game.tick(InputState {});

    // A reset must discard the old slot-4 cursor; the first new clone uses
    // the first inactive record instead of the stale cached slot.
    CHECK_EQ(world.objects.size(), std::size_t {5});
    if (world.objects.size() < 5) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::EnemyDeath);
    CHECK(world.objects[0].aircraft_variant == AircraftVariant::C);
    CHECK(!world.objects[4].active);
}

TEST_CASE("continued collision scan reads the reused source slot geometry")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();
    world.objects.push_back(make_aircraft(AircraftVariant::C, {100, 100}));
    world.objects.push_back(make_aircraft(AircraftVariant::D, {100, 100}));
    Object projectile {};
    projectile.active = true;
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = {101, 101};
    projectile.extent = {1.5f, 1.5f};
    world.objects.push_back(projectile);
    world.objects.push_back(make_type6_source({250, 150}));
    world.objects.push_back(make_aircraft(AircraftVariant::C, {150, 100}));
    world.objects.shrink_to_fit();

    // Neither the shot nor C's box reaches slot 4. D's clone replaces slot 2
    // during the inner scan, and its retained 69-pixel box does reach slot 4.
    game.tick(InputState {});

    CHECK_EQ(world.objects.size(), std::size_t {7});
    if (world.objects.size() < 7) {
        return;
    }
    CHECK(world.objects[2].active);
    CHECK(world.objects[2].type == ObjectType::EnemyDeath);
    CHECK(world.objects[2].aircraft_variant == AircraftVariant::D);
    CHECK(world.objects[4].type == ObjectType::AircraftDebris);
    CHECK(world.objects[5].type == ObjectType::EnemyDeath);
    CHECK(world.objects[6].type == ObjectType::EnemyDeath);
    CHECK_EQ(world.scores.score, 25);
    CHECK_EQ(world.scores.enemy_kills, 3);
}

TEST_CASE("update retirement reuses an expired particle slot before debris scatter")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();

    Object particle {};
    particle.active = true;
    particle.type = ObjectType::ResolutionParticle;
    particle.position = {10.0f, 20.0f};
    particle.assault_stage = 0;
    world.objects.push_back(particle);
    world.objects.push_back(make_scattering_shell({100.0f, 100.0f}));
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    // The first fragment must consume the particle's retired slot. It was
    // already visited, so its payload update must not run again this tick.
    CHECK_EQ(world.objects.size(), std::size_t {5});
    if (world.objects.size() < 5) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(!world.objects[0].pending_destroy);
    CHECK(world.objects[0].type == ObjectType::AircraftDebris);
    CHECK(world.objects[0].has_dropped_payload);
    CHECK_EQ(world.objects[0].timer, 0u);
    CHECK_EQ(world.objects[0].position.y, 101.0f);

    CHECK(!world.objects[1].active);
    CHECK(world.objects[2].active);
    CHECK(world.objects[2].type == ObjectType::AircraftDebris);
    CHECK_EQ(world.objects[2].timer, 1u);
}

TEST_CASE("update scan visits debris children appended after an expiring particle")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();

    world.objects.push_back(make_scattering_shell({100.0f, 100.0f}));
    Object particle {};
    particle.active = true;
    particle.type = ObjectType::ResolutionParticle;
    particle.position = {10.0f, 20.0f};
    particle.assault_stage = 0;
    world.objects.push_back(particle);
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    // The shell has no free slot before its callback, so its children append
    // after the particle. The scan must reach those newly allocated children.
    CHECK_EQ(world.objects.size(), std::size_t {6});
    if (world.objects.size() < 6) {
        return;
    }
    CHECK(!world.objects[0].active);
    CHECK(!world.objects[1].active);
    CHECK(world.objects[2].active);
    CHECK(world.objects[2].type == ObjectType::AircraftDebris);
    CHECK(world.objects[2].has_dropped_payload);
    CHECK_EQ(world.objects[2].timer, 1u);
}

TEST_CASE("update retirement reuses earlier expiry and cull slots before debris scatter")
{
    const std::array earlier_retirements {
        make_expiring_enemy_death(),
        make_culled_fragment(),
        make_culled_projectile(),
    };

    for (const auto& earlier : earlier_retirements) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        world.objects.push_back(earlier);
        world.objects.push_back(make_scattering_shell({100.0f, 100.0f}));
        world.objects.shrink_to_fit();

        game.tick(InputState {});

        CHECK_EQ(world.objects.size(), std::size_t {5});
        if (world.objects.size() < 5) {
            continue;
        }
        CHECK(world.objects[0].active);
        CHECK(!world.objects[0].pending_destroy);
        CHECK(world.objects[0].type == ObjectType::AircraftDebris);
        CHECK(world.objects[0].has_dropped_payload);
        CHECK_EQ(world.objects[0].timer, 0u);
        CHECK_EQ(world.objects[0].position.y, 101.0f);
        CHECK(!world.objects[1].active);
        CHECK(world.objects[2].active);
        CHECK_EQ(world.objects[2].timer, 1u);
    }
}

TEST_CASE("a later shell reuses the first shell slot after its children are allocated")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();
    world.objects.push_back(make_scattering_shell({100.0f, 100.0f}, AircraftVariant::C));
    world.objects.push_back(make_scattering_shell({200.0f, 100.0f}, AircraftVariant::D));
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    // The first shell appends all four children before its slot is retired.
    // The later shell then consumes that slot for its first child.
    CHECK_EQ(world.objects.size(), std::size_t {9});
    if (world.objects.size() < 9) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::AircraftDebris);
    CHECK(world.objects[0].aircraft_variant == AircraftVariant::D);
    CHECK_EQ(world.objects[0].timer, 0u);
    CHECK(!world.objects[1].active);
    CHECK(world.objects[2].active);
    CHECK(world.objects[2].aircraft_variant == AircraftVariant::C);
    CHECK_EQ(world.objects[2].timer, 1u);
    CHECK(world.objects[6].active);
    CHECK(world.objects[6].aircraft_variant == AircraftVariant::D);
    CHECK_EQ(world.objects[6].timer, 1u);
}

TEST_CASE("fragment culling uses the original fixed-point edge predicates")
{
    struct Edge { Vec2 position; bool active; };
    for (const auto edge : std::array {
             Edge {{100.0f, 199.5f}, true}, Edge {{100.0f, 200.0f}, false},
             Edge {{319.5f, 80.0f}, true}, Edge {{320.0f, 80.0f}, false},
             Edge {{-99.5f, 80.0f}, true}, Edge {{-100.0f, 80.0f}, false},
             Edge {{100.0f, -39.5f}, true}, Edge {{100.0f, -40.0f}, false}}) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        world.objects.push_back(make_fragment(edge.position));
        game.tick(InputState {});
        CHECK_EQ(world.objects[0].active, edge.active);
    }
}

TEST_CASE("aircraft shell cuts the chute before resolving an airborne trooper")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();
    constexpr std::uint32_t kCollisionSeed = 3347423727u;
    world.gameplay_rng_seed = kCollisionSeed;
    auto shell = make_scattering_shell({140.0f, 1.0f}, AircraftVariant::D);
    shell.velocity = {};
    shell.assault_stage = 10;
    Object trooper {};
    trooper.active = true;
    trooper.type = ObjectType::Paratrooper;
    trooper.position = {151.625f, 11.75f};
    trooper.velocity = {0.125f, 0.25f};
    trooper.extent = {5.0f, 7.5f};
    trooper.frame = 2;
    trooper.timer = 19;
    world.objects = {shell, trooper};
    world.object_highwater = world.objects.size();
    game.tick(InputState {});
    CHECK(world.objects[1].active);
    CHECK(world.objects[1].type == ObjectType::GroundedTransition);
    CHECK(world.objects[1].parachute_lost);
    CHECK_NEAR(world.objects[1].position.x, 154.75f, 0.0f);
    CHECK_NEAR(world.objects[1].position.y, 23.0f, 0.0f);
    CHECK_EQ(world.objects[1].frame, 0);
    CHECK_EQ(world.scores.score, 0);
    CHECK_EQ(world.scores.grounded_invader_resolutions, 0u);
    CHECK_EQ(world.gameplay_rng_seed, kCollisionSeed);
    CHECK_EQ(live_count(world, ObjectType::ResolutionParticle), 0);
}

TEST_CASE("projectile remains active at the captured offscreen update boundary")
{
    Game game(std::uint16_t {12});
    isolate_collision_world(game);
    auto& world = game.diagnostic_world();
    Object shot {};
    shot.active = true;
    shot.type = ObjectType::PlayerProjectile;
    shot.position = {111.8932037353515625, -7.41204833984375};
    shot.velocity = {-0.2687530517578125f, -0.96319580078125f};
    shot.extent = {1.0f, 1.0f};
    shot.frame = 52;
    shot.timer = 159;
    world.objects = {shot};
    world.object_highwater = world.objects.size();
    game.tick(InputState {});
    CHECK(world.objects[0].active);
    CHECK_EQ(world.object_highwater.value_or(world.objects.size()), std::size_t {1});
    CHECK_NEAR(world.objects[0].position.x, 111.62445068359375, 0.0);
    CHECK_NEAR(world.objects[0].position.y, -8.375244140625, 0.0);
}

TEST_CASE("projectile culling shares the original fixed-point motion bounds")
{
    constexpr double kFixedUnit = 1.0 / 65536.0;
    struct Edge { niteraid::WorldPosition position; bool active; };
    for (const auto edge : std::array {
             Edge {{100, -40 + kFixedUnit}, true}, Edge {{100, -40}, false},
             Edge {{-100 + kFixedUnit, 80}, true}, Edge {{-100, 80}, false},
             Edge {{320 - kFixedUnit, 80}, true}, Edge {{320, 80}, false},
             Edge {{100, 200 - kFixedUnit}, true}, Edge {{100, 200}, false}}) {
        Game game(std::uint16_t {12});
        isolate_collision_world(game);
        auto& world = game.diagnostic_world();
        Object shot {};
        shot.active = true;
        shot.type = ObjectType::PlayerProjectile;
        shot.position = edge.position;
        shot.extent = {1.0f, 1.0f};
        world.objects = {shot};
        world.object_highwater = world.objects.size();
        game.tick(InputState {});
        CHECK_EQ(world.objects[0].active, edge.active);
    }
}

TEST_CASE("fragment origins translate signed aircraft direction to DOS flag")
{
    for (const auto variant : {AircraftVariant::A, AircraftVariant::B,
                               AircraftVariant::C, AircraftVariant::D}) {
        for (const int direction : {-1, 1}) {
            Game game(std::uint16_t {12});
            isolate_collision_world(game);
            auto& world = game.diagnostic_world();
            world.gameplay_rng_seed = 1;
            auto shell = make_scattering_shell({100.0f, 40.0f}, variant);
            shell.direction = direction;
            shell.velocity = {};
            world.objects.push_back(shell);
            game.tick(InputState {});
            CHECK_EQ(world.objects.size(), std::size_t {5});
            if (world.objects.size() != 5) {
                continue;
            }
            const auto& child = world.objects[1];
            const bool large = variant == AircraftVariant::D || variant == AircraftVariant::B;
            const float offset = direction > 0 ? (large ? 44.0f : 10.0f) : (large ? 28.0f : 1.0f);
            // Seed 1's first 546a X delta; five factory steps, then one update.
            constexpr float first_impulse = -30692.0f / 65536.0f;
            CHECK_NEAR(child.position.x, 100.0f + offset + 5.0f * first_impulse + child.velocity.x, 0.0f);
        }
    }
}
