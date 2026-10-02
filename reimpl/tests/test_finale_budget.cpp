// Final-level (gameplay level 0x0c) controller setup. Per nite-wave-scheduling.md
// "Final / Special-Level Path":
//   - ensure DAT_2730_4df0 >= 3
//   - multiply DAT_2730_4df0 by 3
//   - mirror to DAT_2730_4df2 (HUD total)
// and the controller cadence is 0x118 ticks per spawn.

#include "niteraid/game.hpp"
#include "niteraid/game_internals.hpp"
#include "niteraid/finale_particles.hpp"
#include "niteraid/finale_presenter.hpp"
#include "niteraid/presenter_timing.hpp"
#include "test_harness.hpp"

TEST_CASE("single-effect finale serializes departure waits before the same fireworks")
{
    niteraid::WorldState world {};
    world.audio_config_words[4] = 0;
    CHECK(niteraid::presenter_timing::finale_egress_start(world) == 777);
    CHECK(niteraid::presenter_timing::finale_particle_start(world) == 878);
    CHECK(niteraid::presenter_timing::finale_presenter_frames(world) == 2943);
    for (const auto end : {599u, 740u, 776u}) {
        world.finale_presenter_frame = end;
        CHECK(niteraid::original_finale_presenter_frame(world).actors[0].timer == 0xffffffff);
    }
    world.finale_presenter_frame = 600;
    CHECK(niteraid::original_finale_presenter_frame(world).actors[0].timer == 139);
    world.finale_presenter_frame = 777;
    CHECK(niteraid::original_finale_presenter_frame(world).actors[0].vx_fixed == -167772);
    world.finale_presenter_frame = 877;
    CHECK(niteraid::original_finale_presenter_frame(world).actors[0].counter == 0xffff);
    world.finale_presenter_frame = 878;
    CHECK(niteraid::original_finale_presenter_frame(world).actors.empty());
}

using niteraid::Game;
using niteraid::InputState;

namespace {
niteraid::Object falling_finale_drop(float y = 80.0f)
{
    niteraid::Object drop {};
    drop.active = true;
    drop.type = niteraid::ObjectType::Paratrooper;
    drop.finale_drop = true;
    drop.parachute_lost = true;
    drop.assault_stage = 1;
    drop.finale_hits_remaining = 3;
    drop.position = {100.0f, y};
    drop.velocity.y = 0.5f;
    return drop;
}

void add_finale_hit(niteraid::WorldState& world)
{
    const auto& drop = world.objects[0];
    const float next_vy = std::min(0.5f, drop.velocity.y + 0.125f);
    niteraid::Object shot {};
    shot.active = true;
    shot.type = niteraid::ObjectType::PlayerProjectile;
    shot.velocity = {0.25f, -0.5f};
    shot.position = {drop.position.x + drop.velocity.x + 1.75f,
                     drop.position.y + next_vy + 3.5f};
    world.objects.push_back(shot);
}
}

TEST_CASE("finale drops survive three hits and resolve on the fourth without intermediate RNG draws")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.objects.clear();
    world.objects.push_back(falling_finale_drop());
    const auto seed = world.gameplay_rng_seed;
    for (int hit = 1; hit <= 3; ++hit) {
        const float x = world.objects[0].position.x;
        const float next_vy = std::min(0.5f, world.objects[0].velocity.y + 0.125f);
        add_finale_hit(world);
        game.tick(InputState {});
        CHECK(world.objects[0].active && !world.objects[0].pending_destroy);
        CHECK_EQ(world.objects[0].finale_hits_remaining, 3 - hit);
        CHECK_NEAR(world.objects[0].position.x, x + 0.75f, 1e-6f);
        CHECK_NEAR(world.objects[0].velocity.x, 0.0f, 1e-6f);
        CHECK_NEAR(world.objects[0].velocity.y, next_vy - 2.5f, 1e-6f);
        CHECK_EQ(world.scores.score, 0);
        CHECK_EQ(world.gameplay_rng_seed, seed);
        CHECK(world.sound_events.empty());
    }
    add_finale_hit(world);
    game.tick(InputState {});
    CHECK(!world.objects[0].active || world.objects[0].pending_destroy);
    CHECK_EQ(world.scores.score, 2);
    CHECK_EQ(world.scores.grounded_invader_resolutions, 1);
    auto expected = seed;
    for (int draw = 0; draw < 32; ++draw) niteraid::original_random_next(expected);
    CHECK_EQ(world.gameplay_rng_seed, expected);
}

TEST_CASE("IBCD finale hits transfer twice vertical impact velocity instead of five times")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.fast_shots_enabled = true;
    world.objects.clear();
    world.objects.push_back(falling_finale_drop());
    add_finale_hit(world);
    game.tick(InputState {});
    CHECK_EQ(world.objects[0].finale_hits_remaining, 2);
    CHECK_NEAR(world.objects[0].velocity.y, -0.5f, 1e-6f);
    CHECK_NEAR(world.objects[0].position.x, 100.75f, 1e-6f);
}

TEST_CASE("finale ground contact is fatal according to hit budget rather than bunker overlap")
{
    for (const int hits : {0, 1, 2, 3}) {
        Game game(std::uint16_t {0});
        auto& world = game.diagnostic_world();
        world.objects.clear();
        auto drop = falling_finale_drop(174.5f);
        drop.finale_hits_remaining = hits;
        world.objects.push_back(drop);
        game.tick(InputState {});
        CHECK((world.gameplay_state == niteraid::GameplayState::GameOver) == (hits != 0));
        CHECK_EQ(world.scores.score, hits == 0 ? 2 : 0);
    }
}

TEST_CASE("idle finale gameplay loses instead of automatically reaching its winning presenter")
{
    Game game(std::uint16_t {12}, std::uint16_t {1});
    for (int tick = 0; tick < 950 && game.world().gameplay_state != niteraid::GameplayState::GameOver; ++tick) {
        game.tick(InputState {});
    }
    CHECK(game.world().gameplay_state == niteraid::GameplayState::GameOver);
    CHECK(game.world().player_dead);
}

TEST_CASE("finale ground threshold compares the integer coordinate after movement")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.objects.clear();
    world.objects.push_back(falling_finale_drop(174.25f));
    game.tick(InputState {});
    CHECK(!world.player_dead);
    CHECK_NEAR(world.objects[0].position.y, 174.75f, 1e-6f);
    game.tick(InputState {});
    CHECK(world.player_dead);
}

TEST_CASE("hits during finale drop arming do not install a chute-loss callback")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.objects.clear();
    auto drop = falling_finale_drop();
    drop.assault_stage = 0;
    drop.finale_hits_remaining = 0;
    drop.parachute_lost = false;
    drop.velocity = {};
    world.objects.push_back(drop);
    add_finale_hit(world);
    const auto seed = world.gameplay_rng_seed;
    game.tick(InputState {});
    CHECK(world.objects[0].active && !world.objects[0].pending_destroy);
    CHECK(world.objects[0].type == niteraid::ObjectType::Paratrooper);
    CHECK_EQ(world.objects[0].assault_stage, 0);
    CHECK_EQ(world.objects[0].finale_hits_remaining, 0);
    CHECK_EQ(world.gameplay_rng_seed, seed);
}

TEST_CASE("finale arming installs three hits after sixteen stationary updates")
{
    Game game(std::uint16_t {0});
    auto& world = game.diagnostic_world();
    world.objects.clear();
    auto drop = falling_finale_drop();
    drop.assault_stage = 0;
    drop.finale_hits_remaining = 0;
    drop.parachute_lost = false;
    drop.velocity = {};
    world.objects.push_back(drop);
    for (int tick = 1; tick <= 16; ++tick) {
        game.tick(InputState {});
        CHECK_EQ(world.objects[0].finale_hits_remaining, tick == 16 ? 3 : 0);
        CHECK_NEAR(world.objects[0].position.y, 80.0f, 1e-6f);
    }
    CHECK_NEAR(world.objects[0].velocity.y, 0.5f, 1e-6f);
    game.tick(InputState {});
    CHECK_NEAR(world.objects[0].position.y, 80.5f, 1e-6f);
}

TEST_CASE("finale sprite families follow remaining hits and retain initializer collision bounds")
{
    auto drop = falling_finale_drop();
    for (int hits = 3; hits >= 0; --hits) {
        drop.finale_hits_remaining = hits;
        for (int frame = 0; frame < 4; ++frame) {
            drop.frame = frame;
            CHECK_EQ(niteraid::internals::finale_drop_sprite_id(drop), 0x268 + (3 - hits) * 4 + frame);
            const auto bounds = niteraid::internals::collision_bounds_for_object(drop);
            CHECK_NEAR(bounds.left, 100.0f, 1e-6f);
            CHECK_NEAR(bounds.right, 105.0f, 1e-6f);
            CHECK_NEAR(bounds.bottom, 86.0f, 1e-6f);
        }
    }
    drop.assault_stage = 0;
    CHECK_EQ(niteraid::internals::finale_drop_sprite_id(drop), 0x278);
}

TEST_CASE("finale budget on direct entry is max(seed, 3) * 3 = 9")
{
    Game game(std::uint16_t{12}, std::nullopt);  // 0x0c = final
    InputState input {};
    game.tick(input);
    CHECK_EQ(game.world().finale_spawn_budget_remaining, 9u);
    CHECK_EQ(game.world().finale_spawn_budget_total, 9u);
}

TEST_CASE("finale budget honors --finale-budget override")
{
    Game game(std::uint16_t{12}, std::optional<std::uint16_t>{1});
    InputState input {};
    game.tick(input);
    CHECK_EQ(game.world().finale_spawn_budget_remaining, 1u);
    CHECK_EQ(game.world().finale_spawn_budget_total, 1u);
}

TEST_CASE("normal level 1 has no finale budget")
{
    Game game(std::uint16_t{0}, std::nullopt);
    InputState input {};
    game.tick(input);
    CHECK_EQ(game.world().finale_spawn_budget_remaining, 0u);
    CHECK_EQ(game.world().finale_spawn_budget_total, 0u);
}

TEST_CASE("finale particles preserve their reserved slot and pure draw state")
{
    const auto initial = niteraid::original_finale_particle_frame(0xff9652c1u, 0);
    CHECK(!initial.actors[0].active);
    CHECK(initial.actors[1].active);
    CHECK(initial.actors[1].kind == niteraid::FinaleParticleKind::Star);
    CHECK(initial.actors[1].x == (160 << 16));
    CHECK(initial.actors[1].y == (152 << 16));
    CHECK(initial.spawned == 1);
    const auto next = niteraid::original_finale_particle_frame(0xff9652c1u, 1);
    CHECK(next.actors[1].vy == initial.actors[1].vy + 0x800);
    CHECK(next.actors[1].x == initial.actors[1].x + initial.actors[1].vx);
    CHECK(next.actors[1].y == initial.actors[1].y + next.actors[1].vy);
    CHECK(next.random_seed == initial.random_seed);
    const auto repeated = niteraid::original_finale_particle_frame(0xff9652c1u, 1);
    CHECK(repeated.random_seed == next.random_seed);
    CHECK(repeated.actors[1].x == next.actors[1].x);
    CHECK(niteraid::original_finale_particle_frame(0xc6f6338au, 0).random_seed != initial.random_seed);
    CHECK(niteraid::original_finale_particle_frame(0xc6f6338au, 34).spawned == 1);
    CHECK(niteraid::original_finale_particle_frame(0xc6f6338au, 35).spawned == 2);
}

TEST_CASE("finale vehicle and doorway follow completed callback boundaries")
{
    niteraid::WorldState world {};
    world.finale_presenter_timer_origin = 3951;
    auto frame = niteraid::original_finale_presenter_frame(world);
    CHECK(frame.hardware_tick == 3952);
    CHECK(frame.actors[0].x_fixed == 20869120);
    CHECK(frame.actors[0].counter == 79);
    world.finale_presenter_frame = 80;
    frame = niteraid::original_finale_presenter_frame(world);
    CHECK(frame.actors[0].counter == 0xffff);
    CHECK(frame.actors[0].x_fixed < 195 * 65536);
    world.finale_presenter_frame = 81;
    frame = niteraid::original_finale_presenter_frame(world);
    CHECK(frame.actors[0].x_fixed == 195 * 65536);
    CHECK(frame.actors[1].update_callback == 0x3df8);
    world.finale_presenter_frame = 95;
    CHECK(niteraid::original_finale_presenter_frame(world).actors[1].draw_callback == 0x543a);
    world.finale_presenter_frame = 96;
    frame = niteraid::original_finale_presenter_frame(world);
    CHECK(frame.actors[2].x_fixed == 155 * 65536);
    CHECK(frame.actors[2].draw_callback == 0x40b2);
    CHECK(frame.draws[2].sprite == 0x1df);
    world.finale_presenter_frame = 143;
    frame = niteraid::original_finale_presenter_frame(world);
    CHECK(frame.actors[2].x_fixed == 161 * 65536);
    CHECK(frame.actors[2].vx_fixed == 0);
    world.finale_presenter_frame = 144;
    frame = niteraid::original_finale_presenter_frame(world);
    CHECK(frame.actors[1].update_callback == 0x3ef7);
    CHECK(frame.actors[2].y_fixed == 175 * 65536);
    CHECK(frame.actors[2].draw_callback == 0x404a);
    world.finale_presenter_frame = 158;
    CHECK(niteraid::original_finale_presenter_frame(world).actors[1].draw_callback == 0);
    world.finale_presenter_frame = 590;
    frame = niteraid::original_finale_presenter_frame(world);
    CHECK(frame.actors[1].x_fixed == 215 * 65536);
    CHECK(frame.actors[1].animation == 60);
    world.finale_presenter_frame = 626;
    CHECK(niteraid::original_finale_presenter_frame(world).actors[0].timer == 0xffffffff);
    world.finale_presenter_frame = 627;
    CHECK(niteraid::original_finale_presenter_frame(world).actors[0].timer == 34);
    world.finale_presenter_frame = 663;
    CHECK(niteraid::original_finale_presenter_frame(world).actors[0].vx_fixed == -167772);
    world.finale_presenter_frame = 763;
    CHECK(niteraid::original_finale_presenter_frame(world).actors[0].counter == 0xffff);
    world.finale_presenter_frame = 764;
    CHECK(niteraid::original_finale_presenter_frame(world).actors.empty());
    CHECK(niteraid::original_finale_presenter_frame(world).draws.empty());
}

TEST_CASE("finale particle audio uses the shared deterministic burst schedule")
{
    const auto& ticks = niteraid::internals::finale_particle_burst_ticks();
    CHECK_EQ(ticks.size(), 60u);
    CHECK(ticks.front() >= 0x21u);
    for (std::size_t index = 1; index < ticks.size(); ++index) {
        CHECK(ticks[index] > ticks[index - 1]);
        // Later stars use the owner's loop-entry origin, one tick before the
        // first star's birth; their inclusive lifetime is 33..48 updates.
        CHECK(ticks[index] >= static_cast<std::uint32_t>(index) * 0x23u + 0x20u);
        CHECK(ticks[index] <= static_cast<std::uint32_t>(index) * 0x23u + 0x2fu);
    }
}

TEST_CASE("finale particles honor cached free slots and extended viewport destruction")
{
    const auto reused = niteraid::original_finale_particle_frame(0xc6f6338au, 115);
    CHECK(reused.actors[3].vx == -66965);
    CHECK(reused.actors[4].vx == -45720);
    const auto clipped = niteraid::original_finale_particle_frame(0xc6f6338au, 417);
    CHECK(!clipped.actors[30].active);
    for (const auto& actor : clipped.actors) {
        if (!actor.active) continue;
        CHECK(actor.x > -100 * 65536);
        CHECK(actor.y > -40 * 65536);
        CHECK(actor.x < 320 * 65536);
        CHECK(actor.y < 200 * 65536);
    }
}
