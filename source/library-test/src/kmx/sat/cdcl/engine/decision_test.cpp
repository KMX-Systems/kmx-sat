#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/engine/decision.hpp>

namespace kmx::sat::cdcl
{
    struct decision_filter_context final
    {
        std::uint32_t blocked_variable {0u};
    };

    static bool allow_variable_except_blocked(const variable var, const void* raw_context) noexcept
    {
        if (raw_context == nullptr)
            return true;

        const auto& context = *static_cast<const decision_filter_context*>(raw_context);
        return var.index() != context.blocked_variable;
    }

    TEST_CASE("decision engine", "[sat]")
    {
        engine::decision decision;
        decision.set_next_variable(7u);

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of().index() == 7u);
        REQUIRE(branch->is_negated() == false);

        const auto variable = decision.pick_decision_variable();
        REQUIRE(variable.has_value());
        REQUIRE(variable->variable_of().index() == 7u);

        REQUIRE(decision.pick_decision_phase() == true);
        decision.notify_conflict();
        decision.notify_restart();
        decision.notify_rephase();
        decision.select_heuristic_blend();
        REQUIRE(decision.has_active_blend());
        const auto selected = decision.last_selected_variable();
        REQUIRE(selected.has_value());
        REQUIRE(selected->index() == 7u);
    }

    TEST_CASE("decision engine uses evsids fallback when blend is active", "[sat]")
    {
        engine::decision decision;
        decision.set_next_variable(9u);

        const auto first_branch = decision.pick_branch_literal();
        REQUIRE(first_branch.has_value());
        REQUIRE(first_branch->variable_of().index() == 9u);

        decision.notify_conflict();
        decision.set_next_variable(0u);
        decision.select_heuristic_blend();

        const auto candidate = decision.pick_decision_variable();
        REQUIRE(candidate.has_value());
        REQUIRE(candidate->variable_of().index() == 9u);
    }

    TEST_CASE("decision engine uses conflict bump candidates without fallback variable", "[sat]")
    {
        engine::decision decision;
        const std::array<variable, 3u> bump_candidates {variable {21u}, variable {22u}, variable {23u}};

        decision.notify_conflict_variables(bump_candidates);
        decision.set_next_variable(0u);

        const auto candidate = decision.pick_decision_variable();
        REQUIRE(candidate.has_value());
        REQUIRE(candidate->variable_of().index() == 21u);
        REQUIRE(decision.has_active_blend());
    }

    TEST_CASE("decision engine uses propagated variables without fallback variable", "[sat]")
    {
        engine::decision decision;
        const std::array<variable, 2u> propagated_variables {variable {31u}, variable {32u}};

        // Propagation makes variables selectable but must not rank them: an implied literal is not evidence that
        // the variable matters, and scoring every propagation flattens the ranking for the ones that do.
        decision.notify_propagated_variables(propagated_variables);
        decision.set_next_variable(0u);

        const auto candidate = decision.pick_decision_variable();
        REQUIRE(candidate.has_value());
        const auto propagated_only = candidate->variable_of().index();
        REQUIRE(((propagated_only == 31u) || (propagated_only == 32u)));
        REQUIRE(decision.has_active_blend());

        // Conflict participation does rank, and outranks anything merely propagated.
        const std::array<variable, 1u> conflict_variables {variable {32u}};
        decision.notify_conflict_variables(conflict_variables);

        const auto ranked = decision.pick_decision_variable();
        REQUIRE(ranked.has_value());
        REQUIRE(ranked->variable_of().index() == 32u);
    }

    TEST_CASE("decision engine weights short learned clauses more aggressively", "[sat]")
    {
        engine::decision decision;
        const std::array<literal, 5u> broad_clause {literal {variable {41u}, false}, literal {variable {42u}, false},
                                                   literal {variable {43u}, false}, literal {variable {44u}, false},
                                                   literal {variable {45u}, false}};
        const std::array<literal, 2u> short_clause {literal {variable {61u}, false}, literal {variable {62u}, false}};

        decision.notify_learned_clause(broad_clause);
        decision.notify_learned_clause(short_clause);
        decision.set_next_variable(0u);

        const auto candidate = decision.pick_decision_variable();
        REQUIRE(candidate.has_value());
        REQUIRE(candidate->variable_of().index() == 61u);
        REQUIRE(decision.has_active_blend());
    }

    TEST_CASE("decision engine prioritizes asserting literal from learned clause", "[sat]")
    {
        engine::decision decision;
        const std::array<literal, 3u> learned_clause {literal {variable {91u}, true}, literal {variable {92u}, false},
                                                     literal {variable {93u}, false}};

        decision.notify_learned_clause(learned_clause);
        decision.set_next_variable(0u);

        const auto candidate = decision.pick_decision_variable();
        REQUIRE(candidate.has_value());
        REQUIRE(candidate->variable_of().index() == 91u);
    }

    TEST_CASE("decision engine reuses asserting literal polarity as saved phase", "[sat]")
    {
        engine::decision decision;
        const std::array<literal, 2u> learned_clause {literal {variable {101u}, true}, literal {variable {102u}, false}};

        decision.notify_learned_clause(learned_clause);
        decision.set_next_variable(0u);

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of().index() == 101u);
        REQUIRE(branch->is_negated());
    }

    TEST_CASE("decision engine reuses propagated assignment polarity as saved phase", "[sat]")
    {
        engine::decision decision;

        decision.notify_assignment_literal(literal {variable {121u}, true});
        decision.notify_propagated_variables(std::array<variable, 1u> {variable {121u}});
        decision.set_next_variable(0u);

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of().index() == 121u);
        REQUIRE(branch->is_negated());
    }

    TEST_CASE("decision engine flips last decision polarity on conflict", "[sat]")
    {
        engine::decision decision;
        decision.set_next_variable(141u);

        const auto first_branch = decision.pick_branch_literal();
        REQUIRE(first_branch.has_value());
        REQUIRE(first_branch->variable_of().index() == 141u);
        REQUIRE_FALSE(first_branch->is_negated());

        decision.notify_conflict();
        decision.set_next_variable(0u);

        const auto second_branch = decision.pick_branch_literal();
        REQUIRE(second_branch.has_value());
        REQUIRE(second_branch->variable_of().index() == 141u);
        REQUIRE(second_branch->is_negated());
    }

    TEST_CASE("decision engine reuses conflict clause polarity as saved phase", "[sat]")
    {
        engine::decision decision;
        const std::array<literal, 2u> conflict_clause {literal {variable {151u}, true}, literal {variable {152u}, false}};

        decision.notify_conflict_clause(conflict_clause);
        decision.set_next_variable(0u);

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of().index() == 151u);
        REQUIRE(branch->is_negated());
    }

    TEST_CASE("decision engine ignores duplicate variable literals in conflict clause feedback", "[sat]")
    {
        engine::decision decision;
        const std::array<literal, 3u> conflict_clause {literal {variable {201u}, true}, literal {variable {201u}, false},
                                                      literal {variable {202u}, false}};

        decision.notify_conflict_clause(conflict_clause);
        decision.set_next_variable(0u);

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of().index() == 201u);
        REQUIRE_FALSE(branch->is_negated());
    }

    TEST_CASE("decision engine ignores duplicate variables in conflict candidate feedback", "[sat]")
    {
        engine::decision decision;
        const std::array<variable, 3u> conflict_candidates {variable {211u}, variable {211u}, variable {212u}};

        decision.notify_conflict_variables(conflict_candidates);
        decision.set_next_variable(0u);

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of().index() == 211u);
    }

    TEST_CASE("decision engine ignores duplicate propagated variables", "[sat]")
    {
        engine::decision decision;
        const std::array<variable, 3u> propagated_variables {variable {221u}, variable {221u}, variable {222u}};

        decision.notify_assignment_literal(literal {variable {221u}, true});
        decision.notify_propagated_variables(propagated_variables);
        decision.set_next_variable(0u);

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of().index() == 221u);
        REQUIRE(branch->is_negated());
    }

    TEST_CASE("decision engine prioritizes conflict participation over assignment recency", "[sat]")
    {
        engine::decision decision;

        // Assignment activates a variable and saves its phase, but deliberately does not rank it. Ranking by
        // assignment recency tracks propagation order rather than where the search is stuck; measured on random
        // 3-SAT it needed several times as many conflicts as ranking by conflict participation.
        decision.notify_assignment_literal(literal {variable {241u}, false});
        decision.notify_assignment_literal(literal {variable {242u}, false});
        decision.set_next_variable(0u);

        const std::array<variable, 1u> conflict_variables {variable {241u}};
        decision.notify_conflict_variables(conflict_variables);

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of().index() == 241u);

        // The saved phase from the assignment is still what decides polarity.
        REQUIRE_FALSE(branch->is_negated());
    }

    TEST_CASE("decision engine skips non-selectable heuristic candidates", "[sat]")
    {
        engine::decision decision;
        const std::array<literal, 2u> learned_clause {literal {variable {251u}, false}, literal {variable {252u}, false}};

        decision.notify_learned_clause(learned_clause);
        decision.set_next_variable(0u);

        const decision_filter_context filter_context {251u};
        decision.set_selectability_filter(&allow_variable_except_blocked, &filter_context);

        const auto candidate = decision.pick_decision_variable();
        REQUIRE(candidate.has_value());
        REQUIRE(candidate->variable_of().index() == 252u);
    }

    TEST_CASE("decision engine applies periodic heuristic maintenance", "[sat]")
    {
        engine::decision decision;
        decision.set_maintenance_intervals(16u, 8u, 4u);
        decision.set_next_variable(281u);

        const auto first_branch = decision.pick_branch_literal();
        REQUIRE(first_branch.has_value());

        for (std::uint32_t index = 0u; index < 16u; ++index)
            decision.notify_conflict();

        for (std::uint32_t index = 0u; index < 4u; ++index)
            decision.notify_restart();

        REQUIRE(decision.evsids_rescale_count() == 1u);
        REQUIRE(decision.chb_decay_count() == 3u);

        decision.set_maintenance_intervals(0u, 0u, 0u);
        for (std::uint32_t index = 0u; index < 32u; ++index)
            decision.notify_conflict();
        for (std::uint32_t index = 0u; index < 8u; ++index)
            decision.notify_restart();

        REQUIRE(decision.evsids_rescale_count() == 1u);
        REQUIRE(decision.chb_decay_count() == 3u);
    }

    TEST_CASE("decision engine can opt into CHB candidate selection", "[sat]")
    {
        engine::decision decision;
        REQUIRE_FALSE(decision.chb_enabled());

        decision.notify_conflict_variables(std::array<variable, 1u> {variable {301u}});
        decision.notify_conflict_variables(std::array<variable, 1u> {variable {302u}});
        decision.notify_conflict_variables(std::array<variable, 1u> {variable {302u}});
        decision.notify_conflict_variables(std::array<variable, 1u> {variable {302u}});
        decision.set_next_variable(0u);
        decision.set_chb_enabled(true);

        const auto branch = decision.pick_branch_literal();
        REQUIRE(branch.has_value());
        REQUIRE(branch->variable_of().index() == 302u);
        REQUIRE(decision.chb_enabled());
    }
}
