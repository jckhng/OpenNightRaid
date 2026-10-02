#include "niteraid/game_internals.hpp"
#include "test_harness.hpp"

using niteraid::AircraftVariant;
using niteraid::Object;
using niteraid::ObjectType;
using niteraid::Vec2;
using niteraid::internals::collision_bounds_for_object;
using niteraid::internals::projectile_hits_deployed_paratrooper_body;
using niteraid::internals::remake_aircraft_fragment_collision_bounds;
using niteraid::internals::remake_aircraft_fragment_hits_object;

namespace {

void check_bounds(const Object& object, float left, float top, float right, float bottom)
{
    const auto bounds = collision_bounds_for_object(object);
    CHECK_NEAR(bounds.left, left, 1e-6f);
    CHECK_NEAR(bounds.top, top, 1e-6f);
    CHECK_NEAR(bounds.right, right, 1e-6f);
    CHECK_NEAR(bounds.bottom, bottom, 1e-6f);
}

}  // namespace

TEST_CASE("fragment bounds preserve fixed units across the 256 precision boundary")
{
    constexpr double coordinate = 253.6293792724609375;
    Object fragment {};
    fragment.type = ObjectType::AircraftDebris;
    fragment.has_dropped_payload = true;
    fragment.sprite_id = 0x143;
    fragment.position = {static_cast<decltype(fragment.position.x)>(coordinate),
                         static_cast<decltype(fragment.position.y)>(coordinate)};

    for (const auto bounds : {collision_bounds_for_object(fragment),
                              remake_aircraft_fragment_collision_bounds(fragment)}) {
        CHECK_EQ(bounds.left, coordinate);
        CHECK_EQ(bounds.top, coordinate);
        CHECK_EQ(bounds.right, coordinate + 12.0);
        CHECK_EQ(bounds.bottom, coordinate + 12.0);
        CHECK_EQ(bounds.right - bounds.left, 12.0);
        CHECK_EQ(bounds.bottom - bounds.top, 12.0);
    }
    const auto center = niteraid::internals::collision_center_for_object(fragment);
    CHECK_EQ(center.x, coordinate + 6.0);
    CHECK_EQ(center.y, coordinate + 6.0);
}

TEST_CASE("fragment collision distinguishes touching from one fixed unit overlap")
{
    constexpr double fixed_unit = 1.0 / 65536.0;
    Object fragment {};
    fragment.type = ObjectType::AircraftDebris;
    fragment.has_dropped_payload = true;
    fragment.sprite_id = 0x143;
    fragment.position = {256, 256};

    Object landed {};
    landed.type = ObjectType::LandedInvader;
    landed.position = {268, 256};
    CHECK_EQ(remake_aircraft_fragment_hits_object(fragment, landed), false);
    landed.position.x = static_cast<decltype(landed.position.x)>(268.0 - fixed_unit);
    CHECK_EQ(remake_aircraft_fragment_hits_object(fragment, landed), true);
    landed.position.x = static_cast<decltype(landed.position.x)>(268.0 + fixed_unit);
    CHECK_EQ(remake_aircraft_fragment_hits_object(fragment, landed), false);

    landed.position = {256, 268};
    CHECK_EQ(remake_aircraft_fragment_hits_object(fragment, landed), false);
    landed.position.y = static_cast<decltype(landed.position.y)>(268.0 - fixed_unit);
    CHECK_EQ(remake_aircraft_fragment_hits_object(fragment, landed), true);
    landed.position.y = static_cast<decltype(landed.position.y)>(268.0 + fixed_unit);
    CHECK_EQ(remake_aircraft_fragment_hits_object(fragment, landed), false);
}

TEST_CASE("aircraft collision bounds use raw seed dimensions without an extra row")
{
    Object aircraft {};
    aircraft.type = ObjectType::Aircraft;
    aircraft.aircraft_variant = AircraftVariant::D;
    aircraft.position = Vec2 {100.0f, 1.0f};
    aircraft.extent = Vec2 {34.5f, 7.0f};

    check_bounds(aircraft, 100.0f, 1.0f, 169.0f, 18.0f);

    aircraft.aircraft_variant = AircraftVariant::A;
    aircraft.position = Vec2 {50.0f, 24.0f};
    check_bounds(aircraft, 50.0f, 24.0f, 88.0f, 38.0f);

    aircraft.aircraft_variant = AircraftVariant::B;
    check_bounds(aircraft, 50.0f, 24.0f, 113.0f, 37.0f);

    aircraft.aircraft_variant = AircraftVariant::C;
    check_bounds(aircraft, 50.0f, 24.0f, 94.0f, 37.0f);
}

TEST_CASE("paratrooper collision bounds use the initialized record rectangle")
{
    Object paratrooper {};
    paratrooper.type = ObjectType::Paratrooper;
    paratrooper.position = Vec2 {42.0f, 70.0f};
    paratrooper.extent = Vec2 {5.0f, 7.5f};

    check_bounds(paratrooper, 42.0f, 70.0f, 52.0f, 85.0f);
}

TEST_CASE("projectile collision bounds exclude the draw callback offset")
{
    Object projectile {};
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = Vec2 {160.0f, 145.0f};
    projectile.extent = Vec2 {1.0f, 1.0f};

    check_bounds(projectile, 160.0f, 145.0f, 162.0f, 147.0f);
}

TEST_CASE("deployed paratrooper projectile hit split follows chute handler threshold")
{
    Object paratrooper {};
    paratrooper.type = ObjectType::Paratrooper;
    paratrooper.position = Vec2 {42.0f, 70.0f};

    Object projectile {};
    projectile.type = ObjectType::PlayerProjectile;
    projectile.position = Vec2 {45.0f, 80.5f};
    CHECK(!projectile_hits_deployed_paratrooper_body(projectile, paratrooper));

    projectile.position.y = 81.0f;
    CHECK(projectile_hits_deployed_paratrooper_body(projectile, paratrooper));
}

TEST_CASE("deployed paratrooper chute threshold is independent of animation frame")
{
    Object paratrooper {};
    paratrooper.type = ObjectType::Paratrooper;
    paratrooper.position = Vec2 {42.0f, 70.0f};

    Object projectile {};
    projectile.type = ObjectType::PlayerProjectile;

    for (const int frame : {0, 3, 7}) {
        paratrooper.frame = frame;
        projectile.position.y = 80.999f;
        CHECK(!projectile_hits_deployed_paratrooper_body(projectile, paratrooper));

        projectile.position.y = 81.0f;
        CHECK(projectile_hits_deployed_paratrooper_body(projectile, paratrooper));
    }
}

TEST_CASE("no-chute draw origins do not change initialized collision bounds")
{
    CHECK_EQ(niteraid::internals::kNoChuteFrameOriginX[0], 1);
    CHECK_EQ(niteraid::internals::kNoChuteFrameOriginY[0], 0);
    for (std::size_t frame = 1; frame < 4; ++frame) {
        CHECK_EQ(niteraid::internals::kNoChuteFrameOriginX[frame], 0);
        CHECK_EQ(niteraid::internals::kNoChuteFrameOriginY[frame], 1);
    }

    Object falling {};
    falling.type = ObjectType::GroundedTransition;
    falling.parachute_lost = true;
    falling.position = Vec2 {80.0f, 90.0f};
    falling.frame = 2;
    falling.extent = Vec2 {2.5f, 3.0f};

    // 0401 initializes 5x6 in the original collision record; later frames
    // retain those dimensions rather than rebinding their draw sprites.
    check_bounds(falling, 80.0f, 90.0f, 85.0f, 96.0f);
    falling.extent.y = 3.5f;
    check_bounds(falling, 80.0f, 90.0f, 85.0f, 97.0f);
}

TEST_CASE("bunker assault actors use one continuous doorway layering rule")
{
    Object entrant {};
    entrant.type = ObjectType::LandedInvader;
    entrant.assaulting = true;

    entrant.assault_stage = 0;
    CHECK(niteraid::internals::bunker_assault_actor_draws_on_world_layer(entrant));
    CHECK(!niteraid::internals::bunker_assault_actor_draws_in_doorway(entrant));

    entrant.assault_stage = 1;
    CHECK(!niteraid::internals::bunker_assault_actor_draws_on_world_layer(entrant));
    CHECK(niteraid::internals::bunker_assault_actor_draws_in_doorway(entrant));

    entrant.assault_stage = 2;
    CHECK(!niteraid::internals::bunker_assault_actor_draws_on_world_layer(entrant));
    // Arrival is presented before the owner removes the actor next update.
    CHECK(niteraid::internals::bunker_assault_actor_draws_in_doorway(entrant));

    entrant.assault_stage = 3;
    CHECK(niteraid::internals::bunker_assault_actor_draws_on_world_layer(entrant));
    CHECK(!niteraid::internals::bunker_assault_actor_draws_in_doorway(entrant));
}

TEST_CASE("grounded and landed invader collision bounds follow original object rectangles")
{
    Object grounded {};
    grounded.type = ObjectType::GroundedTransition;
    grounded.position = Vec2 {105.0f, 164.0f};
    grounded.frame = 5;
    grounded.extent = Vec2 {3.0f, 7.5f};

    // 0559 retains the 0x0f7 resource bounds bound on landing.
    check_bounds(grounded, 105.0f, 164.0f, 111.0f, 179.0f);
    grounded.frame = 7;
    check_bounds(grounded, 105.0f, 164.0f, 111.0f, 179.0f);

    Object landed {};
    landed.type = ObjectType::LandedInvader;
    landed.position = Vec2 {120.0f, 172.0f};
    landed.extent = Vec2 {2.0f, 3.5f};

    check_bounds(landed, 120.0f, 172.0f, 124.0f, 179.0f);
}

TEST_CASE("remake aircraft fragments collide across their recovered sprite footprint")
{
    Object fragment {};
    fragment.type = ObjectType::AircraftDebris;
    fragment.has_dropped_payload = true;
    fragment.sprite_id = 0x121;
    fragment.position = Vec2 {-8.0f, 165.0f};
    fragment.extent = Vec2 {3.0f, 3.0f};

    const auto fragment_bounds = remake_aircraft_fragment_collision_bounds(fragment);
    CHECK_NEAR(fragment_bounds.left, -8.0f, 1e-6f);
    CHECK_NEAR(fragment_bounds.right, 11.0f, 1e-6f);
    CHECK_NEAR(fragment_bounds.bottom, 182.0f, 1e-6f);

    Object landed {};
    landed.type = ObjectType::LandedInvader;
    landed.position = Vec2 {5.0f, 172.0f};
    landed.extent = Vec2 {2.0f, 3.5f};

    CHECK(remake_aircraft_fragment_hits_object(fragment, landed));
}

TEST_CASE("remake aircraft fragments require strict overlap with landed invaders")
{
    Object fragment {};
    fragment.type = ObjectType::AircraftDebris;
    fragment.sprite_id = 0x121;
    fragment.frame = 0;

    Object landed {};
    landed.type = ObjectType::LandedInvader;
    landed.position = Vec2 {5.0f, 172.0f};

    // Sprite 0x121 is 19x17, so these placements only touch one edge.
    fragment.position = Vec2 {-14.0f, 165.0f};
    CHECK(!remake_aircraft_fragment_hits_object(fragment, landed));
    fragment.position.x = -13.0f;
    CHECK(remake_aircraft_fragment_hits_object(fragment, landed));

    fragment.position = Vec2 {0.0f, 155.0f};
    CHECK(!remake_aircraft_fragment_hits_object(fragment, landed));
    fragment.position.y = 156.0f;
    CHECK(remake_aircraft_fragment_hits_object(fragment, landed));
}

TEST_CASE("smart bomb retains initialized collision bounds through all rotation frames")
{
    Object bomb {};
    bomb.type = ObjectType::SmartBomb;
    bomb.position = Vec2 {120.0f, 49.0f};
    bomb.extent = Vec2 {8.5f, 2.0f};

    check_bounds(bomb, 120.0f, 49.0f, 137.0f, 53.0f);

    bomb.extent = Vec2 {10.0f, 7.0f};
    for (int direction : {0, 1}) {
        bomb.direction = direction;
        for (int frame = 0; frame < 5; ++frame) {
            bomb.frame = frame;
            check_bounds(bomb, 120.0f, 49.0f, 140.0f, 63.0f);
        }
    }
}

TEST_CASE("original wreckage retains shell and initial fragment collision rectangles")
{
    Object debris {};
    debris.type = ObjectType::AircraftDebris;
    debris.position = {100, 50};
    for (const auto variant : {AircraftVariant::A, AircraftVariant::B,
                               AircraftVariant::C, AircraftVariant::D}) {
        debris.aircraft_variant = variant;
        check_bounds(debris, 100, 50,
                     100 + niteraid::internals::aircraft_width_for_variant(variant),
                     50 + niteraid::internals::aircraft_height_for_variant(variant));
    }
    debris.has_dropped_payload = true;
    struct Fragment { int sprite; float width; float height; };
    for (const auto spec : {Fragment {0x121, 19, 17}, {0x129, 17, 17},
                            {0x131, 18, 16}, {0x143, 12, 12},
                            {0x14b, 14, 11}, {0x153, 12, 12}}) {
        debris.sprite_id = spec.sprite;
        for (int frame = 0; frame < 8; ++frame) {
            debris.frame = frame;
            check_bounds(debris, 100, 50, 100 + spec.width, 50 + spec.height);
        }
    }
}

TEST_CASE("type-6 death records retain aircraft and smart-bomb collision rectangles")
{
    Object death {};
    death.type = ObjectType::EnemyDeath;
    death.position = {100, 50};
    for (const auto variant : {AircraftVariant::A, AircraftVariant::B,
                               AircraftVariant::C, AircraftVariant::D}) {
        death.aircraft_variant = variant;
        check_bounds(death, 100, 50,
                     100 + niteraid::internals::aircraft_width_for_variant(variant),
                     50 + niteraid::internals::aircraft_height_for_variant(variant));
    }
    death.has_dropped_payload = true;
    death.finale_drop = true;
    check_bounds(death, 100, 50, 117, 54);
    death.finale_drop = false;
    check_bounds(death, 100, 50, 120, 64);
}
