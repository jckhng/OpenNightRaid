// Per nite-wave-scheduling.md "Level-Clear Arming Logic": the type-flag
// table at 2730:0792 uses bit 0x0004 as the level-clear flag. Types 0,
// 1, 2, 3, 4, and 0x0b (LandedInvader) carry that bit; aircraft /
// paratrooper / smart-bomb / debris / final-controller families do not.

#include "niteraid/game_internals.hpp"
#include "niteraid/game_types.hpp"
#include "test_harness.hpp"

using niteraid::ObjectType;
using niteraid::internals::object_type_allows_level_clear;

TEST_CASE("None, PlayerCannon, WaveController, PlayerProjectile, LandedInvader are clear-safe")
{
    CHECK(object_type_allows_level_clear(ObjectType::None));
    CHECK(object_type_allows_level_clear(ObjectType::PlayerCannon));
    CHECK(object_type_allows_level_clear(ObjectType::WaveController));
    CHECK(object_type_allows_level_clear(ObjectType::PlayerProjectile));
    CHECK(object_type_allows_level_clear(ObjectType::LandedInvader));
}

TEST_CASE("Airborne hostiles block level clear")
{
    CHECK(!object_type_allows_level_clear(ObjectType::Aircraft));
    CHECK(!object_type_allows_level_clear(ObjectType::Paratrooper));
    CHECK(!object_type_allows_level_clear(ObjectType::SmartBomb));
    CHECK(!object_type_allows_level_clear(ObjectType::GroundedTransition));
    CHECK(!object_type_allows_level_clear(ObjectType::FinaleController));
}

TEST_CASE("Active death/debris/effect objects block level clear")
{
    CHECK(!object_type_allows_level_clear(ObjectType::EnemyDeath));
    CHECK(!object_type_allows_level_clear(ObjectType::AircraftDebris));
    CHECK(!object_type_allows_level_clear(ObjectType::ResolutionParticle));
}
