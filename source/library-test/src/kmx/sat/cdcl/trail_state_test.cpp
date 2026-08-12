#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/stack/decision_frame.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/store/assumption.hpp>
#include <kmx/sat/cdcl/store/phase.hpp>
#include <kmx/sat/cdcl/trail.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{

    TEST_CASE("trail state", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::cdcl;

        store::assignment assignment;
        assignment.set_current_level(1);
        assignment.set_current_trail_position(0);
        assignment.assign(literal {variable {1}, false}, {});
        REQUIRE(assignment.value_of(variable {1}).has_value());
        REQUIRE(assignment.level_of(variable {1}) == 1);

        assignment.unassign_from(variable {1});
        REQUIRE(!assignment.value_of(variable {1}).has_value());

        store::phase phase;
        phase.set_saved_phase(variable {2}, true);
        REQUIRE(phase.saved_phase(variable {2}));

        store::assumption assumptions;
        assumptions.push(literal {variable {3}, true});
        REQUIRE(assumptions.size() == 1);

        trail trail_state;
        trail_state.push(literal {variable {4}, false});
        trail_state.push(literal {variable {5}, true});
        REQUIRE(trail_state.current_head() == 2);
        const literal expected_literal {variable {5}, true};
        REQUIRE(trail_state.literal_at(1).raw() == expected_literal.raw());

        stack::decision_frame frames;
        frames.push_frame(literal {variable {6}, false});
        REQUIRE(frames.current_level() == 1);
        const literal expected_decision {variable {6}, false};
        REQUIRE(frames.decision_literal(1).raw() == expected_decision.raw());

        // removed std::cout: "trail state test passed\n";
    }

} // namespace
