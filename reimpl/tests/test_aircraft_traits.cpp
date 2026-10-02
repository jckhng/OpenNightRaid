// Per-variant aircraft traits recovered from the decompile:
//   lane height, width, speed, sprite-shell delay.
// See research/gameplay-systems.md "Aircraft Enemies" section.

#include "niteraid/game_internals.hpp"
#include "test_harness.hpp"

#include <utility>

using niteraid::AircraftVariant;
using niteraid::internals::aircraft_debris_shell_delay;
using niteraid::internals::aircraft_speed_for_variant;
using niteraid::internals::aircraft_width_for_variant;
using niteraid::internals::lane_y_for_variant;
using niteraid::internals::remake_aircraft_fragment_velocity;

TEST_CASE("gameplay painter layers put troopers above aircraft regardless of reused slot order")
{
    using niteraid::ObjectType;
    niteraid::Object object {};
    for (const auto [type, layer] : {
             std::pair{ObjectType::PlayerCannon, 0}, {ObjectType::FinaleController, 0},
             {ObjectType::Aircraft, 1}, {ObjectType::SmartBomb, 1},
             {ObjectType::Paratrooper, 2}, {ObjectType::GroundedTransition, 2},
             {ObjectType::LandedInvader, 2}, {ObjectType::ResolutionParticle, 2},
             {ObjectType::PlayerProjectile, 3}, {ObjectType::EnemyDeath, 4}}) {
        object.type = type;
        CHECK_EQ(niteraid::internals::original_gameplay_draw_layer(object), layer);
    }
    object.type = ObjectType::AircraftDebris;
    CHECK_EQ(niteraid::internals::original_gameplay_draw_layer(object), 1);
    object.has_dropped_payload = true;
    CHECK_EQ(niteraid::internals::original_gameplay_draw_layer(object), 2);
}

TEST_CASE("aircraft overlays use original phase ownership rather than smart-bomb height")
{
    niteraid::Object aircraft {};
    aircraft.aircraft_variant = AircraftVariant::A;
    for (int phase = 0; phase < 4; ++phase) {
        aircraft.frame = phase;
        CHECK_EQ(niteraid::internals::aircraft_rotor_frame(aircraft, 324), (phase + 3) % 4);
    }
    aircraft.aircraft_variant = AircraftVariant::D;
    aircraft.frame = 4;
    CHECK_EQ(niteraid::internals::aircraft_rotor_frame(aircraft, 324), 4);
    for (auto tick : {0u, 29u, 65u, 324u, 408u, 450u, 0xffffffffu}) {
        aircraft.aircraft_variant = AircraftVariant::B;
        CHECK_EQ(niteraid::internals::aircraft_rotor_frame(aircraft, tick), tick % 65);
        aircraft.aircraft_variant = AircraftVariant::C;
        CHECK_EQ(niteraid::internals::aircraft_rotor_frame(aircraft, tick), tick % 29);
    }
}

TEST_CASE("lane_y_for_variant matches recovered per-variant lane heights")
{
    CHECK_NEAR(lane_y_for_variant(AircraftVariant::D), 1.0f, 1e-6f);
    CHECK_NEAR(lane_y_for_variant(AircraftVariant::A), 24.0f, 1e-6f);
    CHECK_NEAR(lane_y_for_variant(AircraftVariant::B), 4.0f, 1e-6f);
    CHECK_NEAR(lane_y_for_variant(AircraftVariant::C), 51.0f, 1e-6f);
}

TEST_CASE("aircraft_width_for_variant matches recovered widths")
{
    CHECK_NEAR(aircraft_width_for_variant(AircraftVariant::D), 69.0f, 1e-6f);
    CHECK_NEAR(aircraft_width_for_variant(AircraftVariant::A), 38.0f, 1e-6f);
    CHECK_NEAR(aircraft_width_for_variant(AircraftVariant::B), 63.0f, 1e-6f);
    CHECK_NEAR(aircraft_width_for_variant(AircraftVariant::C), 44.0f, 1e-6f);
}

TEST_CASE("aircraft_speed_for_variant: big = 1, little A = 2, little C = 3")
{
    CHECK_NEAR(aircraft_speed_for_variant(AircraftVariant::D), 1.0f, 1e-6f);
    CHECK_NEAR(aircraft_speed_for_variant(AircraftVariant::B), 1.0f, 1e-6f);
    CHECK_NEAR(aircraft_speed_for_variant(AircraftVariant::A), 2.0f, 1e-6f);
    CHECK_NEAR(aircraft_speed_for_variant(AircraftVariant::C), 3.0f, 1e-6f);
}

TEST_CASE("aircraft_debris_shell_delay: big = 10, little = 6")
{
    // Per 16c8:08ea — large-family shells break after timer > 9, small-family after > 5.
    CHECK_EQ(aircraft_debris_shell_delay(AircraftVariant::D), 10);
    CHECK_EQ(aircraft_debris_shell_delay(AircraftVariant::B), 10);
    CHECK_EQ(aircraft_debris_shell_delay(AircraftVariant::A), 6);
    CHECK_EQ(aircraft_debris_shell_delay(AircraftVariant::C), 6);
}

TEST_CASE("remake first-family debris scatters to both sides regardless of aircraft direction")
{
    for (const float aircraft_x : {-1.0f, 1.0f}) {
        const auto left = remake_aircraft_fragment_velocity({aircraft_x, 0.0f}, 0.5f, 0.25f, 0);
        const auto right = remake_aircraft_fragment_velocity({aircraft_x, 0.0f}, 0.5f, 0.25f, 1);
        CHECK(left.x < 0.0f);
        CHECK(right.x > 0.0f);
        CHECK_NEAR(left.y, 0.25f, 1e-6f);
        CHECK_NEAR(right.y, 0.25f, 1e-6f);
    }
}
