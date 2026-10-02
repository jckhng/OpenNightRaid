// Asserts that the recovered runtime velocity tables (see
// research/nite-runtime-velocity-tables.md and tmp-ghidra/fidelity_projectile_tables.txt)
// match what game_internals.hpp ships.

#include "niteraid/game_internals.hpp"
#include "test_harness.hpp"

using niteraid::internals::kProjectileVelocityTable;
using niteraid::internals::projectile_velocity_for_frame;
using niteraid::internals::smart_bomb_stage_velocity;
using niteraid::internals::smart_bomb_stage_duration;

TEST_CASE("projectile components match the twelve off-grid captured raw words")
{
    enum class Axis { X, Y };
    struct Component {
        int frame;
        Axis axis;
        int high;
        unsigned low;
    };
    constexpr Component components[] {
        {1, Axis::Y, -1, 63105}, {2, Axis::Y, -1, 61486},
        {4, Axis::Y, -1, 58256}, {60, Axis::X, -1, 60677},
        {61, Axis::X, -1, 62296}, {62, Axis::X, -1, 63916},
        {64, Axis::X, 0, 1620}, {65, Axis::X, 0, 3240},
        {66, Axis::X, 0, 4859}, {122, Axis::Y, -1, 58256},
        {124, Axis::Y, -1, 61486}, {125, Axis::Y, -1, 63105},
    };
    constexpr double fixed_scale = 65536.0;
    for (const auto component : components) {
        const double raw = component.high * fixed_scale + component.low;
        const auto velocity = projectile_velocity_for_frame(component.frame);
        const auto actual = component.axis == Axis::X ? velocity.x : velocity.y;
        CHECK_EQ(actual, raw / fixed_scale);
        CHECK_EQ(actual * fixed_scale, raw);
    }
}

TEST_CASE("every projectile table component lies exactly on the 16.16 grid")
{
    constexpr double fixed_scale = 65536.0;
    for (const auto velocity : kProjectileVelocityTable) {
        for (const double component : {velocity.x, velocity.y}) {
            const double raw = component * fixed_scale;
            CHECK_EQ(raw, std::round(raw));
        }
    }
}

TEST_CASE("projectile_velocity_for_frame endpoints match runtime DS:4df8 dump")
{
    // Per the runtime dump: frame 0 = (-1, 0), frame 63 ~= (0, -1),
    // frame 126 = (1, 0). The table is a signed 16.16 quarter-arc.
    const auto v0 = projectile_velocity_for_frame(0);
    CHECK_NEAR(v0.x, -1.0f, 1e-5f);
    CHECK_NEAR(v0.y, 0.0f, 1e-5f);

    const auto v63 = projectile_velocity_for_frame(63);
    CHECK_NEAR(v63.x, 0.0f, 1e-3f);
    CHECK_NEAR(v63.y, -1.0f, 1e-3f);

    const auto v126 = projectile_velocity_for_frame(126);
    CHECK_NEAR(v126.x, 1.0f, 1e-5f);
    CHECK_NEAR(v126.y, 0.0f, 1e-5f);
}

TEST_CASE("projectile_velocity table is 127 entries and roughly unit-length")
{
    CHECK_EQ(kProjectileVelocityTable.size(), 127u);
    for (std::size_t i = 0; i < kProjectileVelocityTable.size(); ++i) {
        const auto v = kProjectileVelocityTable[i];
        const float mag = std::sqrt(v.x * v.x + v.y * v.y);
        CHECK_NEAR(mag, 1.0f, 1e-3f);
    }
}

TEST_CASE("projectile_velocity_for_frame clamps frame index")
{
    const auto v_neg = projectile_velocity_for_frame(-50);
    CHECK_NEAR(v_neg.x, -1.0f, 1e-5f);
    CHECK_NEAR(v_neg.y, 0.0f, 1e-5f);

    const auto v_over = projectile_velocity_for_frame(9999);
    CHECK_NEAR(v_over.x, 1.0f, 1e-5f);
    CHECK_NEAR(v_over.y, 0.0f, 1e-5f);
}

TEST_CASE("smart_bomb_stage_velocity derives x from 1.0 - base_v per stage")
{
    // research/nite-runtime-velocity-tables.md base vy:
    //   stage 1 = 0.30900574, stage 2 = 0.58778381, stage 3 = 0.80900574, stage 4 = 1.0
    // x = (1.0 - base_v) * direction
    const auto s1 = smart_bomb_stage_velocity(1, 0);
    CHECK_NEAR(s1.x, 1.0f - 0.30900574f, 1e-5f);
    CHECK_NEAR(s1.y, 0.30900574f, 1e-5f);

    const auto s2 = smart_bomb_stage_velocity(2, 0);
    CHECK_NEAR(s2.x, 1.0f - 0.58778381f, 1e-5f);
    CHECK_NEAR(s2.y, 0.58778381f, 1e-5f);

    const auto s3 = smart_bomb_stage_velocity(3, 0);
    CHECK_NEAR(s3.x, 1.0f - 0.80900574f, 1e-5f);
    CHECK_NEAR(s3.y, 0.80900574f, 1e-5f);

    const auto s4 = smart_bomb_stage_velocity(4, 0);
    CHECK_NEAR(s4.x, 0.0f, 1e-6f);
    CHECK_NEAR(s4.y, 1.0f, 1e-6f);
}

TEST_CASE("smart_bomb_stage_velocity flips X by direction flag")
{
    // direction != 0 (i.e. moving left) negates X.
    const auto right = smart_bomb_stage_velocity(2, 0);
    const auto left = smart_bomb_stage_velocity(2, 1);
    CHECK_NEAR(right.x, -left.x, 1e-6f);
    CHECK_NEAR(right.y, left.y, 1e-6f);
}

TEST_CASE("smart_bomb_stage_duration matches recovered 16c8:1af5 table")
{
    // Fall stage durations are 0x1e (30) ticks except stage 3 which seeds 0x23 (35).
    // (Stage 0 not part of the fall machine.)
    CHECK_EQ(smart_bomb_stage_duration(0), 30u);
    CHECK_EQ(smart_bomb_stage_duration(1), 30u);
    CHECK_EQ(smart_bomb_stage_duration(2), 30u);
    CHECK_EQ(smart_bomb_stage_duration(3), 30u);
    CHECK_EQ(smart_bomb_stage_duration(4), 35u);
}
