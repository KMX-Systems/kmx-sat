#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/controller/rephase.hpp>
#include <kmx/sat/cdcl/store/phase.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("rephase controller", "[sat]")
    {
        using namespace kmx::sat;

        controller::rephase controller;
        store::phase phases;
        const auto var_a = variable {1u};
        const auto var_b = variable {2u};

        phases.set_saved_phase(var_a, true);
        phases.set_saved_phase(var_b, false);
        controller.attach_phase_store(phases);

        REQUIRE(controller.should_rephase() == false);
        controller.apply_inverted();
        REQUIRE(phases.saved_phase(var_a) == false);
        REQUIRE(phases.saved_phase(var_b) == true);

        controller.apply_best();
        REQUIRE(phases.saved_phase(var_a) == false);
        REQUIRE(phases.saved_phase(var_b) == true);

        controller.apply_random();
        REQUIRE(phases.saved_phase(var_a) == true);

        controller.record_conflict();
        controller.record_decision();
        REQUIRE(controller.should_rephase() == true);
        REQUIRE(controller.has_pending_rephase() == true);
        REQUIRE(controller.observed_opportunity_count() == 2u);
    }

    TEST_CASE("rephase controller preserves best snapshot", "[sat]")
    {
        using namespace kmx::sat;

        controller::rephase controller;
        store::phase phases;
        const auto var_a = variable {1u};

        phases.set_saved_phase(var_a, true);
        controller.attach_phase_store(phases);

        controller.apply_best();
        phases.set_saved_phase(var_a, false);

        controller.apply_best();

        REQUIRE(phases.saved_phase(var_a) == true);
    }
}
