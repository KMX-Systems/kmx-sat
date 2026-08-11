#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/solver_state_machine.hpp>

namespace kmx::sat
{
    TEST_CASE("solver state machine enforces lifecycle transitions", "[sat]")
    {
        solver_state_machine machine;

        REQUIRE(machine.current_state() == solver_state_machine::state::configuring);
        REQUIRE(machine.expect_configuring());

        machine.transition_to_adding();
        REQUIRE(machine.expect_adding());

        machine.transition_to_solving();
        REQUIRE(machine.expect_solving());

        machine.transition_to_sat();
        REQUIRE(machine.current_state() == solver_state_machine::state::sat);

        machine.transition_to_error();
        REQUIRE(machine.current_state() == solver_state_machine::state::error);
        REQUIRE(machine.expect_error());
    }
}
