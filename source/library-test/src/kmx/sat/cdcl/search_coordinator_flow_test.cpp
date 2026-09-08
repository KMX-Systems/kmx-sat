#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <vector>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/search_coordinator.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    struct coordinator_filter_context final
    {
        std::uint32_t blocked_variable {0u};
    };

    static bool allow_variable_except_blocked(const variable var, const void* raw_context) noexcept
    {
        if (raw_context == nullptr)
            return true;

        const auto& context = *static_cast<const coordinator_filter_context*>(raw_context);
        return var.index() != context.blocked_variable;
    }

    TEST_CASE("search coordinator flow", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::cdcl;

        {
            search_coordinator coordinator;
            solve_request request;
            request.assumptions.push_back(literal {variable {1u}, false});
            coordinator.apply_assumptions(request);

            coordinator.stage_conflict(clause::ref_t {7u});
            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::unsatisfiable);
            REQUIRE(coordinator.assumption_propagation_call_count() == 1u);
            REQUIRE(coordinator.propagation_call_count() == 0u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            const std::vector<literal> conflict_clause {literal {variable {1u}, false}, literal {variable {2u}, true},
                                                        literal {variable {3u}, false}, literal {variable {2u}, true}};
            coordinator.seed_conflict_clause(std::span<const literal> {conflict_clause});
            coordinator.set_decision_level(variable {1u}, 1u);
            coordinator.set_decision_level(variable {2u}, 4u);
            coordinator.set_decision_level(variable {3u}, 2u);
            coordinator.stage_conflict(clause::ref_t {9u});
            coordinator.set_next_decision_variable(10u);

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.learned_clause_count() == 1u);
            REQUIRE(coordinator.last_asserting_literal().raw() == literal {variable {2u}, true}.raw());
            REQUIRE(coordinator.assumption_propagation_call_count() == 1u);
            REQUIRE(coordinator.propagation_call_count() == 2u);
            REQUIRE(coordinator.conflict_event_count() == 1u);
            REQUIRE(coordinator.decision_event_count() == 1u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            const std::vector<literal> conflict_clause {literal {variable {41u}, false}, literal {variable {42u}, true},
                                                        literal {variable {43u}, false}};
            coordinator.seed_conflict_clause(std::span<const literal> {conflict_clause});
            coordinator.set_decision_level(variable {41u}, 4u);
            coordinator.set_decision_level(variable {42u}, 2u);
            coordinator.set_decision_level(variable {43u}, 1u);
            coordinator.stage_conflict(clause::ref_t {29u});

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.learned_clause_count() == 1u);
            REQUIRE(coordinator.conflict_event_count() == 1u);
            REQUIRE(coordinator.decision_event_count() == 1u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            // conflict_analyzer::analyze() deduplicates seeded literals by variable identity, keeping only the
            // first occurrence; a variable that appears with both polarities therefore collapses to a single
            // (non-tautological) occurrence before clause::learner ever sees it, so this can never actually reach
            // clause::learner's own tautology guard. This verifies that safe, non-tautological collapse.
            const std::vector<literal> duplicate_variable_conflict_clause {literal {variable {51u}, false}, literal {variable {51u}, true},
                                                                           literal {variable {52u}, false}};
            coordinator.seed_conflict_clause(std::span<const literal> {duplicate_variable_conflict_clause});
            coordinator.set_decision_level(variable {51u}, 4u);
            coordinator.set_decision_level(variable {52u}, 2u);
            coordinator.stage_conflict(clause::ref_t {31u});

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.learned_clause_count() == 1u);
            REQUIRE(coordinator.last_learned_clause().size() == 2u);
            REQUIRE(coordinator.last_learned_clause()[0u].raw() == literal {variable {51u}, false}.raw());
            REQUIRE(coordinator.last_learned_clause()[1u].raw() == literal {variable {52u}, false}.raw());
            REQUIRE(coordinator.last_asserting_literal().raw() == literal {variable {51u}, false}.raw());
            REQUIRE(coordinator.conflict_event_count() == 1u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.request_restart();
            coordinator.set_next_decision_variable(4u);

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.restart_count() == 1u);
            REQUIRE(coordinator.decision_event_count() == 1u);
        }

        {
            search_coordinator coordinator;
            clause::database database;

            const literal learned_lit_a {variable {21u}, false};
            const literal learned_lit_b {variable {22u}, true};
            const auto removable_ref = database.add_clause(std::array<literal, 2u> {learned_lit_a, learned_lit_b}, true);
            const auto reason_ref = database.add_clause(std::array<literal, 1u> {learned_lit_a}, true);
            database.mark_reason_clause(reason_ref);

            coordinator.attach_database(database);
            coordinator.apply_assumptions({});
            coordinator.request_reduce();
            coordinator.set_next_decision_variable(10u);

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.reduction_pass_count() == 1u);
            REQUIRE(coordinator.reduced_clause_count() == 1u);
            REQUIRE(coordinator.deleted_clause_count() == 1u);
            REQUIRE(coordinator.decision_event_count() == 1u);
            REQUIRE(database.stats_snapshot().redundant_count == 1u);
            REQUIRE(database.storage_of().is_alive(removable_ref) == false);
            REQUIRE(database.storage_of().is_alive(reason_ref));
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.set_restart_interval(2u);

            const std::vector<literal> conflict_clause {literal {variable {11u}, false}, literal {variable {12u}, true}};
            coordinator.seed_conflict_clause(std::span<const literal> {conflict_clause});
            coordinator.set_decision_level(variable {11u}, 1u);
            coordinator.set_decision_level(variable {12u}, 2u);
            coordinator.stage_conflict(clause::ref_t {13u});
            coordinator.set_next_decision_variable(14u);

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.conflict_event_count() == 1u);
            REQUIRE(coordinator.restart_count() == 0u);
            REQUIRE(coordinator.current_restart_budget() == 1u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.set_restart_interval(1u);

            const std::vector<literal> conflict_clause {literal {variable {15u}, false}, literal {variable {16u}, true}};
            coordinator.seed_conflict_clause(std::span<const literal> {conflict_clause});
            coordinator.set_decision_level(variable {15u}, 1u);
            coordinator.set_decision_level(variable {16u}, 2u);
            coordinator.stage_conflict(clause::ref_t {17u});
            coordinator.set_next_decision_variable(18u);

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.conflict_event_count() == 1u);
            REQUIRE(coordinator.restart_count() == 1u);
            REQUIRE(coordinator.current_restart_budget() == 1u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.set_decision_restart_interval(1u);
            coordinator.set_next_decision_variable(31u);

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.decision_event_count() == 1u);
            REQUIRE(coordinator.restart_count() == 1u);
            REQUIRE(coordinator.current_decision_restart_budget() == 1u);

            coordinator.set_next_decision_variable(32u);
            coordinator.run_search_epoch();

            REQUIRE(coordinator.decision_event_count() == 2u);
            REQUIRE(coordinator.restart_count() == 2u);
            REQUIRE(coordinator.current_decision_restart_budget() == 1u);
        }

        {
            search_coordinator coordinator;
            solve_request request;
            request.decision_limit = 1u;
            coordinator.apply_assumptions(request);
            coordinator.set_next_decision_variable(8u);

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::terminated);
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::decision_limit);
            REQUIRE(coordinator.decision_event_count() == 1u);
        }

        {
            search_coordinator coordinator;
            solve_request request;
            request.conflict_limit = 1u;
            coordinator.apply_assumptions(request);
            coordinator.stage_conflict(clause::ref_t {12u});

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::terminated);
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::conflict_limit);
            REQUIRE(coordinator.conflict_event_count() == 1u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::none);

            coordinator.handle_termination();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::terminated);
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::external);

            coordinator.apply_assumptions({});
            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::none);

            coordinator.handle_termination();
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::external);
            coordinator.handle_sat();
            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::satisfiable);
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::none);

            coordinator.handle_termination();
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::external);
            coordinator.handle_unsat();
            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::unsatisfiable);
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::none);
        }

        {
            search_coordinator coordinator;
            solve_request request;
            request.conflict_limit = 1u;
            request.decision_limit = 5u;
            coordinator.apply_assumptions(request);
            coordinator.stage_conflict(clause::ref_t {41u});
            coordinator.set_next_decision_variable(99u);

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::terminated);
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::conflict_limit);
            REQUIRE(coordinator.conflict_event_count() == 1u);
            REQUIRE(coordinator.decision_event_count() == 0u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.set_restart_interval(3u);
            coordinator.request_restart();

            REQUIRE(coordinator.current_restart_budget() == 0u);
            REQUIRE(coordinator.restart_count() == 0u);

            // An inprocessing epoch must not swallow a restart that is already due. Epochs complete roughly
            // every thirty conflicts, so consuming the pending flag here (and rebasing the conflict budget)
            // starved every restart interval above that cadence: the trigger was pushed out faster than the
            // conflict counter could reach it. The restart stays pending until the search actually performs it.
            coordinator.notify_inprocess_epoch_completed();

            REQUIRE(coordinator.restart_count() == 0u);
            REQUIRE(coordinator.should_restart());
            REQUIRE(coordinator.current_restart_budget() == 0u);

            coordinator.handle_restart();

            REQUIRE(coordinator.restart_count() == 1u);
            REQUIRE_FALSE(coordinator.should_restart());
            REQUIRE(coordinator.current_restart_budget() == 3u);

            coordinator.set_next_decision_variable(27u);
            coordinator.run_search_epoch();
            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.decision_event_count() == 1u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            const std::array<literal, 5u> broad_clause {literal {variable {71u}, false}, literal {variable {72u}, false},
                                                       literal {variable {73u}, false}, literal {variable {74u}, false},
                                                       literal {variable {75u}, false}};
            const std::array<literal, 2u> short_clause {literal {variable {81u}, false}, literal {variable {82u}, false}};

            coordinator.notify_learned_clause(broad_clause);
            coordinator.notify_learned_clause(short_clause);

            const auto candidate = coordinator.next_branch_literal(0u);
            REQUIRE(candidate.has_value());
            REQUIRE(candidate->variable_of().index() == 81u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            const std::array<literal, 3u> learned_clause {literal {variable {111u}, true}, literal {variable {112u}, false},
                                                         literal {variable {113u}, false}};

            coordinator.notify_learned_clause(learned_clause);

            const auto branch = coordinator.next_branch_literal(0u);
            REQUIRE(branch.has_value());
            REQUIRE(branch->variable_of().index() == 111u);
            REQUIRE(branch->is_negated());
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            coordinator.notify_assignment_literal(literal {variable {131u}, true});
            coordinator.notify_propagated_variables(std::array<variable, 1u> {variable {131u}});

            const auto branch = coordinator.next_branch_literal(0u);
            REQUIRE(branch.has_value());
            REQUIRE(branch->variable_of().index() == 131u);
            REQUIRE(branch->is_negated());
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            const std::array<literal, 2u> conflict_clause {literal {variable {161u}, true}, literal {variable {162u}, false}};
            coordinator.notify_conflict_clause(conflict_clause);

            const auto branch = coordinator.next_branch_literal(0u);
            REQUIRE(branch.has_value());
            REQUIRE(branch->variable_of().index() == 161u);
            REQUIRE(branch->is_negated());
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            const std::array<literal, 3u> duplicate_conflict_clause {literal {variable {191u}, true}, literal {variable {191u}, false},
                                                                    literal {variable {192u}, false}};
            coordinator.notify_conflict_clause(duplicate_conflict_clause);

            const auto branch = coordinator.next_branch_literal(0u);
            REQUIRE(branch.has_value());
            REQUIRE(branch->variable_of().index() == 191u);
            REQUIRE_FALSE(branch->is_negated());
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            const std::array<literal, 2u> learned_clause {literal {variable {261u}, false}, literal {variable {262u}, false}};
            coordinator.notify_learned_clause(learned_clause);

            const coordinator_filter_context filter_context {261u};
            coordinator.set_variable_selectability_filter(&allow_variable_except_blocked, &filter_context);

            const auto branch = coordinator.next_branch_literal(0u);
            REQUIRE(branch.has_value());
            REQUIRE(branch->variable_of().index() == 262u);
            REQUIRE_FALSE(branch->is_negated());
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.clear_variable_selectability_filter();
            coordinator.set_next_decision_variable(271u);

            const auto branch = coordinator.next_branch_literal(271u);
            REQUIRE(branch.has_value());
            REQUIRE(branch->variable_of().index() == 271u);
            REQUIRE_FALSE(branch->is_negated());
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.set_next_decision_variable(377u);

            const auto branch = coordinator.next_branch_literal(0u);
            REQUIRE(branch.has_value());
            REQUIRE(branch->variable_of().index() == 377u);
            REQUIRE_FALSE(branch->is_negated());
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            coordinator.notify_assignment_literal(literal {variable {231u}, true});
            coordinator.notify_propagated_variables(std::array<variable, 3u> {variable {231u}, variable {231u}, variable {232u}});

            const auto branch = coordinator.next_branch_literal(0u);
            REQUIRE(branch.has_value());
            REQUIRE(branch->variable_of().index() == 231u);
            REQUIRE(branch->is_negated());
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.set_decision_maintenance_intervals(1u, 1u, 1u);
            const auto intervals = coordinator.decision_maintenance_intervals();
            REQUIRE(intervals[0u] == 1u);
            REQUIRE(intervals[1u] == 1u);
            REQUIRE(intervals[2u] == 1u);

            const std::array<literal, 3u> conflict_clause {literal {variable {171u}, true}, literal {variable {172u}, false},
                                                          literal {variable {173u}, false}};
            coordinator.seed_conflict_clause(conflict_clause);
            coordinator.set_decision_level(variable {171u}, 3u);
            coordinator.set_decision_level(variable {172u}, 2u);
            coordinator.set_decision_level(variable {173u}, 1u);

            coordinator.handle_conflict();

            const auto learned_driven_branch = coordinator.next_branch_literal(0u);
            REQUIRE(learned_driven_branch.has_value());
            REQUIRE(learned_driven_branch->variable_of().index() == 171u);
            REQUIRE(learned_driven_branch->is_negated());
            REQUIRE(coordinator.decision_evsids_rescale_count() >= 1u);
            REQUIRE(coordinator.decision_chb_decay_count() >= 1u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.set_restart_interval(4u);

            // Inprocessing epochs track their own yield/deferral bookkeeping but never consume a due restart
            // and never rebase the conflict budget; only the search performing the restart does that.
            coordinator.request_restart();
            coordinator.notify_inprocess_epoch_completed(0u);

            REQUIRE(coordinator.inprocess_epoch_notification_count() == 1u);
            REQUIRE(coordinator.low_yield_inprocess_epoch_count() == 1u);
            REQUIRE(coordinator.deferred_inprocess_resync_count() == 0u);
            REQUIRE(coordinator.restart_count() == 0u);
            REQUIRE(coordinator.should_restart());
            REQUIRE(coordinator.current_restart_budget() == 0u);

            coordinator.request_restart();
            coordinator.notify_inprocess_epoch_completed(0u);

            REQUIRE(coordinator.inprocess_epoch_notification_count() == 2u);
            REQUIRE(coordinator.low_yield_inprocess_epoch_count() == 2u);
            REQUIRE(coordinator.deferred_inprocess_resync_count() == 1u);
            REQUIRE(coordinator.restart_count() == 0u);
            REQUIRE(coordinator.current_restart_budget() == 0u);

            coordinator.notify_inprocess_epoch_completed(1u);

            REQUIRE(coordinator.inprocess_epoch_notification_count() == 3u);
            REQUIRE(coordinator.deferred_inprocess_resync_count() == 1u);
            REQUIRE(coordinator.restart_count() == 0u);
            REQUIRE(coordinator.current_restart_budget() == 0u);

            coordinator.handle_restart();

            REQUIRE(coordinator.restart_count() == 1u);
            REQUIRE(coordinator.current_restart_budget() == 4u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.set_restart_interval(5u);

            coordinator.request_restart();
            coordinator.notify_inprocess_epoch_completed(0u);
            REQUIRE(coordinator.restart_count() == 0u);
            REQUIRE(coordinator.should_restart());
            REQUIRE(coordinator.current_restart_budget() == 0u);

            coordinator.request_restart();
            coordinator.notify_inprocess_epoch_completed(0u);
            REQUIRE(coordinator.restart_count() == 0u);
            REQUIRE(coordinator.current_restart_budget() == 0u);
            REQUIRE(coordinator.deferred_inprocess_resync_count() == 1u);

            // The deferral guard bounds how long resynchronization can be postponed; it still must not
            // manufacture a restart that the search never performed.
            coordinator.notify_inprocess_epoch_completed(0u);
            REQUIRE(coordinator.restart_count() == 0u);
            REQUIRE(coordinator.current_restart_budget() == 0u);
            REQUIRE(coordinator.deferred_inprocess_resync_count() == 1u);

            coordinator.handle_restart();
            REQUIRE(coordinator.restart_count() == 1u);
            REQUIRE(coordinator.current_restart_budget() == 5u);
        }

        {
            search_coordinator coordinator;
            coordinator.set_restart_interval(3u);
            coordinator.apply_assumptions({});

            REQUIRE(coordinator.current_restart_budget() == 3u);

            coordinator.request_restart();
            REQUIRE(coordinator.current_restart_budget() == 0u);

            coordinator.apply_assumptions({});
            REQUIRE(coordinator.current_restart_budget() == 3u);
            REQUIRE(coordinator.restart_count() == 0u);
        }

        {
            search_coordinator coordinator;
            coordinator.set_decision_restart_interval(2u);
            coordinator.apply_assumptions({});

            REQUIRE(coordinator.current_decision_restart_budget() == 2u);

            coordinator.request_restart();
            REQUIRE(coordinator.current_decision_restart_budget() == 0u);

            coordinator.apply_assumptions({});
            REQUIRE(coordinator.current_decision_restart_budget() == 2u);
            REQUIRE(coordinator.restart_count() == 0u);

            coordinator.set_next_decision_variable(341u);
            coordinator.run_search_epoch();
            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.decision_event_count() == 1u);
            REQUIRE(coordinator.current_decision_restart_budget() == 1u);

            coordinator.set_next_decision_variable(342u);
            coordinator.run_search_epoch();
            REQUIRE(coordinator.decision_event_count() == 2u);
            REQUIRE(coordinator.restart_count() == 1u);
        }

        {
            search_coordinator coordinator;
            solve_request request;
            request.decision_limit = 1u;
            coordinator.apply_assumptions(request);
            coordinator.set_restart_interval(5u);
            coordinator.request_restart();
            coordinator.request_reduce();
            coordinator.set_next_decision_variable(351u);

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::terminated);
            REQUIRE(coordinator.current_termination_cause() == search_coordinator::termination_cause::decision_limit);
            REQUIRE(coordinator.restart_count() == 1u);
            REQUIRE(coordinator.reduction_pass_count() == 1u);
            REQUIRE(coordinator.decision_event_count() == 1u);
            REQUIRE(coordinator.current_restart_budget() == 5u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.set_restart_interval(2u);
            coordinator.request_restart();
            coordinator.request_reduce();
            coordinator.notify_inprocess_epoch_completed(0u);

            // The epoch notification leaves the pending restart and its conflict budget untouched.
            REQUIRE(coordinator.restart_count() == 0u);
            REQUIRE(coordinator.should_restart());
            REQUIRE(coordinator.reduction_pass_count() == 0u);
            REQUIRE(coordinator.current_restart_budget() == 0u);
            REQUIRE(coordinator.inprocess_epoch_notification_count() == 1u);

            coordinator.apply_assumptions({});
            coordinator.set_next_decision_variable(333u);
            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.restart_count() == 0u);
            REQUIRE(coordinator.reduction_pass_count() == 0u);
            REQUIRE(coordinator.current_restart_budget() == 2u);
            REQUIRE(coordinator.inprocess_epoch_notification_count() == 0u);
            REQUIRE(coordinator.deferred_inprocess_resync_count() == 0u);
            REQUIRE(coordinator.decision_event_count() == 1u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});
            coordinator.set_next_decision_variable(389u);

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::in_progress);
            REQUIRE(coordinator.decision_event_count() == 1u);
            const auto followup_branch = coordinator.next_branch_literal(0u);
            REQUIRE(followup_branch.has_value());
            REQUIRE(followup_branch->variable_of().index() == 389u);
        }

        {
            search_coordinator coordinator;
            coordinator.apply_assumptions({});

            coordinator.run_search_epoch();

            REQUIRE(coordinator.current_outcome() == search_coordinator::outcome::satisfiable);
            REQUIRE(coordinator.assumption_propagation_call_count() == 1u);
            REQUIRE(coordinator.propagation_call_count() == 1u);

            coordinator.run_search_epoch();
            REQUIRE(coordinator.assumption_propagation_call_count() == 1u);
            REQUIRE(coordinator.propagation_call_count() == 1u);
        }

        // removed std::cout: "search coordinator flow test passed\n";
    }

} // namespace
