#include "niteraid/game.hpp"
#include "niteraid/game_internals.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using niteraid::AircraftVariant;
using niteraid::Game;
using niteraid::GameplayState;
using niteraid::InputState;
using niteraid::Object;
using niteraid::ObjectType;
using niteraid::Screen;
using niteraid::Vec2;
using niteraid::WorldState;

namespace niteraid {
struct WreckageProbeAccess {
    static void collide(Game& game) { game.check_object_collisions(); }
    static void update(Game& game) { game.update_active_objects(); }
    static void store(Game& game, Object object) { game.store_object(std::move(object)); }
    static void enter_intermission(Game& game) { game.handle_level_transition_or_intermission(); }
    static std::optional<std::size_t> free_slot(const Game& game) { return game.reusable_object_slot_; }
    static void reset_free_slot(Game& game) { game.reusable_object_slot_.reset(); }
};
}

namespace {

struct Fixture {
    std::string name;
    std::string kind;
    std::vector<std::string> roles;
    std::size_t source = 0;
    std::size_t target = 0;
    bool source_first = true;
};

struct CaseState {
    std::vector<Object> objects;
    std::int32_t score = 0;
    std::uint16_t kills = 0;
    std::uint32_t rng = 0;
};

void isolate(Game& game)
{
    auto& world = game.diagnostic_world();
    world.screen = Screen::Gameplay;
    world.gameplay_state = GameplayState::Active;
    world.objects.clear();
    world.object_highwater.reset();
    world.wave_banks = {};
    world.finale_spawn_budget_remaining = 0;
    world.finale_spawn_budget_total = 0;
    world.waves_exhausted = false;
    world.transition_armed = false;
    world.transition_freeze = false;
    world.bunker_assault_active = false;
    world.bunker_overrun_pending = false;
    world.scores = {};
    niteraid::WreckageProbeAccess::reset_free_slot(game);
}

Object fragment(Vec2 position, float width, float height, int sprite = 0)
{
    Object object {};
    object.active = true;
    object.type = ObjectType::AircraftDebris;
    object.aircraft_variant = AircraftVariant::D;
    object.position = position;
    object.velocity = {};
    object.extent = {width / 2.0f, height / 2.0f};
    object.sprite_id = sprite;
    object.has_dropped_payload = true;
    return object;
}

Object aircraft(AircraftVariant variant, Vec2 position, float width, float height)
{
    Object object {};
    object.active = true;
    object.type = ObjectType::Aircraft;
    object.aircraft_variant = variant;
    object.position = position;
    object.extent = {width / 2.0f, height / 2.0f};
    object.direction = 1;
    return object;
}

Object projectile(Vec2 position, float width = 3.0f, float height = 3.0f)
{
    Object object {};
    object.active = true;
    object.type = ObjectType::PlayerProjectile;
    object.position = position;
    object.extent = {width / 2.0f, height / 2.0f};
    return object;
}

Object bomb(Vec2 position, bool falling)
{
    Object object {};
    object.active = true;
    object.type = ObjectType::SmartBomb;
    object.position = position;
    object.timer = 1;
    object.extent = {9.0f, 3.0f};
    object.assault_stage = falling ? 1 : 0;
    return object;
}

Object enemy_death(Vec2 position, float width = 20.0f, float height = 10.0f)
{
    Object object {};
    object.active = true;
    object.type = ObjectType::EnemyDeath;
    object.position = position;
    object.extent = {width / 2.0f, height / 2.0f};
    object.assault_stage = 6;
    object.timer = 1;
    return object;
}

Object stale_hole()
{
    Object object {};
    object.position = {-200.0f, -200.0f};
    object.extent = {0.5f, 0.5f};
    return object;
}

void append_aircraft_pair(Game& game, AircraftVariant variant, bool reverse)
{
    auto& objects = game.diagnostic_world().objects;
    const auto [width, height] = [&]() {
        switch (variant) {
        case AircraftVariant::A: return std::pair {38.0f, 14.0f};
        case AircraftVariant::B: return std::pair {63.0f, 13.0f};
        case AircraftVariant::C: return std::pair {44.0f, 13.0f};
        case AircraftVariant::D: return std::pair {69.0f, 17.0f};
        }
        return std::pair {38.0f, 14.0f};
    }();
    const auto shell = fragment({100.0f, 40.0f}, width, height);
    const auto target = aircraft(variant, {100.0f, 40.0f}, width, height);
    if (reverse) {
        objects.push_back(target);
        objects.push_back(shell);
    } else {
        objects.push_back(shell);
        objects.push_back(target);
    }
}

void append_smartbomb_pair(Game& game, bool falling, bool reverse)
{
    auto& objects = game.diagnostic_world().objects;
    const auto shell = fragment({100.0f, 40.0f}, 20.0f, 10.0f);
    const auto target = bomb({100.0f, 40.0f}, falling);
    if (reverse) {
        objects.push_back(target);
        objects.push_back(shell);
    } else {
        objects.push_back(shell);
        objects.push_back(target);
    }
}

void configure_fixture(Game& game, const Fixture& fixture)
{
    auto& objects = game.diagnostic_world().objects;
    const auto& name = fixture.name;
    if (name.rfind("aircraft_", 0) == 0) {
        const auto variant = name[9] == 'a' ? AircraftVariant::A :
                             name[9] == 'b' ? AircraftVariant::B :
                             name[9] == 'c' ? AircraftVariant::C : AircraftVariant::D;
        append_aircraft_pair(game, variant, !fixture.source_first);
        return;
    }
    if (name.rfind("smartbomb-", 0) == 0) {
        append_smartbomb_pair(game, name.find("falling") != std::string::npos,
                              !fixture.source_first);
        return;
    }
    if (name == "type6-aircraft-mask-negative") {
        objects.push_back(enemy_death({100.0f, 40.0f}));
        objects.push_back(aircraft(AircraftVariant::A, {100.0f, 40.0f}, 20.0f, 10.0f));
        return;
    }
    if (name == "type7-aircraft-nonoverlap-negative") {
        objects.push_back(fragment({20.0f, 20.0f}, 20.0f, 10.0f));
        objects.push_back(aircraft(AircraftVariant::A, {200.0f, 100.0f}, 20.0f, 10.0f));
        return;
    }
    if (name == "mixed-aircraft-debris-projectile") {
        objects.push_back(aircraft(AircraftVariant::C, {120.0f, 100.0f}, 44.0f, 13.0f));
        objects.push_back(fragment({100.0f, 100.0f}, 19.0f, 17.0f, 0x121));
        objects.push_back(projectile({118.5f, 99.0f}));
        return;
    }
    if (name == "mixed-aircraft-projectile-debris") {
        objects.push_back(aircraft(AircraftVariant::C, {120.0f, 100.0f}, 44.0f, 13.0f));
        objects.push_back(projectile({118.5f, 99.0f}));
        objects.push_back(fragment({100.0f, 100.0f}, 19.0f, 17.0f, 0x121));
        return;
    }
    if (name == "mixed-chain-aircraft-projectile-aircraft") {
        objects.push_back(aircraft(AircraftVariant::C, {100.0f, 100.0f}, 44.0f, 13.0f));
        objects.push_back(projectile({119.0f, 99.0f}));
        objects.push_back(aircraft(AircraftVariant::D, {140.0f, 100.0f}, 69.0f, 17.0f));
        return;
    }
    if (name == "mixed-chain-projectile-aircraft-aircraft") {
        objects.push_back(projectile({119.0f, 99.0f}));
        objects.push_back(aircraft(AircraftVariant::C, {100.0f, 100.0f}, 44.0f, 13.0f));
        objects.push_back(aircraft(AircraftVariant::D, {140.0f, 100.0f}, 69.0f, 17.0f));
        return;
    }
    if (name == "allocator-cached-slot-over-older-hole") {
        objects.push_back(stale_hole());
        objects.push_back(fragment({100.0f, 100.0f}, 19.0f, 17.0f, 0x121));
        objects.push_back(projectile({118.5f, 99.0f}));
        objects.push_back(aircraft(AircraftVariant::C, {100.0f, 100.0f}, 44.0f, 13.0f));
        return;
    }
    if (name == "projectile-retirement-continues-inner-scan") {
        objects.push_back(aircraft(AircraftVariant::C, {95.0f, 100.0f}, 8.0f, 10.0f));
        objects.push_back(aircraft(AircraftVariant::D, {108.0f, 100.0f}, 8.0f, 10.0f));
        objects.push_back(projectile({100.0f, 100.0f}, 10.0f, 10.0f));
        objects.push_back(enemy_death({250.0f, 150.0f}, 4.0f, 4.0f));
        objects.push_back(aircraft(AircraftVariant::C, {113.0f, 100.0f}, 8.0f, 10.0f));
        return;
    }
    throw std::runtime_error("unknown wreckage fixture: " + name);
}

std::vector<Fixture> fixtures()
{
    std::vector<Fixture> result;
    for (const auto [letter, kind] : std::array<std::pair<char, std::string>, 4> {
             std::pair {'a', "aircraft"}, {'b', "aircraft"}, {'c', "aircraft"}, {'d', "aircraft"}}) {
        for (const bool reverse : {false, true}) {
            result.push_back({"aircraft_" + std::string(1, letter) +
                                  (reverse ? "-reverse-slot-order" : "-overlap"),
                              kind, reverse ? std::vector<std::string>{"target", "source"}
                                  : std::vector<std::string>{"source", "target"},
                              reverse ? 1u : 0u, reverse ? 0u : 1u, !reverse});
        }
    }
    for (const auto state : {std::string("armed"), std::string("falling")}) {
        for (const bool reverse : {false, true}) {
            result.push_back({"smartbomb-" + state +
                                  (reverse ? "-reverse-slot-order" : "-overlap"),
                              "smartbomb", reverse ? std::vector<std::string>{"target", "source"}
                                  : std::vector<std::string>{"source", "target"}, reverse ? 1u : 0u,
                              reverse ? 0u : 1u, !reverse});
        }
    }
    result.push_back({"type6-aircraft-mask-negative", "type6-negative", {"source", "target"}, 0, 1, true});
    result.push_back({"type7-aircraft-nonoverlap-negative", "nonoverlap-negative", {"source", "target"}, 0, 1, true});
    result.push_back({"mixed-aircraft-debris-projectile", "mixed-projectile-order",
                      {"aircraft", "debris", "projectile"}, 2, 0, false});
    result.push_back({"mixed-aircraft-projectile-debris", "mixed-projectile-order",
                      {"aircraft", "projectile", "debris"}, 1, 0, false});
    result.push_back({"mixed-chain-aircraft-projectile-aircraft", "mixed-chain-order",
                      {"aircraft_c", "projectile", "aircraft_d"}, 1, 0, false});
    result.push_back({"mixed-chain-projectile-aircraft-aircraft", "mixed-chain-order",
                      {"projectile", "aircraft_c", "aircraft_d"}, 0, 1, true});
    result.push_back({"allocator-cached-slot-over-older-hole", "bounded-runtime",
                      {"older-hole", "debris", "projectile", "aircraft"}, 2, 3, false});
    result.push_back({"projectile-retirement-continues-inner-scan", "bounded-runtime",
                      {"visited-aircraft-a", "visited-aircraft-b", "outer-projectile",
                       "occupied-death", "later-aircraft"}, 2, 0, true});
    return result;
}

int type_id(ObjectType type) { return static_cast<int>(type); }

void json_string(std::ostream& out, std::string_view value)
{
    out << '"';
    for (const char character : value) {
        if (character == '"' || character == '\\') out << '\\';
        out << character;
    }
    out << '"';
}

int original_update_callback(const Object& object, std::size_t slot)
{
    if (object.type == ObjectType::None && slot == 0) return 0x4444;
    if (object.type == ObjectType::SmartBomb) return object.assault_stage == 1 ? 6901 : 7066;
    return 0;
}

int original_collision_callback(const Object& object, std::size_t slot)
{
    if (object.type == ObjectType::None && slot == 0) return 0x5555;
    switch (object.type) {
    case ObjectType::Aircraft:
        switch (object.aircraft_variant) {
        case AircraftVariant::A: return 3849;
        case AircraftVariant::B: return 4641;
        case AircraftVariant::C: return 5421;
        case AircraftVariant::D: return 3058;
        }
        break;
    case ObjectType::PlayerProjectile: return 360;
    case ObjectType::SmartBomb: return 7203;
    default: break;
    }
    return 0;
}

int original_draw_callback(const Object& object, std::size_t slot)
{
    if (object.type == ObjectType::None && slot == 0) return 0x6666;
    return 0;
}

enum class CallbackFields { LegacyMapping, Unavailable };

void json_object(std::ostream& out, const Object& object, std::size_t slot,
                 CallbackFields callbacks = CallbackFields::LegacyMapping)
{
    const auto sprite = object.sprite_id < 0 ? 0 : object.sprite_id;
    const auto bounds = niteraid::internals::collision_bounds_for_object(object);
    out << "{\"slot\":" << slot << ",\"active\":" << (object.active ? "true" : "false")
        << ",\"pending_destroy\":" << (object.pending_destroy ? "true" : "false")
        << ",\"type_id\":" << type_id(object.type)
        << ",\"primary_type_id\":" << type_id(object.type)
        << ",\"secondary_type\":null"
        << ",\"secondary_type_available\":false"
        << ",\"type_model\":\"runtime_primary_only\""
        << ",\"aircraft_variant\":" << static_cast<int>(object.aircraft_variant)
        << ",\"x\":" << object.position.x << ",\"y\":" << object.position.y
        << ",\"width\":" << object.extent.x * 2.0f
        << ",\"height\":" << object.extent.y * 2.0f
        << ",\"collision_width\":" << bounds.right - bounds.left
        << ",\"collision_height\":" << bounds.bottom - bounds.top
        << ",\"vx\":" << object.velocity.x << ",\"vy\":" << object.velocity.y
        << ",\"frame\":" << object.frame << ",\"sprite\":" << sprite
        << ",\"timer\":" << object.timer << ",\"assault_stage\":" << object.assault_stage
        << ",\"parachute_lost\":" << (object.parachute_lost ? "true" : "false");
    if (callbacks == CallbackFields::Unavailable) {
        out << ",\"direction\":" << object.direction
            << ",\"has_dropped_payload\":" << (object.has_dropped_payload ? "true" : "false")
            << ",\"update_callback\":null,\"collision_callback\":null,\"draw_callback\":null}";
        return;
    }
    out << ",\"update_callback\":" << original_update_callback(object, slot)
        << ",\"collision_callback\":" << original_collision_callback(object, slot)
        << ",\"draw_callback\":" << original_draw_callback(object, slot) << "}";
}

void json_state(std::ostream& out, const CaseState& state)
{
    out << "{\"object_highwater\":" << state.objects.size()
        << ",\"score\":" << state.score << ",\"enemy_kills\":" << state.kills
        << ",\"rng_seed\":" << state.rng << ",\"objects\":[";
    for (std::size_t index = 0; index < state.objects.size(); ++index) {
        if (index != 0) out << ',';
        json_object(out, state.objects[index], index);
    }
    out << "]}";
}
CaseState snapshot(const WorldState& world, bool trim_trailing_inactive)
{
    CaseState state {
        world.objects,
        world.scores.score,
        world.scores.enemy_kills,
        world.gameplay_rng_seed,
    };
    if (trim_trailing_inactive) {
        while (!state.objects.empty() && !state.objects.back().active) {
            state.objects.pop_back();
        }
    }
    return state;
}

void emit_case(std::ostream& out, const Fixture& fixture)
{
    Game game(std::uint16_t {12});
    isolate(game);
    configure_fixture(game, fixture);
    const auto before = snapshot(game.world(), false);
    niteraid::WreckageProbeAccess::collide(game);
    const auto after = snapshot(game.world(), true);

    std::vector<std::size_t> clones;
    for (std::size_t index = 0; index < after.objects.size(); ++index) {
        const auto previous_type = index < before.objects.size() ? before.objects[index].type : ObjectType::None;
        const bool can_be_allocated_clone = previous_type == ObjectType::None ||
                                           previous_type == ObjectType::PlayerProjectile;
        if (after.objects[index].active && after.objects[index].type == ObjectType::EnemyDeath &&
            can_be_allocated_clone) {
            clones.push_back(index);
        }
    }
    const auto allocation = clones.size();
    const auto source_before = fixture.source < before.objects.size() ? &before.objects[fixture.source] : nullptr;
    const auto source_after = fixture.source < after.objects.size() ? &after.objects[fixture.source] : nullptr;
    out << "{\"name\":"; json_string(out, fixture.name);
    out << ",\"kind\":"; json_string(out, fixture.kind);
    out << ",\"roles\":[";
    for (std::size_t index = 0; index < fixture.roles.size(); ++index) {
        if (index != 0) out << ',';
        json_string(out, fixture.roles[index]);
    }
    out << "],\"source_slot\":" << fixture.source << ",\"target_slot\":" << fixture.target
        << ",\"source_first\":" << (fixture.source_first ? "true" : "false")
        << ",\"allocation_delta\":" << allocation
        << ",\"highwater_delta\":" << static_cast<long long>(after.objects.size()) -
        static_cast<long long>(before.objects.size())
        << ",\"clone_slots\":[";
    for (std::size_t index = 0; index < clones.size(); ++index) {
        if (index != 0) out << ',';
        out << clones[index];
    }
    out << "],\"score_delta\":" << after.score - before.score
        << ",\"enemy_kills_delta\":" << after.kills - before.kills
        << ",\"before\":";
    json_state(out, before);
    out << ",\"after\":";
    json_state(out, after);
    out << ",\"source_bbox_before\":";
    if (source_before != nullptr) json_object(out, *source_before, fixture.source);
    else out << "null";
    out << ",\"source_bbox_after\":";
    if (source_after != nullptr) json_object(out, *source_after, fixture.source);
    else out << "null";
    out << ",\"rng\":{\"available\":true,\"before\":" << before.rng
        << ",\"after\":" << after.rng << ",\"deterministic\":true}"
        << ",\"failures\":[]}";
}

void emit_lifetime(std::ostream& out)
{
    out << "[";
    const std::array<std::pair<const char*, AircraftVariant>, 4> variants {{
        {"A", AircraftVariant::A}, {"B", AircraftVariant::B},
        {"C", AircraftVariant::C}, {"D", AircraftVariant::D}}};
    for (std::size_t variant_index = 0; variant_index < variants.size(); ++variant_index) {
        if (variant_index != 0) out << ',';
        Game game(std::uint16_t {12});
        isolate(game);
        auto& world = game.diagnostic_world();
        Object shell = fragment({100.0f, 100.0f}, 19.0f, 17.0f);
        shell.aircraft_variant = variants[variant_index].second;
        shell.assault_stage = 0;
        shell.has_dropped_payload = false;
        world.objects.push_back(shell);
        std::uint32_t ticks = 0;
        const auto rng_before = world.gameplay_rng_seed;
        std::uint32_t last_rng = rng_before;
        while (ticks < 256 && std::any_of(world.objects.begin(), world.objects.end(),
                                           [](const Object& object) {
                                               return object.active && object.type == ObjectType::AircraftDebris;
                                           })) {
            game.tick(InputState {});
            ++ticks;
            last_rng = world.gameplay_rng_seed;
        }
        out << "{\"variant\":";
        json_string(out, variants[variant_index].first);
        out << ",\"ticks\":" << ticks << ",\"completed\":" << (ticks < 256 ? "true" : "false")
            << ",\"rng_before\":" << rng_before << ",\"rng_after\":" << last_rng
            << ",\"trace_available\":true}";
    }
    out << "]";
}

void emit_paired_lifetimes(std::ostream& out, bool matrix)
{
    struct LifetimeCase { int variant; int direction; std::uint32_t seed; };
    std::vector<LifetimeCase> cases;
    for (int variant = 0; variant < 4; ++variant) {
        for (int direction = 0; direction < (matrix ? 2 : 1); ++direction) {
            for (const auto seed : matrix ? std::vector<std::uint32_t>{0, 1, 0x12345678}
                                          : std::vector<std::uint32_t>{1}) {
                cases.push_back({variant, direction, seed});
            }
        }
    }
    out << "{\"coverage\":\"controlled_shell_update_lifetimes" << (matrix ? "_matrix" : "") << '"';
    if (matrix) {
        out << ",\"directions\":[0,1],\"seeds\":[0,1,305419896]";
    } else {
        out << ",\"seed\":1";
    }
    out << ",\"cases\":[";
    for (std::size_t index = 0; index < cases.size(); ++index) {
        if (index != 0) { out << ','; }
        const auto [variant, direction, seed] = cases[index];
        Game game(std::uint16_t {12});
        isolate(game);
        const auto family = static_cast<AircraftVariant>(variant);
        append_aircraft_pair(game, family, false);
        game.diagnostic_world().objects[1].direction = direction == 0 ? 1 : -1;
        niteraid::WreckageProbeAccess::collide(game);
        const auto shell = game.world().objects[1];
        auto& world = game.diagnostic_world();
        world.objects = {shell};
        world.object_highwater = world.objects.size();
        world.scores = {};
        world.gameplay_rng_seed = seed;
        niteraid::WreckageProbeAccess::reset_free_slot(game);
        out << "{\"variant\":" << variant;
        if (matrix) {
            out << ",\"direction\":" << direction << ",\"seed\":" << seed;
        }
        out << ",\"states\":[";
        json_state(out, snapshot(world, true));
        int updates = 0;
        while (updates < 256 && std::any_of(world.objects.begin(), world.objects.end(),
                                           [](const Object& object) { return object.active; })) {
            niteraid::WreckageProbeAccess::update(game);
            ++updates;
            out << ',';
            json_state(out, snapshot(world, true));
        }
        out << "],\"updates\":" << updates << ",\"complete\":"
            << (std::none_of(world.objects.begin(), world.objects.end(),
                            [](const Object& object) { return object.active; }) ? "true" : "false") << '}';
    }
    out << "]}\n";
}

Object retirement_particle()
{
    Object particle {};
    particle.active = true;
    particle.type = ObjectType::ResolutionParticle;
    particle.position = {80.0f, 40.0f};
    particle.extent = {0.5f, 0.5f};
    particle.timer = 3;
    return particle;
}

Object retirement_shell()
{
    auto shell = fragment({100.0f, 40.0f}, 69.0f, 17.0f, 0x121);
    shell.has_dropped_payload = false;
    shell.assault_stage = 9;
    shell.frame = 9;
    shell.timer = 10;
    return shell;
}

std::vector<Object> retirement_fixture(std::size_t index)
{
    const auto particle = retirement_particle();
    const auto shell = retirement_shell();
    switch (index) {
    case 0: return {particle};
    case 1: return {shell};
    case 2: return {particle, shell};
    case 3: return {shell, particle};
    default: break;
    }
    Object source {};
    source.active = true;
    source.type = ObjectType::GroundedTransition;
    source.position = {100.0f, index == 4 ? 174.5f : 175.0f};
    source.extent = {2.5f, 3.5f};
    source.parachute_lost = true;
    source.frame = 2;
    source.timer = 5;
    source.sprite_id = 0x111;
    Object neighbor {};
    neighbor.active = true;
    neighbor.type = ObjectType::LandedInvader;
    neighbor.position = {100.0f, 172.0f};
    neighbor.extent = {2.0f, 3.5f};
    neighbor.sprite_id = 0x0f8;
    neighbor.frame = 4;
    neighbor.timer = 0x11234;
    if (index == 6) {
        source.position.y = 164.0f;
        source.extent = {2.0f, 3.5f};
        source.parachute_lost = false;
        source.frame = 7;
        source.timer = 0;
        source.sprite_id = 0x0f7;
        neighbor.position.x = 103.0f;
        neighbor.frame = 3;
        neighbor.timer = 0x22345;
    }
    return {source, neighbor, shell};
}

void json_retirement_state(std::ostream& out, const Game& game,
                           CallbackFields callbacks = CallbackFields::LegacyMapping)
{
    const auto& world = game.world();
    const auto free_slot = niteraid::WreckageProbeAccess::free_slot(game);
    const auto landed = std::count_if(world.objects.begin(), world.objects.end(), [](const Object& actor) {
        return actor.active && !actor.pending_destroy && actor.type == ObjectType::LandedInvader;
    });
    out << "{\"rng_seed\":" << world.gameplay_rng_seed << ",\"score\":" << world.scores.score
        << ",\"enemy_kills\":" << world.scores.enemy_kills
        << ",\"grounded_resolutions\":" << world.scores.grounded_invader_resolutions
        << ",\"landed_invader_count\":" << landed << ",\"free_slot\":";
    if (free_slot) { out << *free_slot; } else { out << "null"; }
    out << ",\"object_highwater\":" << world.object_highwater.value_or(world.objects.size());
    out << ",\"objects\":[";
    for (std::size_t slot = 0; slot < world.objects.size(); ++slot) {
        if (slot != 0) { out << ','; }
        json_object(out, world.objects[slot], slot, callbacks);
    }
    out << "]}";
}

void emit_bomb_death_lifetimes(std::ostream& out)
{
    constexpr int kUpdateLimit = 32;
    static constexpr std::array kNames {"powered", "falling"};
    out << "{\"coverage\":\"controlled_bomb_death_lifetimes\","
           "\"execution_boundary\":\"update_only_31a0\",\"seed\":1,"
           "\"state_imported\":true,\"reseeded_between_updates\":false,"
           "\"max_updates\":" << kUpdateLimit << ",\"cases\":[";
    for (std::size_t index = 0; index < kNames.size(); ++index) {
        if (index != 0) { out << ','; }
        Game game(std::uint16_t {12});
        isolate(game);
        auto& world = game.diagnostic_world();
        world.gameplay_rng_seed = 1;
        auto death = enemy_death({100.0f, 40.0f}, 18.0f, 6.0f);
        death.timer = 0;
        death.has_dropped_payload = true;
        death.finale_drop = index == 0;
        world.objects = {death};
        world.object_highwater = world.objects.size();
        out << "{\"name\":";
        json_string(out, kNames[index]);
        out << ",\"states\":[";
        const auto emit = [&](int update) {
            out << "{\"update\":" << update << ",\"phase\":";
            json_string(out, update == 0 ? "initial" : "update");
            out << ",\"state\":";
            json_retirement_state(out, game, CallbackFields::Unavailable);
            out << '}';
        };
        emit(0);
        int updates = 0;
        while (updates < kUpdateLimit && world.objects.front().active) {
            ++updates;
            niteraid::WreckageProbeAccess::update(game);
            out << ',';
            emit(updates);
        }
        out << "],\"updates\":" << updates << ",\"complete\":"
            << (!world.objects.front().active ? "true" : "false") << '}';
    }
    out << "]}\n";
}

void emit_intermission_owner_chain(std::ostream& out, std::uint32_t seed, std::size_t survivors)
{
    using niteraid::MilestoneIntermission;
    static constexpr std::array<std::uint16_t, 2> kLevels {4, 8};
    static constexpr std::array kSurvivorX {60.0f, 240.0f};
    constexpr int kTickLimit = 10000;
    constexpr int kLandedSprite = 0x0f8;

    out << "{\"coverage\":\"controlled_native_intermission_owner_chain\","
           "\"scope\":\"controlled_fixture_actual_native_owner_and_ticks\","
           "\"state_imported_between_phases\":false,\"reseeded_between_phases\":false,"
           "\"original_clock_equivalence_claimed\":false,\"rgb_compared\":false,"
           "\"natural_campaign_validated\":false,\"original_raw_fields_available\":false,"
           "\"level_mapping\":\"native_initial_current_level_is_case_level_minus_one\","
           "\"bunker_mapping\":\"constructor_cannon_preserved_semantic_only_not_raw_coordinates_or_aim\","
           "\"tick_limit\":" << kTickLimit << ",\"survivors\":" << survivors << ",\"cases\":[";
    for (std::size_t index = 0; index < kLevels.size(); ++index) {
        if (index != 0) { out << ','; }
        const auto level = kLevels[index];
        const auto native_level = static_cast<std::uint16_t>(level - 1);
        const auto expected_milestone = level == 4 ? MilestoneIntermission::Level4Pizza
                                                  : MilestoneIntermission::Level8Helicopter;
        Game game(native_level);
        const auto cannon = game.world().objects.at(0);
        if (!cannon.active || cannon.type != ObjectType::PlayerCannon) {
            throw std::runtime_error("owner chain requires the production slot-zero cannon");
        }
        isolate(game);
        auto& world = game.diagnostic_world();
        world.objects.push_back(cannon);
        for (std::size_t survivor_index = 0; survivor_index < survivors; ++survivor_index) {
            Object survivor {};
            survivor.active = true;
            survivor.type = ObjectType::LandedInvader;
            survivor.position = {kSurvivorX[survivor_index], 172.0f};
            survivor.extent = {2.0f, 3.5f};
            survivor.sprite_id = kLandedSprite;
            world.objects.push_back(survivor);
        }
        world.gameplay_state = GameplayState::LevelComplete;
        world.gameplay_rng_seed = seed;

        int ticks = 0;
        bool first_state = true;
        out << "{\"level\":" << level << ",\"seed\":" << seed
            << ",\"survivors\":" << survivors << ",\"states\":[";
        const auto emit = [&](std::string_view phase) {
            if (!first_state) { out << ','; }
            first_state = false;
            out << "{\"phase\":";
            json_string(out, phase);
            out << ",\"tick\":" << ticks << ",\"frame_tick\":" << world.frame_tick
                << ",\"screen\":";
            json_string(out, niteraid::to_string(world.screen));
            out << ",\"gameplay_state\":";
            json_string(out, niteraid::to_string(world.gameplay_state));
            out << ",\"current_level\":" << world.current_level
                << ",\"finale_budget_seed\":" << world.finale_budget_seed
                << ",\"intermission_frames_remaining\":" << world.intermission_frames_remaining
                << ",\"milestone_intermission\":" << static_cast<int>(world.milestone_intermission)
                << ",\"milestone_intermission_frame\":" << world.milestone_intermission_frame
                << ",\"milestone_timer_origin\":" << world.milestone_timer_origin
                << ",\"survivor_intermission_frame\":" << world.survivor_intermission_frame
                << ",\"survivor_intermission_timer_origin\":" << world.survivor_intermission_timer_origin
                << ",\"survivor_intermission_rng_seed\":" << world.survivor_intermission_rng_seed
                << ",\"survivor_intermission_failure_mask\":" << world.survivor_intermission_failure_mask
                << ",\"flags\":{\"survivor_intermission_active\":" << (world.survivor_intermission_active ? "true" : "false")
                << ",\"survivor_native_presenter\":" << (world.survivor_native_presenter ? "true" : "false")
                << ",\"no_survivor_intermission_active\":" << (world.no_survivor_intermission_active ? "true" : "false")
                << ",\"transition_freeze\":" << (world.transition_freeze ? "true" : "false")
                << ",\"transition_armed\":" << (world.transition_armed ? "true" : "false")
                << ",\"bunker_assault_active\":" << (world.bunker_assault_active ? "true" : "false")
                << ",\"waves_exhausted\":" << (world.waves_exhausted ? "true" : "false")
                << ",\"player_dead\":" << (world.player_dead ? "true" : "false") << "},\"state\":";
            json_retirement_state(out, game, CallbackFields::Unavailable);
            out << '}';
        };
        emit("initial");
        // Only production owner calls and neutral ticks may mutate this fixture now.
        niteraid::WreckageProbeAccess::enter_intermission(game);
        const bool survivor_started = world.screen == Screen::Intermission &&
                                      world.survivor_intermission_active;
        if (survivor_started) {
            emit("survivor_started");
        }
        bool milestone_started = false;
        bool before_next_level_observed = false;
        bool next_level_started = false;
        while (ticks < kTickLimit && world.screen == Screen::Intermission) {
            if (!before_next_level_observed && world.milestone_intermission == expected_milestone &&
                world.intermission_frames_remaining == 1) {
                emit("before_next_level");
                before_next_level_observed = true;
            }
            const auto previous_screen = world.screen;
            const auto previous_milestone = world.milestone_intermission;
            game.tick(InputState {});
            game.prepare_gameplay_page();
            ++ticks;
            if (world.screen == Screen::Intermission && previous_milestone == MilestoneIntermission::None &&
                world.milestone_intermission == expected_milestone) {
                emit("milestone_started");
                milestone_started = true;
            }
            if (previous_screen == Screen::Intermission && world.screen == Screen::Gameplay &&
                world.current_level == level) {
                emit("next_level_started");
                next_level_started = true;
            }
        }
        const bool complete = survivor_started && milestone_started && next_level_started;
        out << "],\"ticks\":" << ticks << ",\"complete\":" << (complete ? "true" : "false")
            << ",\"before_next_level_observed\":" << (before_next_level_observed ? "true" : "false")
            << ",\"termination\":";
        json_string(out, complete ? "next_level_started" : ticks == kTickLimit ? "tick_limit"
                                                                             : "unexpected_owner_transition");
        out << '}';
    }
    out << "]}\n";
}

void emit_scripted_retirement(std::ostream& out, bool lifetime = false, bool posttail = false)
{
    struct ScriptedCase { const char* name; std::size_t landed; };
    static constexpr std::array cases {
        ScriptedCase {"cleanup-four", 4}, ScriptedCase {"cleanup-five", 5},
        ScriptedCase {"finale-egress", 0}};
    constexpr std::size_t kRetainedLanded = 3;
    constexpr std::uint32_t kSeed = 1;
    constexpr float kLandedStartX = 148.0f;
    constexpr int kLandedSprite = 0x0f8;
    constexpr int kCShellSprite = 0x15b; // 1805 initializer, PUSH at 1850.
    constexpr int kFinaleEgressSprite = 0x28e;
    constexpr int kLifetimeUpdateLimit = 256;
    constexpr int kPosttailAllocations = 24;

    out << "{\"coverage\":\"controlled_scripted_retirement"
        << (lifetime ? "_lifetime" : "") << "\",\"lifetime\":" << (lifetime ? "true" : "false")
        << ",\"posttail_allocations\":" << (posttail ? kPosttailAllocations : 0)
        << ",\"seed\":" << kSeed
        << ",\"scope\":\"native_cleanup_and_update_not_original_physical_sorting\","
           "\"original_raw_fields_available\":false,"
           "\"cleanup_target\":{\"storage\":\"spawn_bunker_resolution_particles_local\","
           "\"type_id\":2,\"vx\":0,\"vy\":0,\"object_slot\":null},"
           "\"original_target_mapping_requirement\":"
           "\"omit_only_reserved_target_199_after_validating_unchanged_all_phases_and_highwater_lt_199\","
           "\"cases\":[";
    for (std::size_t index = 0; index < cases.size(); ++index) {
        if (index != 0) { out << ','; }
        const auto& fixture = cases[index];
        Game game(std::uint16_t {12});
        isolate(game);
        auto& world = game.diagnostic_world();
        world.gameplay_rng_seed = kSeed;
        world.gameplay_state = GameplayState::ScriptedSequence;
        world.bunker_assault_active = true;
        world.bunker_assault_cleanup_pending = fixture.landed != 0;
        world.frame_tick = 2;
        world.bunker_assault_step_tick = world.frame_tick - 1;
        for (std::size_t slot = 0; slot < fixture.landed; ++slot) {
            Object landed {};
            landed.active = true;
            landed.type = ObjectType::LandedInvader;
            landed.position = {kLandedStartX + static_cast<float>(slot), 172.0f};
            landed.extent = {2.0f, 3.5f};
            landed.sprite_id = kLandedSprite;
            world.objects.push_back(landed);
        }
        if (fixture.landed == 0) {
            Object controller {};
            controller.active = true;
            controller.type = ObjectType::FinaleController;
            controller.position = {263.0f, 30.0f};
            controller.extent = {14.0f, 8.0f};
            controller.sprite_id = kFinaleEgressSprite;
            controller.assault_stage = 1;
            world.objects.push_back(controller);
        }
        auto shell = fragment({250.0f, 100.0f}, 44.0f, 13.0f, kCShellSprite);
        shell.aircraft_variant = AircraftVariant::C;
        shell.has_dropped_payload = false;
        shell.direction = 1;
        shell.frame = 5;
        // 1489: even old timer 6 becomes 7 (>5), then four children precede retirement.
        shell.timer = niteraid::internals::aircraft_debris_shell_delay(AircraftVariant::C);
        shell.assault_stage = static_cast<int>(shell.timer) - 1;
        world.objects.push_back(shell);
        world.objects.shrink_to_fit();

        out << "{\"name\":";
        json_string(out, fixture.name);
        out << ",\"execution_boundary\":";
        json_string(out, fixture.landed == 0 ? "native_finale_egress_update"
                                            : "native_cleanup_then_update");
        // Cleanup is inline in update_active_objects: no genuine intermediate return.
        out << ",\"missing_native_phases\":[";
        for (std::size_t slot = kRetainedLanded; slot < fixture.landed; ++slot) {
            if (slot != kRetainedLanded) { out << ','; }
            json_string(out, "cleanup_" + std::to_string(slot));
        }
        out << "],\"states\":[";
        const auto emit = [&](std::string_view phase) {
            out << "{\"phase\":";
            json_string(out, phase);
            out << ",\"state\":";
            json_retirement_state(out, game, CallbackFields::Unavailable);
            out << '}';
        };
        emit("initial");
        // Run the real owner cleanup and live-high-water scan, without a full tick.
        niteraid::WreckageProbeAccess::update(game);
        out << ',';
        emit("update");
        if (lifetime) {
            // Subsequent 31a0 calls do not reenter 4cfb's assault owner.
            world.bunker_assault_active = false;
            world.gameplay_state = GameplayState::LevelComplete;
            const auto effects_active = [&world]() {
                return std::any_of(world.objects.begin(), world.objects.end(), [](const Object& actor) {
                    return actor.active && !actor.pending_destroy &&
                           (actor.type == ObjectType::AircraftDebris || actor.type == ObjectType::ResolutionParticle);
                });
            };
            int updates = 1;
            while (effects_active() && updates < kLifetimeUpdateLimit) {
                niteraid::WreckageProbeAccess::update(game);
                ++updates;
                out << ',';
                emit("update_" + std::to_string(updates));
            }
            out << "],\"updates\":" << updates << ",\"termination\":";
            json_string(out, effects_active() ? "update_limit" : "effects_retired");
            if (posttail && !effects_active()) {
                out << ",\"posttail\":{\"slots\":[";
                for (int allocation = 0; allocation < kPosttailAllocations; ++allocation) {
                    std::vector<bool> occupied;
                    occupied.reserve(world.objects.size());
                    for (const auto& actor : world.objects) {
                        occupied.push_back(actor.active);
                    }
                    Object filler {};
                    filler.active = true;
                    filler.type = ObjectType::PlayerCannon;
                    filler.position = {148.0f, 0.0f};
                    filler.extent = {};
                    filler.timer = 1;
                    niteraid::WreckageProbeAccess::store(game, std::move(filler));
                    std::size_t slot = world.objects.size();
                    for (std::size_t index = 0; index < world.objects.size(); ++index) {
                        if (world.objects[index].active && (index >= occupied.size() || !occupied[index])) {
                            slot = index;
                            break;
                        }
                    }
                    if (slot == world.objects.size()) {
                        throw std::runtime_error("posttail allocator did not activate a new slot");
                    }
                    if (allocation != 0) { out << ','; }
                    out << slot;
                }
                out << "],\"state\":";
                json_retirement_state(out, game, CallbackFields::Unavailable);
                out << '}';
            }
            out << '}';
        } else {
            out << "]}";
        }
    }
    out << "]}\n";
}

void emit_fragment_contacts(std::ostream& out)
{
    struct ContactCase { const char* name; AircraftVariant variant; float target_x; };
    static constexpr std::array cases {
        ContactCase {"d-ground-contact", AircraftVariant::D, 125.0f},
        ContactCase {"d-ground-negative", AircraftVariant::D, 300.0f},
        ContactCase {"a-ground-contact", AircraftVariant::A, 91.0f},
        ContactCase {"a-ground-negative", AircraftVariant::A, 300.0f}};
    constexpr int kUpdateLimit = 256;
    out << "{\"coverage\":\"controlled_fragment_contacts\",\"seed\":1,\"cases\":[";
    for (std::size_t index = 0; index < cases.size(); ++index) {
        if (index != 0) { out << ','; }
        const auto& fixture = cases[index];
        Game game(std::uint16_t {12});
        isolate(game);
        append_aircraft_pair(game, fixture.variant, false);
        niteraid::WreckageProbeAccess::collide(game);
        const auto shell = game.world().objects[1];
        Object target {};
        target.active = true;
        target.type = ObjectType::LandedInvader;
        target.position = {fixture.target_x, 172.0f};
        target.extent = {2.0f, 3.5f};
        target.sprite_id = 0x0f8;
        auto& world = game.diagnostic_world();
        world.objects = {shell, target};
        world.object_highwater = world.objects.size();
        world.scores = {};
        world.gameplay_rng_seed = 1;
        niteraid::WreckageProbeAccess::reset_free_slot(game);
        out << "{\"name\":";
        json_string(out, fixture.name);
        out << ",\"variant\":" << static_cast<int>(fixture.variant)
            << ",\"target_x\":" << fixture.target_x << ",\"states\":[";
        const auto emit = [&](int update, const char* phase) {
            out << "{\"update\":" << update << ",\"phase\":";
            json_string(out, phase);
            out << ",\"state\":";
            json_retirement_state(out, game);
            out << '}';
        };
        const auto effects_active = [&]() {
            return std::any_of(world.objects.begin(), world.objects.end(), [](const Object& actor) {
                return actor.active && (actor.type == ObjectType::AircraftDebris ||
                                        actor.type == ObjectType::ResolutionParticle);
            });
        };
        emit(0, "initial");
        int updates = 0;
        while (updates < kUpdateLimit && effects_active()) {
            ++updates;
            niteraid::WreckageProbeAccess::update(game);
            out << ',';
            emit(updates, "update");
            niteraid::WreckageProbeAccess::collide(game);
            out << ',';
            emit(updates, "collision");
        }
        out << "],\"updates\":" << updates << ",\"complete\":" << (!effects_active() ? "true" : "false") << '}';
    }
    out << "]}\n";
}

void emit_mixed_allocations(std::ostream& out, Game& game, int count)
{
    auto& world = game.diagnostic_world();
    out << "{\"slots\":[";
    for (int allocation = 0; allocation < count; ++allocation) {
        std::vector<bool> occupied;
        occupied.reserve(world.objects.size());
        for (const auto& actor : world.objects) {
            occupied.push_back(actor.active);
        }
        Object filler {};
        filler.active = true;
        filler.type = ObjectType::PlayerCannon;
        filler.position = {148.0f, 0.0f};
        filler.extent = {};
        filler.timer = 1;
        niteraid::WreckageProbeAccess::store(game, std::move(filler));
        std::size_t slot = world.objects.size();
        for (std::size_t index = 0; index < world.objects.size(); ++index) {
            if (world.objects[index].active && (index >= occupied.size() || !occupied[index])) {
                slot = index;
                break;
            }
        }
        if (slot == world.objects.size()) {
            throw std::runtime_error("mixed allocator did not activate a new slot");
        }
        if (allocation != 0) { out << ','; }
        out << slot;
    }
    out << "],\"state\":";
    json_retirement_state(out, game, CallbackFields::Unavailable);
    out << '}';
}

void emit_mixed_chain_lifecycle(std::ostream& out, bool posttail = false)
{
    constexpr std::string_view kCaseName = "mixed-chain-projectile-aircraft-aircraft";
    constexpr int kUpdateLimit = 256;
    constexpr int kPosttailAllocations = 24;
    const auto available = fixtures();
    const auto fixture = std::find_if(available.begin(), available.end(), [](const Fixture& item) {
        return item.name == kCaseName;
    });
    if (fixture == available.end()) {
        throw std::runtime_error("mixed chain fixture missing");
    }
    Game game(std::uint16_t {12});
    isolate(game);
    game.diagnostic_world().gameplay_rng_seed = 1;
    configure_fixture(game, *fixture);
    auto& world = game.diagnostic_world();
    for (auto& object : world.objects) {
        object.timer = 1;
    }
    out << "{\"coverage\":\"controlled_mixed_chain_lifecycle\",\"seed\":1,"
           "\"posttail_allocations\":" << (posttail ? kPosttailAllocations : 0)
        << ",\"cases\":[{\"name\":";
    json_string(out, kCaseName);
    out << ",\"fixture\":";
    json_retirement_state(out, game);
    niteraid::WreckageProbeAccess::collide(game);
    out << ",\"states\":[";
    const auto emit = [&](int update, std::string_view phase) {
        out << "{\"update\":" << update << ",\"phase\":";
        json_string(out, phase);
        out << ",\"state\":";
        json_retirement_state(out, game);
        out << '}';
    };
    const auto effects_active = [&world]() {
        return std::any_of(world.objects.begin(), world.objects.end(), [](const Object& actor) {
            return actor.active && (actor.type == ObjectType::EnemyDeath ||
                                    actor.type == ObjectType::AircraftDebris ||
                                    actor.type == ObjectType::ResolutionParticle);
        });
    };
    emit(0, "initial");
    int updates = 0;
    while (updates < kUpdateLimit && effects_active()) {
        ++updates;
        niteraid::WreckageProbeAccess::update(game);
        out << ',';
        emit(updates, "update");
        niteraid::WreckageProbeAccess::collide(game);
        out << ',';
        emit(updates, "collision");
    }
    out << "],\"updates\":" << updates << ",\"complete\":" << (!effects_active() ? "true" : "false");
    if (posttail && !effects_active()) {
        out << ",\"posttail\":";
        emit_mixed_allocations(out, game, kPosttailAllocations);
    }
    out << "}]}\n";
}

void emit_mixed_chain_midalloc(std::ostream& out)
{
    constexpr std::string_view kCaseName = "mixed-chain-projectile-aircraft-aircraft";
    constexpr int kBoundaryUpdate = 63;
    constexpr int kAllocations = 24;
    const auto available = fixtures();
    const auto fixture = std::find_if(available.begin(), available.end(), [](const Fixture& item) {
        return item.name == kCaseName;
    });
    if (fixture == available.end()) {
        throw std::runtime_error("mixed chain fixture missing");
    }
    Game game(std::uint16_t {12});
    isolate(game);
    auto& world = game.diagnostic_world();
    world.gameplay_rng_seed = 1;
    configure_fixture(game, *fixture);
    for (auto& object : world.objects) {
        object.timer = 1;
    }
    niteraid::WreckageProbeAccess::collide(game);
    for (int update = 0; update < kBoundaryUpdate; ++update) {
        niteraid::WreckageProbeAccess::update(game);
        niteraid::WreckageProbeAccess::collide(game);
    }
    if (!std::any_of(world.objects.begin(), world.objects.end(), [](const Object& actor) {
            return actor.active && (actor.type == ObjectType::EnemyDeath ||
                                    actor.type == ObjectType::AircraftDebris ||
                                    actor.type == ObjectType::ResolutionParticle);
        })) {
        throw std::runtime_error("mixed midchain boundary has no live effects");
    }
    out << "{\"coverage\":\"controlled_mixed_chain_midalloc\",\"seed\":1,"
           "\"update\":" << kBoundaryUpdate << ",\"allocations\":" << kAllocations
        << ",\"case\":{\"name\":";
    json_string(out, kCaseName);
    out << ",\"before\":";
    json_retirement_state(out, game, CallbackFields::Unavailable);
    out << ",\"allocation\":";
    emit_mixed_allocations(out, game, kAllocations);
    out << "}}\n";
}

void emit_update_retirement(std::ostream& out)
{
    static constexpr std::array names {
        "particle-zero-lifetime-retires", "shell-factory-before-retire-and-child-update",
        "expired-earlier-slot-reused-without-second-update", "shell-before-expiring-particle",
        "grounded-y174.5-no-resolution", "grounded-y175-resolution-before-shell",
        "landed-merge-before-later-shell"};
    out << "{\"coverage\":\"controlled_update_retirement\",\"execution_boundary\":\"update_only_31a0\","
           "\"seed\":1,\"cases\":[";
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0) { out << ','; }
        Game game(std::uint16_t {12});
        isolate(game);
        auto& world = game.diagnostic_world();
        world.gameplay_rng_seed = 1;
        world.objects = retirement_fixture(index);
        out << "{\"name\":";
        json_string(out, names[index]);
        out << ",\"before\":";
        json_retirement_state(out, game);
        niteraid::WreckageProbeAccess::update(game);
        out << ",\"after\":";
        json_retirement_state(out, game);
        out << '}';
    }
    out << "]}\n";
}

void emit_landing_edges(std::ostream& out)
{
    struct EdgeCase { const char* name; float source_x; };
    static constexpr std::array cases {
        EdgeCase {"landing-edge-left-inside", -3.0f},
        EdgeCase {"landing-edge-left-outside", -4.0f},
        EdgeCase {"landing-edge-right-inside", 312.0f},
        EdgeCase {"landing-edge-right-outside", 313.0f},
    };
    out << "{\"coverage\":\"controlled_landing_conversion_edges\",\"seed\":1,\"cases\":[";
    for (std::size_t index = 0; index < cases.size(); ++index) {
        if (index != 0) { out << ','; }
        Game game(std::uint16_t {12});
        isolate(game);
        auto& world = game.diagnostic_world();
        world.gameplay_rng_seed = 1;
        Object source {};
        source.active = true;
        source.type = ObjectType::GroundedTransition;
        source.position = {cases[index].source_x, 164.0f};
        source.extent = {2.0f, 3.5f};
        source.frame = 7;
        source.timer = 0;
        source.sprite_id = 0x0f7;
        world.objects.push_back(source);
        out << "{\"name\":";
        json_string(out, cases[index].name);
        out << ",\"before\":";
        json_retirement_state(out, game);
        niteraid::WreckageProbeAccess::update(game);
        out << ",\"after\":";
        json_retirement_state(out, game);
        out << '}';
    }
    out << "]}\n";
}

void emit_free_fall(std::ostream& out)
{
    struct FallCase { const char* name; float x; float vx; std::uint32_t timer; };
    static constexpr std::array cases {
        FallCase {"free-fall-left-timer-4", 90.0f, -0.375f, 4},
        FallCase {"free-fall-left-timer-5", 90.0f, -0.375f, 5},
        FallCase {"free-fall-right-timer-4", 210.0f, 0.375f, 4},
        FallCase {"free-fall-right-timer-5", 210.0f, 0.375f, 5},
    };
    out << "{\"coverage\":\"controlled_chute_lost_free_fall\",\"seed\":1,\"cases\":[";
    for (std::size_t index = 0; index < cases.size(); ++index) {
        if (index != 0) { out << ','; }
        Game game(std::uint16_t {12});
        isolate(game);
        auto& world = game.diagnostic_world();
        world.gameplay_rng_seed = 1;
        Object source {};
        source.active = true;
        source.type = ObjectType::GroundedTransition;
        source.parachute_lost = true;
        source.position = {cases[index].x, 100.0f};
        source.velocity = {cases[index].vx, 1.5f};
        source.extent = {2.5f, 3.5f};
        source.frame = 2;
        source.timer = cases[index].timer;
        source.sprite_id = 0x111;
        world.objects.push_back(source);
        out << "{\"name\":";
        json_string(out, cases[index].name);
        out << ",\"before\":";
        json_retirement_state(out, game);
        niteraid::WreckageProbeAccess::update(game);
        out << ",\"after\":";
        json_retirement_state(out, game);
        out << '}';
    }
    out << "]}\n";
}

Object deployed_trooper(float drift)
{
    Object trooper {};
    trooper.active = true;
    trooper.type = ObjectType::Paratrooper;
    trooper.position = {100.0f, 70.0f};
    trooper.extent = {5.5f, 8.0f};
    trooper.timer = 1;
    trooper.velocity.x = drift;
    return trooper;
}

void emit_trooper_hits(std::ostream& out, bool drift = false)
{
    struct HitCase { const char* name; float shot_y; bool target_first; };
    static constexpr std::array cases {
        HitCase {"trooper-chute-hit-source-first", 80.5f, false},
        HitCase {"trooper-chute-hit-target-first", 80.5f, true},
        HitCase {"trooper-body-hit-source-first", 81.0f, false},
        HitCase {"trooper-body-hit-target-first", 81.0f, true},
    };
    out << "{\"coverage\":\"controlled_trooper_projectile_hits" << (drift ? "_drift" : "")
        << "\",\"seed\":1,\"cases\":[";
    for (std::size_t index = 0; index < cases.size() * (drift ? 2 : 1); ++index) {
        if (index != 0) { out << ','; }
        const auto& fixture = cases[index % cases.size()];
        const auto name = std::string(fixture.name) +
                          (drift ? (index < cases.size() ? "-left" : "-right") : "");
        Game game(std::uint16_t {12});
        isolate(game);
        auto& world = game.diagnostic_world();
        world.gameplay_rng_seed = 1;
        const auto trooper = deployed_trooper(drift ? (index < cases.size() ? -0.375f : 0.375f)
                                                   : 0.0f);
        Object shot {};
        shot.active = true;
        shot.type = ObjectType::PlayerProjectile;
        shot.position = {103.0f, fixture.shot_y};
        shot.extent = {1.5f, 1.5f};
        shot.timer = 1;
        if (fixture.target_first) {
            world.objects = {trooper, shot};
        } else {
            world.objects = {shot, trooper};
        }
        out << "{\"name\":";
        json_string(out, name);
        out << ",\"before\":";
        json_retirement_state(out, game);
        niteraid::WreckageProbeAccess::collide(game);
        out << ",\"after\":";
        json_retirement_state(out, game);
        out << '}';
    }
    out << "]}\n";
}

void emit_overrun_handoffs(std::ostream& out)
{
    constexpr std::uint32_t kPrelevelSeed = 4288041665u;
    constexpr std::uint32_t kUpdateLimit = 3200;
    Game game(std::uint16_t {0}, std::nullopt, false, false, false, false, kPrelevelSeed);
    out << "{\"coverage\":\"native_overrun_doorway_phases\","
           "\"state_imported_between_phases\":false,\"render_projection_explicit\":true,"
           "\"prelevel_seed\":" << kPrelevelSeed << ",\"cases\":[";
    int count = 0;
    while (game.world().frame_tick < kUpdateLimit && count < 3) {
        game.tick(InputState {});
        if (!game.before_owner_page() || !game.world().bunker_assault_direct_draw) {
            continue;
        }
        const auto& before = *game.before_owner_page();
        const auto found = std::find_if(before.objects.begin(), before.objects.end(), [](const auto& actor) {
            return actor.active && !actor.pending_destroy && actor.type == ObjectType::LandedInvader &&
                   actor.assaulting && (actor.assault_stage == 0 || actor.assault_stage == 3);
        });
        if (found == before.objects.end()) {
            throw std::runtime_error("doorway handoff lacks its approach actor");
        }
        const auto slot = static_cast<std::size_t>(found - before.objects.begin());
        const auto emit = [&](const char* phase, const WorldState& world, bool direct_draw) {
            const auto& actor = world.objects.at(slot);
            out << "{\"phase\":";
            json_string(out, phase);
            out << ",\"tick\":" << world.frame_tick << ",\"score\":" << world.scores.score
                << ",\"rng_seed\":" << world.gameplay_rng_seed
                << ",\"direct_draw\":" << (direct_draw ? "true" : "false")
                << ",\"draw_y\":" << actor.position.y + (direct_draw ? 1 : 0)
                << ",\"actor\":";
            json_object(out, actor, slot, CallbackFields::Unavailable);
            out << '}';
        };
        if (count != 0) { out << ','; }
        out << "{\"states\":[";
        emit("last_approach_draw", before, false);
        out << ',';
        emit("first_doorway_draw", game.world(), true);
        game.tick(InputState {});
        out << ',';
        emit("next_walk_update", game.world(), game.world().bunker_assault_direct_draw);
        out << "]}";
        ++count;
    }
    out << "],\"complete\":" << (count == 3 ? "true" : "false") << "}\n";
}

void emit_natural_particle_birth(std::ostream& out)
{
    constexpr std::uint32_t kPrelevelSeed = 4288041665u;
    constexpr std::uint32_t kUpdateLimit = 900;
    Game game(std::uint16_t {0}, std::nullopt, false, false, false, false, kPrelevelSeed);
    out << "{\"coverage\":\"native_natural_particle_birth\","
           "\"state_imported_between_updates\":false,\"prelevel_seed\":" << kPrelevelSeed
        << ",\"states\":[";
    bool first_state = true;
    while (game.world().frame_tick < kUpdateLimit) {
        game.tick(InputState {});
        const auto& world = game.world();
        if (!first_state) { out << ','; }
        first_state = false;
        out << "{\"tick\":" << world.frame_tick << ",\"score\":" << world.scores.score
            << ",\"grounded_resolutions\":" << world.scores.grounded_invader_resolutions
            << ",\"rng_seed\":" << world.gameplay_rng_seed << ",\"particles\":[";
        bool first_particle = true;
        for (std::size_t slot = 0; slot < world.objects.size(); ++slot) {
            const auto& actor = world.objects[slot];
            if (!actor.active || actor.pending_destroy || actor.type != ObjectType::ResolutionParticle) {
                continue;
            }
            if (!first_particle) { out << ','; }
            first_particle = false;
            json_object(out, actor, slot, CallbackFields::Unavailable);
        }
        out << "]}";
    }
    out << "],\"complete\":true}\n";
}

void emit_trooper_lifecycle(std::ostream& out)
{
    struct LifetimeCase { const char* name; float drift; bool target_first; };
    static constexpr std::array cases {
        LifetimeCase {"trooper-chute-hit-source-first-left", -0.375f, false},
        LifetimeCase {"trooper-chute-hit-target-first-left", -0.375f, true},
        LifetimeCase {"trooper-chute-hit-source-first-right", 0.375f, false},
        LifetimeCase {"trooper-chute-hit-target-first-right", 0.375f, true},
    };
    constexpr int kUpdateLimit = 256;
    out << "{\"coverage\":\"controlled_trooper_hit_lifetime\","
           "\"execution_boundary\":\"collision_31d7_then_update_31a0_collision_31d7\","
           "\"seed\":1,\"max_updates\":" << kUpdateLimit
        << ",\"reseeded_between_phases\":false,\"cases\":[";
    for (std::size_t index = 0; index < cases.size(); ++index) {
        if (index != 0) { out << ','; }
        Game game(std::uint16_t {12});
        isolate(game);
        auto& world = game.diagnostic_world();
        world.gameplay_rng_seed = 1;
        const auto trooper = deployed_trooper(cases[index].drift);
        auto shot = projectile({103.0f, 80.5f});
        shot.timer = 1;
        if (cases[index].target_first) {
            world.objects = {trooper, shot};
        } else {
            world.objects = {shot, trooper};
        }
        out << "{\"name\":";
        json_string(out, cases[index].name);
        out << ",\"states\":[";
        const auto emit = [&](int update, const char* phase) {
            out << "{\"update\":" << update << ",\"phase\":";
            json_string(out, phase);
            out << ",\"state\":";
            json_retirement_state(out, game, CallbackFields::Unavailable);
            out << '}';
        };
        emit(0, "initial");
        niteraid::WreckageProbeAccess::collide(game);
        out << ',';
        emit(0, "collision");
        const auto active = [&]() {
            return std::any_of(world.objects.begin(), world.objects.end(),
                               [](const auto& actor) { return actor.active; });
        };
        int updates = 0;
        while (updates < kUpdateLimit && active()) {
            ++updates;
            niteraid::WreckageProbeAccess::update(game);
            out << ',';
            emit(updates, "update");
            niteraid::WreckageProbeAccess::collide(game);
            out << ',';
            emit(updates, "collision");
        }
        out << "],\"updates\":" << updates << ",\"complete\":"
            << (!active() ? "true" : "false") << '}';
    }
    out << "]}\n";
}

}  // namespace

int main(int argc, char** argv)
{
    std::cout << std::setprecision(17);
    if (argc == 2 && std::string_view(argv[1]) == "--bomb-death-lifetimes") {
        emit_bomb_death_lifetimes(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--natural-particle-birth") {
        emit_natural_particle_birth(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--overrun-doorway-handoffs") {
        emit_overrun_handoffs(std::cout);
        return 0;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--intermission-owner-chain") {
        if (argc != 2 && argc != 3 && argc != 5) {
            std::cerr << "owner chain accepts a seed and optional --survivors 1|2\n";
            return 2;
        }
        std::uint32_t seed = 1;
        if (argc >= 3) {
            const std::string_view value(argv[2]);
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), seed);
            if (error != std::errc {} || end != value.data() + value.size()) {
                std::cerr << "owner chain seed must be an unsigned DWORD\n";
                return 2;
            }
        }
        std::size_t survivors = 2;
        if (argc == 5) {
            const std::string_view count(argv[4]);
            if (std::string_view(argv[3]) != "--survivors" || (count != "1" && count != "2")) {
                std::cerr << "owner chain survivors must be 1 or 2\n";
                return 2;
            }
            survivors = count == "1" ? 1 : 2;
        }
        emit_intermission_owner_chain(std::cout, seed, survivors);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--scripted-retirement") {
        emit_scripted_retirement(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--scripted-retirement-lifetime") {
        emit_scripted_retirement(std::cout, true);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--scripted-retirement-posttail") {
        emit_scripted_retirement(std::cout, true, true);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--fragment-contacts") {
        emit_fragment_contacts(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--mixed-chain-lifecycle") {
        emit_mixed_chain_lifecycle(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--mixed-chain-posttail") {
        emit_mixed_chain_lifecycle(std::cout, true);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--mixed-chain-midalloc") {
        emit_mixed_chain_midalloc(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--update-retirement") {
        emit_update_retirement(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--landing-edge") {
        emit_landing_edges(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--free-fall") {
        emit_free_fall(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--trooper-hit") {
        emit_trooper_hits(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--trooper-drift-hit") {
        emit_trooper_hits(std::cout, true);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--trooper-lifecycle") {
        emit_trooper_lifecycle(std::cout);
        return 0;
    }
    if ((argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--matrix")) &&
        std::string_view(argv[1]) == "--fragment-lifetimes") {
        emit_paired_lifetimes(std::cout, argc == 3);
        return 0;
    }
    const auto cases = fixtures();
    std::cout << "{\"schema\":\"niteraid.current_wreckage_actor_state.v1\","
                 "\"execution_boundary\":\"collision_only_31d7\","
                 "\"coverage\":\"current_faithful_semantics_20_case_wreckage\","
                 "\"runtime_success\":true,\"cases\":[";
    for (std::size_t index = 0; index < cases.size(); ++index) {
        if (index != 0) std::cout << ',';
        emit_case(std::cout, cases[index]);
    }
    std::cout << "],\"fragment_lifetime\":{\"coverage\":\"current_only_full_spawn_to_expiry\","
                 "\"variants\":";
    emit_lifetime(std::cout);
    std::cout << ",\"original_pairing_available\":false,"
                 "\"missing_original\":[\"per-frame fragment lifetime traces for aircraft A/B/C/D\","
                 "\"original RNG words across complete fragment lifetimes\"]},"
                 "\"rng\":{\"available\":true,\"deterministic\":true}}\n";
    return 0;
}
