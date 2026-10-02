#include "niteraid/input_schedule.hpp"
#include "test_harness.hpp"

#include <sstream>
#include <stdexcept>

using niteraid::InputSchedule;

TEST_CASE("natural input schedule retains keys across update boundaries")
{
    std::istringstream text(
        "niteraid-input-schedule-v1 6\n"
        "1 left down\n"
        "3 spc down\n"
        "3 left up\n"
        "4 spc up\n");
    auto schedule = InputSchedule::parse(text);
    CHECK_EQ(schedule.count(), 6u);
    schedule.advance(0);
    CHECK(!schedule.apply({}).move_left);
    schedule.advance(1);
    CHECK(schedule.apply({}).move_left);
    schedule.advance(2);
    CHECK(schedule.apply({}).move_left);
    schedule.advance(3);
    CHECK(!schedule.apply({}).move_left);
    CHECK(schedule.apply({}).fire);
    schedule.advance(4);
    CHECK(!schedule.apply({}).fire);
    schedule.advance(5);
    CHECK(!schedule.apply({}).fire);
}

TEST_CASE("natural input schedule rejects malformed and repeated edges")
{
    for (const char* text : {
             "niteraid-input-schedule-v1 3\n1 left up\n",
             "niteraid-input-schedule-v1 3\n1 left down\n2 left down\n",
             "niteraid-input-schedule-v1 3\n3 left down\n",
             "niteraid-input-schedule-v1 3\n1 warp down\n",
             "niteraid-input-schedule-v1 3\n2 left down\n1 left up\n",
             "niteraid-input-schedule-v1 0\n",
         }) {
        std::istringstream input(text);
        bool rejected = false;
        try {
            InputSchedule::parse(input);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        CHECK(rejected);
    }
}
