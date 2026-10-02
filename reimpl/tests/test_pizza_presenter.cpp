#include "niteraid/pizza_presenter.hpp"
#include "test_harness.hpp"

TEST_CASE("pizza retains its walk animation and unsigned wait residue")
{
    niteraid::WorldState world {};
    world.pizza_all_draws_diagnostic = true;
    world.milestone_intermission_frame = 869;
    auto frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.actors[1].animation == 50);
    CHECK(frame.actors[1].timer == 0xffffffff);
    world.milestone_intermission_frame = 884;
    frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.actors[2].sprite == 0x1e4);
    CHECK(frame.actors[2].draw_callback == 0x543a);
    world.milestone_intermission_frame = 885;
    frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.simulation_updates == 885);
    CHECK(frame.actors[1].y_fixed == 174 * 65536);
    world.milestone_intermission_frame = 886;
    frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.simulation_updates == 886);
    CHECK(frame.actors[1].y_fixed == 173 * 65536);
    CHECK(frame.actors[1].timer == 0);
    world.milestone_intermission_frame = 934;
    frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.actors[1].x_fixed == 155 * 65536);
    CHECK(frame.actors[1].animation == 56);
    CHECK(frame.actors[1].vx_fixed == 0);
    CHECK(frame.draws[2].sprite == 0x1df);
}

TEST_CASE("pizza live clock consumes zero-update draws without adding simulation ticks")
{
    niteraid::WorldState world {};
    world.milestone_intermission_frame = 884;
    auto frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.simulation_updates == 885);
    CHECK(frame.draw_index == 885);
    CHECK(frame.actors[1].y_fixed == 174 * 65536);
    world.milestone_intermission_frame = 1222;
    frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.simulation_updates == 1223);
    CHECK(frame.draw_index == 1224);
    CHECK(frame.actors[1].y_fixed == 174 * 65536);
    world.milestone_intermission_frame = 1865;
    frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.active);
    CHECK(frame.draw_index == 1867);
    world.milestone_intermission_frame = 1866;
    CHECK(!niteraid::original_pizza_presenter_frame(world).active);
}

TEST_CASE("pizza door closing removes the original special actor while trooper stays occluded")
{
    niteraid::WorldState world {};
    world.pizza_all_draws_diagnostic = true;
    world.milestone_intermission_frame = 935;
    auto frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.sounds.size() == 1);
    CHECK(frame.sounds[0] == 0x339);
    CHECK(frame.actors[2].draw_callback == 0x3f48);
    world.milestone_intermission_frame = 949;
    frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.actors[2].draw_callback == 0);
    world.milestone_intermission_frame = 950;
    frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.actors[2].draw_callback == 0);
    CHECK(frame.actors[2].timer == 209);
    CHECK(frame.draws[1].sprite == 0x1df);
    world.milestone_intermission_frame = 1161;
    frame = niteraid::original_pizza_presenter_frame(world);
    CHECK(frame.actors[2].draw_callback == 0x3e52);
    CHECK(frame.sounds[0] == 0x337);
}
