#include "niteraid/game.hpp"
#include "niteraid/game_internals.hpp"
#include "test_harness.hpp"

#include <algorithm>
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

namespace {

enum class Chute { Intact, Lost };

void isolate_ground_world(Game& game)
{
    auto& world = game.diagnostic_world();
    world.screen = Screen::Gameplay;
    world.gameplay_state = GameplayState::Active;
    world.objects.clear();
    world.object_highwater.reset();
    world.wave_banks = {};
    world.waves_exhausted = false;
    world.finale_spawn_budget_remaining = 0;
    world.finale_spawn_budget_total = 0;
    world.transition_armed = false;
    world.transition_freeze = false;
    world.bunker_assault_active = false;
    world.bunker_overrun_pending = false;
}

Object make_shell(Vec2 position)
{
    Object shell {};
    shell.active = true;
    shell.type = ObjectType::AircraftDebris;
    shell.aircraft_variant = AircraftVariant::C;
    shell.position = position;
    shell.velocity = {};
    shell.assault_stage = 0;
    shell.timer = 0;
    shell.has_dropped_payload = false;
    return shell;
}

Object make_landed_invader(Vec2 position)
{
    Object invader {};
    invader.active = true;
    invader.type = ObjectType::LandedInvader;
    invader.position = position;
    invader.velocity = {};
    invader.extent = {2.0f, 3.5f};
    return invader;
}

Object make_grounded_transition(Vec2 position, Chute chute, int frame = 0)
{
    Object transition {};
    transition.active = true;
    transition.type = ObjectType::GroundedTransition;
    transition.position = position;
    transition.velocity = {};
    transition.extent = {2.5f, 3.0f};
    transition.parachute_lost = chute == Chute::Lost;
    transition.frame = frame;
    return transition;
}

Object make_grounded_paratrooper(Vec2 position)
{
    Object paratrooper {};
    paratrooper.active = true;
    paratrooper.type = ObjectType::Paratrooper;
    paratrooper.position = position;
    paratrooper.velocity = {};
    paratrooper.extent = {2.5f, 3.0f};
    paratrooper.assault_stage = 1;
    return paratrooper;
}

Object make_finale_controller(float x)
{
    Object controller {};
    controller.active = true;
    controller.type = ObjectType::FinaleController;
    controller.position = {x, 30.0f};
    controller.assault_stage = 1;
    return controller;
}

void check_particle_slots(const WorldState& world, std::size_t first, std::size_t last)
{
    for (std::size_t index = first; index <= last; ++index) {
        CHECK(world.objects[index].type == ObjectType::ResolutionParticle);
    }
}

TEST_CASE("captured landing retains velocity and F7 bounds without moving during 0559")
{
    Game game(std::uint16_t {0});
    isolate_ground_world(game);
    auto& world = game.diagnostic_world();
    auto trooper = make_grounded_paratrooper({-14.625f, 163.75f});
    trooper.velocity = {0.015625f, 0.25f};
    trooper.extent = {5.0f, 7.5f};
    world.objects.push_back(trooper);
    const auto seed = world.gameplay_rng_seed;

    game.tick(InputState {});

    CHECK(world.objects[0].type == ObjectType::GroundedTransition);
    CHECK_EQ(world.objects[0].position.x, -14.609375);
    CHECK_EQ(world.objects[0].position.y, 164.0);
    CHECK_EQ(world.objects[0].timer, 0u);
    CHECK_EQ(world.objects[0].frame, 0);
    for (int update = 0; update < 8; ++update) {
        const auto& actor = world.objects[0];
        CHECK(actor.active);
        CHECK_EQ(actor.position.x, -14.609375);
        CHECK_EQ(actor.position.y, 164.0);
        CHECK_EQ(actor.velocity.x, 0.015625f);
        CHECK_EQ(actor.velocity.y, 0.25f);
        const auto bounds = niteraid::internals::collision_bounds_for_object(actor);
        CHECK_EQ(bounds.right - bounds.left, 6.0);
        CHECK_EQ(bounds.bottom - bounds.top, 15.0);
        CHECK_EQ(world.gameplay_rng_seed, seed);
        game.tick(InputState {});
    }
}

TEST_CASE("0559 conversion retains velocity while the landed record remains stationary")
{
    Game game(std::uint16_t {0});
    isolate_ground_world(game);
    auto& world = game.diagnostic_world();
    auto trooper = make_grounded_transition({100.375f, 164.0f}, Chute::Intact, 7);
    trooper.velocity = {0.015625f, 0.25f};
    trooper.extent = {3.0f, 7.5f};
    world.objects.push_back(trooper);

    game.tick(InputState {});

    for (int update = 0; update < 5; ++update) {
        const auto& actor = world.objects[0];
        CHECK(actor.active);
        CHECK(actor.type == ObjectType::LandedInvader);
        CHECK_EQ(actor.position.x, 103.0);
        CHECK_EQ(actor.position.y, 172.0);
        CHECK_EQ(actor.velocity.x, 0.015625f);
        CHECK_EQ(actor.velocity.y, 0.25f);
        const auto bounds = niteraid::internals::collision_bounds_for_object(actor);
        CHECK_EQ(bounds.right - bounds.left, 4.0);
        CHECK_EQ(bounds.bottom - bounds.top, 7.0);
        game.tick(InputState {});
    }
}

TEST_CASE("landed animation retains 0401 chute and body contacts in both slot orders")
{
    enum class Impact { Projectile, Fragment };
    struct Contact { Impact impact; double y; bool body_hit; };
    constexpr double kFixedUnit = 1.0 / 65536.0;
    constexpr Contact kContacts[] {
        {Impact::Projectile, 175.0 - kFixedUnit, false},
        {Impact::Projectile, 175.0, true},
        {Impact::Projectile, 175.0 + kFixedUnit, true},
        {Impact::Fragment, 162.0, false},
        {Impact::Fragment, 175.0, true},
    };
    for (const auto contact : kContacts) {
        for (const bool target_first : {false, true}) {
            Game game(std::uint16_t {0});
            isolate_ground_world(game);
            auto& world = game.diagnostic_world();
            world.gameplay_rng_seed = 1;
            auto trooper = make_grounded_transition({100.25f, 164.0f}, Chute::Intact);
            trooper.velocity = {0.015625f, 0.25f};
            trooper.extent = {3.0f, 7.5f};
            Object impact {};
            impact.active = true;
            impact.position = {105.75, contact.y};
            if (contact.impact == Impact::Projectile) {
                impact.type = ObjectType::PlayerProjectile;
                impact.extent = {1, 1};
            } else {
                impact.type = ObjectType::AircraftDebris;
                impact.has_dropped_payload = true;
                impact.sprite_id = 0x143;
                impact.extent = {3, 3};
                impact.velocity.y = -0.03125f;
            }
            world.objects = target_first ? std::vector<Object> {trooper, impact}
                                         : std::vector<Object> {impact, trooper};
            const auto target_slot = target_first ? 0u : 1u;
            const auto impact_slot = target_first ? 1u : 0u;

            game.tick(InputState {});

            CHECK_EQ(world.objects[target_slot].active, !contact.body_hit);
            CHECK_EQ(world.objects[impact_slot].active, contact.impact == Impact::Fragment);
            CHECK_EQ(world.scores.score, contact.body_hit ? 2u : 0u);
            CHECK_EQ(world.scores.grounded_invader_resolutions, contact.body_hit ? 1u : 0u);
            const auto particles = std::count_if(world.objects.begin(), world.objects.end(),
                [](const Object& actor) {
                    return actor.active && actor.type == ObjectType::ResolutionParticle;
                });
            CHECK_EQ(particles, contact.body_hit ? 8 : 0);
            if (contact.body_hit) {
                continue;
            }
            const auto& actor = world.objects[target_slot];
            CHECK(actor.parachute_lost);
            CHECK_EQ(actor.position.x, 103.25);
            CHECK_EQ(actor.position.y, 175.0);
            CHECK_EQ(actor.velocity.x, 0.015625f);
            CHECK_EQ(actor.velocity.y, 0.5f);
            CHECK_EQ(actor.frame, 0);
            CHECK_EQ(actor.timer, 1u);
            const auto bounds = niteraid::internals::collision_bounds_for_object(actor);
            CHECK_EQ(bounds.right - bounds.left, 5.0);
            CHECK_EQ(bounds.bottom - bounds.top, 6.0);
            CHECK_EQ(world.gameplay_rng_seed, 1u);
        }
    }
}

TEST_CASE("expired particle retains the original freed sprite sentinel")
{
    Game game(std::uint16_t {12});
    isolate_ground_world(game);
    auto& world = game.diagnostic_world();
    Object particle {};
    particle.active = true;
    particle.type = ObjectType::ResolutionParticle;
    particle.frame = 12;
    particle.sprite_id = 12;
    particle.assault_stage = 0;
    world.objects.push_back(particle);

    game.tick(InputState {});

    CHECK(!world.objects[0].active);
    CHECK_EQ(world.objects[0].frame, 0xffff);
    CHECK_EQ(world.objects[0].sprite_id, -1);
    CHECK(world.object_highwater.has_value());
    if (world.object_highwater) {
        CHECK_EQ(*world.object_highwater, std::size_t {0});
    }
}

TEST_CASE("falling fragment advances its animation before ground culling")
{
    Game game(std::uint16_t {12});
    isolate_ground_world(game);
    auto& world = game.diagnostic_world();
    auto fragment = make_shell({158.27984619140625f, 198.4813690185546875f});
    fragment.has_dropped_payload = true;
    fragment.sprite_id = 0x131;
    fragment.velocity = {-0.443450927734375f, 2.6706390380859375f};
    fragment.frame = 8;
    fragment.timer = 5;
    world.objects.push_back(fragment);

    game.tick(InputState {});

    CHECK(!world.objects[0].active);
    CHECK_EQ(world.objects[0].frame, 9);
    CHECK_EQ(world.objects[0].timer, 0u);
}

TEST_CASE("tail retirement lowers logical high-water without trimming older holes")
{
    Game game(std::uint16_t {12});
    isolate_ground_world(game);
    auto& world = game.diagnostic_world();
    Object cannon {};
    cannon.active = true;
    cannon.type = ObjectType::PlayerCannon;
    world.objects.push_back(cannon);
    world.objects.push_back(Object {});
    auto fragment = make_shell({158.0f, 201.0f});
    fragment.has_dropped_payload = true;
    world.objects.push_back(fragment);

    game.tick(InputState {});

    CHECK_EQ(world.objects.size(), std::size_t {3});
    CHECK(!world.objects[2].active);
    CHECK(world.object_highwater.has_value());
    if (world.object_highwater) {
        CHECK_EQ(*world.object_highwater, std::size_t {2});
    }
}

void prepare_captured_cleanup(Game& game, std::size_t landed_count)
{
    isolate_ground_world(game);
    auto& world = game.diagnostic_world();
    world.gameplay_rng_seed = 1;
    world.gameplay_state = GameplayState::ScriptedSequence;
    world.bunker_assault_active = true;
    world.bunker_assault_cleanup_pending = true;
    world.frame_tick = 2;
    world.bunker_assault_step_tick = 1;
    for (std::size_t slot = 0; slot < landed_count; ++slot) {
        auto landed = make_landed_invader({148.0f + static_cast<float>(slot), 172.0f});
        landed.sprite_id = 0x0f8;
        world.objects.push_back(landed);
    }

    // Match the captured 1489 callback boundary, not a newly created shell.
    auto shell = make_shell({250.0f, 100.0f});
    shell.extent = {22.0f, 6.5f};
    shell.sprite_id = 0x15b;
    shell.direction = 1;
    shell.timer = 6;
    shell.frame = 5;
    shell.assault_stage = 5;
    world.objects.push_back(shell);
    world.objects.shrink_to_fit();
}

}  // namespace

TEST_CASE("captured cleanup-four preserves slot 15 fixed-point position")
{
    Game game(std::uint16_t {12});
    prepare_captured_cleanup(game, 4);
    game.tick(InputState {});
    const auto& world = game.world();

    CHECK_EQ(world.objects.size(), std::size_t {16});
    if (world.objects.size() != 16) {
        return;
    }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        CHECK(world.objects[slot].active);
        CHECK(world.objects[slot].type == ObjectType::LandedInvader);
    }
    CHECK(world.objects[3].active);
    CHECK(world.objects[3].type == ObjectType::AircraftDebris);
    CHECK_EQ(world.objects[3].timer, 0u);
    CHECK(!world.objects[4].active);
    CHECK_EQ(world.objects[4].timer, 7u);
    CHECK_EQ(world.objects[4].frame, 6);
    CHECK_EQ(world.objects[4].sprite_id, 0x15b);
    check_particle_slots(world, 5, 12);
    for (std::size_t slot = 13; slot <= 15; ++slot) {
        CHECK(world.objects[slot].active);
        CHECK(world.objects[slot].type == ObjectType::AircraftDebris);
        CHECK_EQ(world.objects[slot].timer, 1u);
    }
    CHECK_EQ(world.scores.score, 2);
    CHECK_EQ(world.scores.grounded_invader_resolutions, 1);
    CHECK_EQ(world.gameplay_rng_seed, 908527603u);
    CHECK_EQ(world.objects[15].velocity.x, 2585.0 / 65536.0);
    CHECK_EQ(world.objects[15].position.x, 260.236663818359375);
}

TEST_CASE("captured cleanup-five preserves slot 4 collision width and endpoint")
{
    Game game(std::uint16_t {12});
    prepare_captured_cleanup(game, 5);
    game.tick(InputState {});
    const auto& world = game.world();

    CHECK_EQ(world.objects.size(), std::size_t {24});
    if (world.objects.size() != 24) {
        return;
    }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        CHECK(world.objects[slot].active);
        CHECK(world.objects[slot].type == ObjectType::LandedInvader);
    }
    check_particle_slots(world, 3, 3);
    check_particle_slots(world, 6, 20);
    CHECK(!world.objects[5].active);
    CHECK_EQ(world.objects[5].timer, 7u);
    CHECK_EQ(world.objects[5].frame, 6);
    CHECK_EQ(world.objects[5].sprite_id, 0x15b);
    for (std::size_t slot = 21; slot <= 23; ++slot) {
        CHECK(world.objects[slot].active);
        CHECK(world.objects[slot].type == ObjectType::AircraftDebris);
        CHECK_EQ(world.objects[slot].timer, 1u);
    }
    CHECK_EQ(world.scores.score, 4);
    CHECK_EQ(world.scores.grounded_invader_resolutions, 2);
    CHECK_EQ(world.gameplay_rng_seed, 869595475u);

    const auto& child = world.objects[4];
    CHECK(child.active);
    CHECK(child.type == ObjectType::AircraftDebris);
    CHECK_EQ(child.sprite_id, 0x143);
    CHECK_EQ(child.timer, 0u);
    CHECK_EQ(child.position.x, 253.6293792724609375);
    const auto bounds = niteraid::internals::collision_bounds_for_object(child);
    CHECK_EQ(bounds.left, 253.6293792724609375);
    CHECK_EQ(bounds.right, 265.6293792724609375);
    CHECK_EQ(bounds.right - bounds.left, 12.0);
}

TEST_CASE("cut-chute animation retains the original 111 collision bounds")
{
    auto actor = make_grounded_transition({100.25f, 80.5f}, Chute::Lost);
    for (int frame = 0; frame < 4; ++frame) {
        actor.frame = frame;
        const auto bounds = niteraid::internals::collision_bounds_for_object(actor);
        CHECK_EQ(bounds.left, 100.25f);
        CHECK_EQ(bounds.top, 80.5f);
        CHECK_EQ(bounds.right - bounds.left, 5.0f);
        CHECK_EQ(bounds.bottom - bounds.top, 6.0f);
    }
}

TEST_CASE("lost-chute resolution preserves nearby landed invader and reuses source slot")
{
    {
        Game game(std::uint16_t {12});
        isolate_ground_world(game);
        auto& world = game.diagnostic_world();
        world.objects.push_back(make_grounded_transition({100.0f, 174.5f}, Chute::Lost));
        world.objects.push_back(make_landed_invader({100.0f, 172.0f}));
        world.objects.shrink_to_fit();

        game.tick(InputState {});

        // 034b compares the original high word, so 174.5 is not past 174.
        CHECK(world.objects[0].active);
        CHECK(world.objects[0].type == ObjectType::GroundedTransition);
        CHECK(world.objects[1].active);
        CHECK(world.objects[1].type == ObjectType::LandedInvader);
        CHECK_EQ(world.objects.size(), std::size_t {2});
    }

    Game game(std::uint16_t {12});
    isolate_ground_world(game);
    auto& world = game.diagnostic_world();
    world.objects.push_back(make_grounded_transition({100.0f, 175.0f}, Chute::Lost));
    world.objects.push_back(make_landed_invader({100.0f, 172.0f}));
    auto shell = make_shell({250.0f, 100.0f});
    shell.sprite_id = 0x121;
    world.objects.push_back(shell);
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    // The nearby landed actor is not a merge target for 034b. The source is
    // retired only after eight particles, making slot 0 available to the shell.
    CHECK_EQ(world.objects.size(), std::size_t {14});
    if (world.objects.size() < 14) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::AircraftDebris);
    CHECK(world.objects[0].has_dropped_payload);
    CHECK(world.objects[1].active);
    CHECK(world.objects[1].type == ObjectType::LandedInvader);
    CHECK(!world.objects[2].active);
    CHECK_EQ(world.objects[2].frame, 1);
    CHECK_EQ(world.objects[2].sprite_id, 0x121);
    check_particle_slots(world, 3, 10);
    for (std::size_t index = 3; index <= 10; ++index) {
        const auto bounds = niteraid::internals::collision_bounds_for_object(world.objects[index]);
        CHECK_EQ(bounds.right - bounds.left, 0.0f);
        CHECK_EQ(bounds.bottom - bounds.top, 0.0f);
    }
    CHECK_EQ(world.objects[0].timer, 0u);
    CHECK_EQ(world.scores.score, 2);
    CHECK_EQ(world.scores.grounded_invader_resolutions, 1);
}

TEST_CASE("0559 merge retires the grounded source rather than its landed target")
{
    Game game(std::uint16_t {12});
    isolate_ground_world(game);
    auto& world = game.diagnostic_world();
    world.objects.push_back(make_grounded_transition({100.0f, 164.0f}, Chute::Intact, 7));
    world.objects.push_back(make_landed_invader({103.0f, 172.0f}));
    world.objects.push_back(make_shell({250.0f, 100.0f}));
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    // Conversion lands at (103,172), then 0559 resolves the current record
    // against the existing target. The later shell must consume source slot 0.
    CHECK_EQ(world.objects.size(), std::size_t {14});
    if (world.objects.size() < 14) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::AircraftDebris);
    CHECK(world.objects[0].has_dropped_payload);
    CHECK(world.objects[1].active);
    CHECK(world.objects[1].type == ObjectType::LandedInvader);
    CHECK(!world.objects[2].active);
    check_particle_slots(world, 3, 10);
    CHECK_EQ(world.objects[0].timer, 0u);
    CHECK_EQ(world.scores.score, 2);
    CHECK_EQ(world.scores.grounded_invader_resolutions, 1);
}

TEST_CASE("edge cleanup releases its slot after particles and survives table growth")
{
    Game game(std::uint16_t {12});
    isolate_ground_world(game);
    auto& world = game.diagnostic_world();
    world.objects.push_back(make_landed_invader({-1.0f, 172.0f}));
    world.objects.push_back(make_shell({250.0f, 100.0f}));
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    CHECK_EQ(world.objects.size(), std::size_t {13});
    if (world.objects.size() < 13) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::AircraftDebris);
    CHECK_EQ(world.objects[0].timer, 0u);
    CHECK(!world.objects[1].active);
    check_particle_slots(world, 2, 9);
    CHECK_EQ(world.scores.grounded_invader_resolutions, 1);
}

TEST_CASE("bunker ground resolution releases its slot after the particle factory")
{
    Game game(std::uint16_t {12});
    isolate_ground_world(game);
    auto& world = game.diagnostic_world();
    world.objects.push_back(make_grounded_paratrooper({160.0f, 164.0f}));
    world.objects.push_back(make_shell({250.0f, 100.0f}));
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    CHECK_EQ(world.objects.size(), std::size_t {13});
    if (world.objects.size() < 13) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::AircraftDebris);
    CHECK_EQ(world.objects[0].timer, 0u);
    CHECK(!world.objects[1].active);
    check_particle_slots(world, 2, 9);
    CHECK_EQ(world.scores.grounded_invader_resolutions, 1);
}

TEST_CASE("scripted cleanup retires each excess landed actor before the next allocation")
{
    Game game(std::uint16_t {12});
    auto& world = game.diagnostic_world();
    world.screen = Screen::Gameplay;
    world.gameplay_state = GameplayState::ScriptedSequence;
    world.bunker_assault_active = true;
    world.bunker_assault_cleanup_pending = true;
    world.frame_tick = 2;
    world.bunker_assault_step_tick = 1;
    world.objects.clear();
    world.objects.push_back(make_landed_invader({148.0f, 172.0f}));
    world.objects.push_back(make_landed_invader({149.0f, 172.0f}));
    world.objects.push_back(make_landed_invader({150.0f, 172.0f}));
    world.objects.push_back(make_landed_invader({151.0f, 172.0f}));
    world.objects.push_back(make_shell({250.0f, 100.0f}));
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    // 04bc -> 026e emits eight particles, then 567f releases the excess
    // source. The later shell therefore reuses that source slot.
    CHECK_EQ(world.objects.size(), std::size_t {16});
    if (world.objects.size() < 16) {
        return;
    }
    CHECK(world.objects[3].active);
    CHECK(world.objects[3].type == ObjectType::AircraftDebris);
    CHECK_EQ(world.objects[3].timer, 0u);
    CHECK(!world.objects[4].active);
    check_particle_slots(world, 5, 12);
    for (std::size_t index = 13; index <= 15; ++index) {
        CHECK(world.objects[index].active);
        CHECK(world.objects[index].type == ObjectType::AircraftDebris);
        CHECK_EQ(world.objects[index].timer, 1u);
    }
}

TEST_CASE("finale controller retirement exposes its slot to later update callbacks")
{
    Game game(std::uint16_t {12});
    auto& world = game.diagnostic_world();
    world.screen = Screen::Gameplay;
    world.gameplay_state = GameplayState::ScriptedSequence;
    world.bunker_assault_active = true;
    world.objects.clear();
    world.objects.push_back(make_finale_controller(263.0f));
    world.objects.push_back(make_shell({250.0f, 100.0f}));
    world.objects.shrink_to_fit();

    game.tick(InputState {});

    // 215d calls 567f as soon as the controller crosses 0x107.
    CHECK_EQ(world.objects.size(), std::size_t {5});
    if (world.objects.size() < 5) {
        return;
    }
    CHECK(world.objects[0].active);
    CHECK(world.objects[0].type == ObjectType::AircraftDebris);
    CHECK_EQ(world.objects[0].timer, 0u);
    CHECK(!world.objects[1].active);
    for (std::size_t index = 2; index <= 4; ++index) {
        CHECK(world.objects[index].active);
        CHECK(world.objects[index].type == ObjectType::AircraftDebris);
        CHECK_EQ(world.objects[index].timer, 1u);
    }
}
