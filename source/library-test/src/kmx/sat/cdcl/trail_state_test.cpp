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
        assignment.set_current_level(1u);
        assignment.set_current_trail_position(0u);
        assignment.assign(literal {variable {1u}, false}, {});
        REQUIRE(assignment.value_of(variable {1u}).has_value());
        REQUIRE(assignment.level_of(variable {1u}) == 1u);

        assignment.unassign_from(variable {1u});
        REQUIRE(!assignment.value_of(variable {1u}).has_value());

        store::phase phase;
        phase.set_saved_phase(variable {2u}, true);
        REQUIRE(phase.saved_phase(variable {2u}));

        store::assumption assumptions;
        assumptions.push(literal {variable {3u}, true});
        REQUIRE(assumptions.size() == 1u);

        trail trail_state;
        trail_state.push(literal {variable {4u}, false});
        trail_state.push(literal {variable {5u}, true});
        REQUIRE(trail_state.current_head() == 2u);
        const literal expected_literal {variable {5u}, true};
        REQUIRE(trail_state.literal_at(1u).raw() == expected_literal.raw());

        stack::decision_frame frames;
        frames.push_frame(literal {variable {6u}, false});
        REQUIRE(frames.current_level() == 1u);
        const literal expected_decision {variable {6u}, false};
        REQUIRE(frames.decision_literal(1u).raw() == expected_decision.raw());

        // removed std::cout: "trail state test passed\n";
    }

} // namespace
