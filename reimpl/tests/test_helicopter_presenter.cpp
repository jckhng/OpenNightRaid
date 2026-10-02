#include "niteraid/helicopter_presenter.hpp"
#include "test_harness.hpp"

TEST_CASE("helicopter rotor uses the shared simulation clock")
{
    niteraid::WorldState world {};
    world.milestone_timer_origin = 267;
    const auto frame = niteraid::original_helicopter_presenter_frame(world);
    CHECK(frame.actors[0].x_fixed == 320 * 65536 - 102400);
    CHECK(frame.actors[0].counter == 79);
    CHECK(frame.draws[1].sprite == 0x2e2 + 268 % 29);
    CHECK(frame.draws[0].x == 318);
}

TEST_CASE("helicopter wait callback survives both outhouse door loops")
{
    niteraid::WorldState world {};
    world.milestone_intermission_frame = 1052;
    auto frame = niteraid::original_helicopter_presenter_frame(world);
    CHECK(frame.actors.back().timer == 0xfffffffe);
    CHECK(frame.actors.back().update_callback == 0x558c);
    world.milestone_intermission_frame = 1081;
    frame = niteraid::original_helicopter_presenter_frame(world);
    CHECK(frame.actors.back().timer == 0xffffffe1);
    world.milestone_intermission_frame = 1082;
    frame = niteraid::original_helicopter_presenter_frame(world);
    CHECK(frame.actors.back().x_fixed == 205 * 65536);
    CHECK(frame.actors.back().timer == 0);
    CHECK(frame.actors.back().sprite == 0x2d3);
    CHECK(frame.actors.back().animation == 52);
}

TEST_CASE("helicopter doorway redraw and inclusive departure retain the original order")
{
    niteraid::WorldState world {};
    world.milestone_intermission_frame = 1450;
    auto frame = niteraid::original_helicopter_presenter_frame(world);
    CHECK(frame.actors.back().y_fixed == 173 * 65536);
    CHECK(frame.draws.back().sprite == 0x1df);
    world.milestone_intermission_frame = 1513;
    frame = niteraid::original_helicopter_presenter_frame(world);
    CHECK(frame.actors.size() == 2);
    CHECK(frame.start_vehicle_sound);
    world.milestone_intermission_frame = 1674;
    frame = niteraid::original_helicopter_presenter_frame(world);
    CHECK(frame.active);
    CHECK(frame.actors[0].counter == 0xffff);
    CHECK(frame.actors[0].x_fixed < -49 * 65536);
    world.milestone_intermission_frame = 1675;
    CHECK(!niteraid::original_helicopter_presenter_frame(world).active);
}
