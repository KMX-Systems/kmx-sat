#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <kmx/sat/cdcl/chb_tracker.hpp>
#include <kmx/sat/cdcl/engine/decision.hpp>
#include <kmx/sat/cdcl/evsids_heap.hpp>
#include <kmx/sat/cdcl/vmtf_queue.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("heuristic validation keeps deterministic candidate ordering", "[sat]")
    {
        evsids_heap evsids;
        const variable low_index {101u};
        const variable high_index {202u};

        evsids.increase_score(high_index);
        evsids.increase_score(low_index);
        evsids.increase_score(high_index);

        REQUIRE(evsids.extract_best().value() == high_index);
        REQUIRE(evsids.extract_best().value() == low_index);
        REQUIRE_FALSE(evsids.extract_best().has_value());
    }

    TEST_CASE("heuristic validation preserves VMTF recency and removal semantics", "[sat]")
    {
        vmtf_queue vmtf;
        const variable first {301u};
        const variable second {302u};
        const variable third {303u};

        vmtf.activate(first);
        vmtf.activate(second);
        vmtf.activate(third);
        vmtf.bump(third);
        REQUIRE(vmtf.front_candidate().value() == third);

        vmtf.remove(third);
        REQUIRE(vmtf.front_candidate().value() == first);
        REQUIRE(vmtf.size() == 2u);

        vmtf.reinsert(third);
        REQUIRE(vmtf.size() == 3u);
        vmtf.bump(third);
        REQUIRE(vmtf.front_candidate().value() == third);
    }

    TEST_CASE("heuristic validation bounds CHB scores and selects the strongest opt-in candidate", "[sat]")
    {
        chb_tracker chb;
        const variable weaker {401u};
        const variable stronger {402u};

        chb.update_on_conflict(weaker);
        for (std::uint32_t index {}; index < 8u; ++index)
        {
            chb.update_on_conflict(stronger);
        }

        REQUIRE(chb.score_of(weaker) >= 0.0);
        REQUIRE(chb.score_of(stronger) <= 1.0);
        REQUIRE(chb.best_candidate([](const variable) noexcept { return true; }).value() == stronger);
    }

    TEST_CASE("heuristic validation preserves phase, filtering, and maintenance cadence", "[sat]")
    {
        engine::decision decision;
        const variable selected {501u};
        const variable blocked {502u};

        decision.set_next_variable(0u);
        decision.notify_assignment_literal(literal {selected, true});
        decision.set_selectability_filter(
            [](const variable var, const void* context) noexcept
            {
                return var.index() != static_cast<const variable*>(context)->index();
            },
            &blocked);
        decision.notify_conflict_variables(std::array<variable, 1> {selected});

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of() == selected);
        REQUIRE(branch->is_negated());

        decision.set_maintenance_intervals(2u, 3u, 4u);
        decision.notify_conflict();
        decision.notify_conflict();
        decision.notify_conflict();
        REQUIRE(decision.evsids_rescale_count() == 1u);
        REQUIRE(decision.chb_decay_count() == 1u);
    }
}
