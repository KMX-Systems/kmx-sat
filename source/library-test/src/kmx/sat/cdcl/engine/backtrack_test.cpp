#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/engine/backtrack.hpp>
#include <kmx/sat/cdcl/stack/decision_frame.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/trail.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("backtrack unwinds to the requested level", "[sat]")
    {
        engine::backtrack backtrack;
        trail trail_state;
        store::assignment assignment;
        stack::decision_frame decision_frames;

        const literal first {variable {1u}, false};
        const literal second {variable {2u}, false};
        const literal third {variable {3u}, false};

        decision_frames.push_frame(first);
        decision_frames.set_current_trail_base(1u);
        decision_frames.push_frame(second);

        trail_state.push(first);
        trail_state.push(second);
        trail_state.push(third);

        assignment.set_current_level(1u);
        assignment.set_current_trail_position(1u);
        assignment.assign(first, {});
        assignment.set_current_level(2u);
        assignment.set_current_trail_position(2u);
        assignment.assign(second, {});
        assignment.assign(third, {});

        backtrack.attach_state(trail_state, assignment, decision_frames);
        backtrack.backtrack_to_level(1u);

        REQUIRE(trail_state.current_head() == 1u);
        REQUIRE(decision_frames.current_level() == 1u);
        REQUIRE(assignment.value_of(variable {2u}).has_value() == false);
        REQUIRE(assignment.level_of(variable {1u}) == 1u);
        REQUIRE(assignment.level_of(variable {3u}) == 0u);
        REQUIRE(backtrack.last_backtracked_level() == 1u);

        assignment.mark_analysis_seen(variable {3u});
        backtrack.clear_transient_marks();
        REQUIRE(assignment.analysis_seen(variable {3u}) == false);

        const literal fourth {variable {4u}, false};
        decision_frames.push_frame(fourth);
        assignment.set_current_level(2u);
        assignment.set_current_trail_position(4u);
        assignment.assign(fourth, {});

        backtrack.chronological_backtrack();
        REQUIRE(assignment.level_of(variable {4u}) == 0u);
        REQUIRE(assignment.value_of(variable {4u}).has_value() == false);
        REQUIRE(decision_frames.current_level() == 1u);
    }
}
