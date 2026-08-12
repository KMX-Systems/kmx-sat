/// @file inc/kmx/sat/cdcl/solver_core.hpp
/// @brief The main internal solver container, but without degenerating back into an opaque monolith.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <initializer_list>
    #include <optional>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/incremental_context.hpp>
#include <kmx/sat/cdcl/memory_governor.hpp>
#include <kmx/sat/cdcl/search_coordinator.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/flush_restore_manager.hpp>
#include <kmx/sat/simplify/forward_subsumer.hpp>
#include <kmx/sat/simplify/scheduler/inprocess.hpp>
#include <kmx/sat/simplify/scheduler/preprocess.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief The main internal solver container, but without degenerating back into an opaque monolith.
    ///
    /// @details
    /// `solver_core` plays the same coordinating role as CaDiCaL's `Internal` struct, but as a composition root over
    /// separately testable objects (`clause::database`, `search_coordinator`, and transitively every CDCL component)
    /// rather than one large struct holding all state and behavior as fields and methods. `solve`/
    /// `solve_under_assumptions` are the entry points `external_frontend` calls into for one episode;
    /// `add_problem_clause` registers an original clause before or between episodes; `current_status` exposes the
    /// last terminal outcome; `extract_internal_model`/`extract_failed_core` hand raw internal-variable results to
    /// `model_reconstructor`/`failed_core_extractor` for translation back to external variables.
    /// @note The aggregate memory layout and call overhead of this composition-root design must remain within
    /// measurement noise of an equivalent flat-struct baseline, as validated by Phase 5 comparative benchmarking
    /// against pinned CaDiCaL/Kissat releases.
    class solver_core final
    {
    public:
        /// @brief Enumerates the internal (pre-external-mapping) outcomes of one solve episode.
        enum class status
        {
            /// @brief The formula is satisfiable at the internal-variable level.
            satisfiable,
            /// @brief The formula is unsatisfiable at the internal-variable level.
            unsatisfiable,
            /// @brief The episode ended without a definite result.
            unknown
        };

        /// @brief Constructs a solver core with an empty clause database and a fresh search coordinator.
        /// @throws None (noexcept).
        solver_core() noexcept { rebind_internal_views(); }

        /// @brief Resets the core to an empty, freshly bound state without invalidating internal helper pointers.
        /// @throws None (noexcept).
        void reset() noexcept
        {
            clause_database_ = {};
            search_coordinator_ = {};
            proof_manager_ = {};
            incremental_context_ = {};
            memory_governor_ = {};
            watch_list_ = {};
            variable_mapper_ = {};
            flush_restore_manager_ = {};
            forward_subsumer_ = {};
            preprocess_scheduler_ = {};
            inprocess_scheduler_ = {};
            original_clause_count_ = 0u;
            internal_model_.clear();
            failed_core_.clear();
            status_ = status::unknown;
            rebind_internal_views();
        }

        /// @brief Runs one solve episode under the given request.
        /// @param request Solve configuration for this episode (assumptions, limits, mode flags).
        /// @return Internal-level terminal status for this episode.
        /// @throws None (noexcept).
        status solve(const solve_request& request) noexcept
        {
            internal_model_.clear();
            failed_core_.clear();

            incremental_context_.begin_solve_epoch();
            memory_governor_.reset_epoch_usage();
            forward_subsumer_.run();
            preprocess_scheduler_.clear_abort();
            preprocess_scheduler_.run_initial_pipeline();
            preprocess_scheduler_.report_pass_summary();
            inprocess_scheduler_.clear_abort();

            search_coordinator_.apply_assumptions(request);

            const auto finalize_epoch = [this](const status result) noexcept
            {
                status_ = result;
                incremental_context_.end_solve_epoch();
                incremental_context_.reset_transient_state();
                synchronize_search_outcome(status_);
                return status_;
            };

            const auto max_variable = find_max_variable(request.assumptions);
            assignment_vector assignment(static_cast<std::size_t>(max_variable + 1u), unassigned_value);
            decision_level_vector decision_levels(static_cast<std::size_t>(max_variable + 1u), 0u);
            reason_vector reasons(static_cast<std::size_t>(max_variable + 1u), clause::ref_t {});

            for (const auto assumption: request.assumptions)
            {
                if (!assign_literal(assignment, decision_levels, reasons, assumption, 0u))
                {
                    failed_core_ = request.assumptions;
                    return finalize_epoch(status::unsatisfiable);
                }
            }

            std::uint64_t conflicts = 0;
            status_ = solve_recursive(assignment, decision_levels, reasons, request, conflicts, 0u);

            if (status_ == status::satisfiable)
            {
                build_internal_model(assignment);
            }
            else if (status_ == status::unsatisfiable)
            {
                failed_core_ = request.assumptions;
            }

            run_inprocess_if_due();

            return finalize_epoch(status_);
        }

        /// @brief Runs one solve episode restricted to the given internal assumption literals.
        /// @param assumptions Internal assumption literals for this episode.
        /// @return Internal-level terminal status for this episode.
        /// @throws None (noexcept).
        status solve_under_assumptions(const std::span<const literal> assumptions) noexcept
        {
            solve_request request {};
            request.assumptions.assign(assumptions.begin(), assumptions.end());
            return solve(request);
        }

        /// @brief Registers an original (non-redundant) problem clause with the internal clause database.
        /// @param literals Internal literals composing the clause.
        /// @throws None (noexcept).
        void add_problem_clause(const std::span<const literal> literals) noexcept
        {
            const auto ref = clause_database_.add_clause(literals, false);
            search_coordinator_.attach_clause(ref);
            proof_manager_.on_add_original(ref, literals);
            ++original_clause_count_;
        }

        /// @brief Convenience overload for adding a clause from a braced literal list.
        /// @param literals Internal literals composing the clause.
        /// @throws None (noexcept).
        void add_problem_clause(const std::initializer_list<literal> literals) noexcept
        {
            add_problem_clause(std::span<const literal> {literals.begin(), literals.size()});
        }

        /// @brief Returns the terminal status of the most recently completed solve episode.
        /// @return Current internal status value.
        /// @throws None (noexcept).
        status current_status() const noexcept { return status_; }

        /// @brief Returns how many original problem clauses have been registered.
        /// @return Number of clauses added via `add_problem_clause`.
        /// @throws None (noexcept).
        std::size_t original_clause_count() const noexcept { return original_clause_count_; }

        /// @brief Returns how many learned clauses have been registered in the clause database.
        /// @return Number of redundant clauses currently owned by the database.
        /// @throws None (noexcept).
        std::size_t learned_clause_count() const noexcept { return clause_database_.stats_snapshot().redundant_count; }

        /// @brief Extracts the internal-variable model after a satisfiable episode.
        /// @return Read-only span of internal model literals, to be translated by `model_reconstructor`.
        /// @throws None (noexcept).
        std::span<const literal> extract_internal_model() const noexcept { return internal_model_; }

        /// @brief Extracts the internal-variable failed core after an unsatisfiable episode under assumptions.
        /// @return Read-only span of internal failed-assumption literals, to be translated by `failed_core_extractor`.
        /// @throws None (noexcept).
        std::span<const literal> extract_failed_core() const noexcept { return failed_core_; }

        std::size_t proof_buffered_event_count() const noexcept { return proof_manager_.buffered_event_count(); }

        const proof::proof_event& last_proof_event() const noexcept { return proof_manager_.last_event(); }

        std::span<const proof::proof_event> buffered_proof_events() const noexcept { return proof_manager_.buffered_events(); }

        /// @brief Returns the latest outcome produced by the coordinator-backed search episode.
        /// @return Coordinator outcome for the most recent solve episode.
        /// @throws None (noexcept).
        search_coordinator::outcome current_search_outcome() const noexcept { return search_coordinator_.current_outcome(); }

        /// @brief Returns how many learned clauses were retained across completed epochs.
        /// @return Retained learned-clause count tracked by the incremental context.
        std::uint32_t retained_learned_clause_count() const noexcept { return incremental_context_.retained_learned_clauses(); }

        /// @brief Returns whether the latest solve call completed a transient-state reset.
        /// @return True if transient per-epoch state has been reset.
        bool transient_state_was_reset() const noexcept { return incremental_context_.transient_state_reset(); }

        /// @brief Returns whether a persistent option subset has been recorded.
        /// @return True if persistent option state was marked.
        bool persisted_option_subset() const noexcept { return incremental_context_.persisted_option_subset(); }

        /// @brief Marks the currently configured options as persistent across solve epochs.
        /// @throws None (noexcept).
        void persist_option_subset() noexcept { incremental_context_.persist_option_subset(); }

        /// @brief Returns how many clauses the subsumption pass removed.
        /// @return Subsumed clause count accumulated by the attached forward subsumer.
        std::size_t subsumed_clause_count() const noexcept { return forward_subsumer_.subsumed_count(); }

        /// @brief Returns how many preprocess pipeline runs have been executed.
        /// @return Number of preprocess runs.
        std::size_t preprocess_run_count() const noexcept { return preprocess_scheduler_.pipeline_run_count(); }

        /// @brief Returns how many inprocess epochs have been executed.
        /// @return Number of inprocess epochs.
        std::uint64_t inprocess_epoch_count() const noexcept { return inprocess_scheduler_.epoch_count(); }

        /// @brief Exposes the flush/restore policy manager for focused integration tests.
        /// @return Reference to the clause flush/restore manager.
        simplify::flush_restore_manager& flush_restore_manager() noexcept { return flush_restore_manager_; }

        /// @brief Attaches an external proof tracer to the core-owned proof manager.
        /// @param sink External proof tracer sink.
        void attach_proof_tracer(const proof::tracer::view& sink) noexcept { proof_manager_.register_tracer(sink); }

        /// @brief Returns whether any proof format is active through the core-owned proof manager.
        bool proof_enabled() const noexcept { return proof_manager_.has_enabled_formats(); }

        bool proof_checkers_valid() const noexcept { return proof_manager_.validate_checkers(); }

        const std::vector<simplify::scheduler::preprocess::pass_summary>& preprocess_last_reported_summaries() const noexcept
        {
            return preprocess_scheduler_.last_reported_summaries();
        }

        const std::vector<simplify::scheduler::inprocess::pass_summary>& inprocess_last_reported_summaries() const noexcept
        {
            return inprocess_scheduler_.last_reported_summaries();
        }

    private:
        void rebind_internal_views() noexcept
        {
            search_coordinator_.attach_database(clause_database_);
            flush_restore_manager_.attach_database(clause_database_);
            forward_subsumer_.attach_database(clause_database_);
            forward_subsumer_.attach_proof_manager(proof_manager_);
            preprocess_scheduler_.attach_memory_governor(memory_governor_);
            preprocess_scheduler_.attach_clause_database(clause_database_);
            preprocess_scheduler_.attach_watch_list(watch_list_);
            preprocess_scheduler_.attach_variable_mapper(variable_mapper_);
            preprocess_scheduler_.attach_proof_manager(proof_manager_);
            preprocess_scheduler_.attach_clause_sink([this](const clause::ref_t ref) noexcept { search_coordinator_.attach_clause(ref); });
            inprocess_scheduler_.attach_memory_governor(memory_governor_);
            inprocess_scheduler_.attach_clause_database(clause_database_);
            inprocess_scheduler_.attach_watch_list(watch_list_);
            inprocess_scheduler_.attach_variable_mapper(variable_mapper_);
            inprocess_scheduler_.attach_proof_manager(proof_manager_);
        }

        using assignment_vector = std::vector<std::int8_t>;
        using decision_level_vector = std::vector<std::uint32_t>;
        using reason_vector = std::vector<clause::ref_t>;

        static constexpr std::int8_t unassigned_value = -1;
        static constexpr std::int8_t false_value = 0;
        static constexpr std::int8_t true_value = 1;

        std::vector<clause::ref_t> active_clause_refs() const noexcept
        {
            const auto stats = clause_database_.stats_snapshot();
            std::vector<clause::ref_t> refs {};
            refs.reserve(stats.irredundant_count + stats.redundant_count);

            clause_database_.iterate_irredundant([&](const clause::ref_t ref) noexcept { refs.push_back(ref); });
            clause_database_.iterate_redundant([&](const clause::ref_t ref) noexcept { refs.push_back(ref); });

            return refs;
        }

        static bool contains_clause_id(const std::vector<proof::clause::id>& ids, const proof::clause::id id) noexcept
        {
            for (const auto existing: ids)
            {
                if (existing.equals(id))
                {
                    return true;
                }
            }
            return false;
        }

        static bool contains_clause_ref(const std::vector<clause::ref_t>& refs, const clause::ref_t ref) noexcept
        {
            for (const auto existing: refs)
            {
                if (existing.offset() == ref.offset())
                {
                    return true;
                }
            }
            return false;
        }

        std::vector<clause::ref_t> build_ordered_reason_chain_refs(const std::span<const literal> chain_literals,
                                                                   const reason_vector& reasons) const noexcept
        {
            std::vector<clause::ref_t> ordered_reason_refs {};

            for (const auto lit: chain_literals)
            {
                const auto variable_index = static_cast<std::size_t>(lit.variable_of().index());
                if (variable_index >= reasons.size())
                {
                    continue;
                }

                const auto reason_ref = reasons[variable_index];
                if (!reason_ref.valid() || contains_clause_ref(ordered_reason_refs, reason_ref))
                {
                    continue;
                }

                ordered_reason_refs.push_back(reason_ref);
            }

            return ordered_reason_refs;
        }

        std::vector<proof::clause::id> build_conflict_antecedents(const clause::ref_t conflict_ref,
                                                                  const std::span<const clause::ref_t> ordered_reason_refs) const noexcept
        {
            std::vector<proof::clause::id> antecedents {};

            const auto conflict_id = proof_manager_.stable_id_for_clause(conflict_ref);
            if (conflict_id.valid())
            {
                antecedents.push_back(conflict_id);
            }

            for (const auto reason_ref: ordered_reason_refs)
            {
                const auto reason_id = proof_manager_.stable_id_for_clause(reason_ref);
                if (!reason_id.valid() || contains_clause_id(antecedents, reason_id))
                {
                    continue;
                }
                antecedents.push_back(reason_id);
            }

            return antecedents;
        }

        std::uint32_t find_max_variable(const std::span<const literal> assumptions) const noexcept
        {
            std::uint32_t max_variable = 0;

            for (const auto ref: active_clause_refs())
            {
                for (const auto lit: clause_database_.storage_of().literals_of(ref))
                {
                    if (lit.variable_of().index() > max_variable)
                    {
                        max_variable = lit.variable_of().index();
                    }
                }
            }

            for (const auto lit: assumptions)
            {
                if (lit.variable_of().index() > max_variable)
                {
                    max_variable = lit.variable_of().index();
                }
            }

            return max_variable;
        }

        static bool assign_literal(assignment_vector& assignment, decision_level_vector& decision_levels, reason_vector& reasons,
                                   const literal lit, const std::uint32_t decision_level, const clause::ref_t reason_ref = {}) noexcept
        {
            const auto index = lit.variable_of().index();
            if (index >= assignment.size() || index >= reasons.size())
            {
                return false;
            }

            const std::int8_t required_value = lit.is_negated() ? false_value : true_value;
            const auto current_value = assignment[index];
            if (current_value == unassigned_value)
            {
                assignment[index] = required_value;
                decision_levels[index] = decision_level;
                reasons[index] = reason_ref;
                return true;
            }
            return current_value == required_value;
        }

        static bool literal_is_satisfied(const literal lit, const std::int8_t variable_value) noexcept
        {
            if (variable_value == unassigned_value)
            {
                return false;
            }
            if (lit.is_negated())
            {
                return variable_value == false_value;
            }
            return variable_value == true_value;
        }

        static bool conflict_limit_reached(const solve_request& request, const std::uint64_t conflicts) noexcept
        {
            return request.conflict_limit != 0 && conflicts > request.conflict_limit;
        }

        static bool decision_limit_reached(const solve_request& request, const std::uint64_t decisions) noexcept
        {
            return request.decision_limit != 0 && decisions > request.decision_limit;
        }

        status handle_clause_conflict(const clause::ref_t ref, const std::span<const literal> conflict_clause,
                                      const decision_level_vector& decision_levels, const reason_vector& reasons,
                                      const solve_request& request, std::uint64_t& conflicts) noexcept
        {
            (void) ref;

            const auto learned_clause_count_before = search_coordinator_.learned_clause_count();

            search_coordinator_.seed_conflict_clause(conflict_clause);
            for (const auto lit: conflict_clause)
            {
                const auto index = lit.variable_of().index();
                const auto level = index < decision_levels.size() ? decision_levels[index] : 0u;
                search_coordinator_.set_decision_level(lit.variable_of(), level);
            }
            search_coordinator_.handle_conflict();

            if (search_coordinator_.learned_clause_count() != learned_clause_count_before)
            {
                const auto learned_clause = search_coordinator_.last_learned_clause();
                if (!learned_clause.empty())
                {
                    const auto learned_ref = clause_database_.add_clause(learned_clause, true);
                    search_coordinator_.attach_clause(learned_ref);
                    const auto ordered_reason_refs =
                        build_ordered_reason_chain_refs(search_coordinator_.last_resolution_chain_literals(), reasons);
                    const auto antecedents = build_conflict_antecedents(ref, ordered_reason_refs);
                    proof_manager_.on_add_derived(learned_ref, learned_clause, antecedents);
                    incremental_context_.retain_learned_clause();
                }
            }

            ++conflicts;
            run_inprocess_if_due();

            if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
            {
                return status::unknown;
            }
            return conflict_limit_reached(request, conflicts) ? status::unknown : status::unsatisfiable;
        }

        void run_inprocess_if_due() noexcept
        {
            const auto inprocess_epochs_before = inprocess_scheduler_.epoch_count();
            inprocess_scheduler_.set_conflicts_seen(search_coordinator_.conflict_event_count());
            inprocess_scheduler_.set_restart_count(search_coordinator_.restart_count());
            inprocess_scheduler_.set_decisions_seen(search_coordinator_.decision_event_count());
            inprocess_scheduler_.run_epoch();
            if (inprocess_scheduler_.epoch_count() > inprocess_epochs_before)
            {
                inprocess_scheduler_.report_epoch_summary();
                search_coordinator_.notify_inprocess_epoch_completed(inprocess_scheduler_.last_structural_gain());
            }
        }

        status propagate_units(assignment_vector& assignment, decision_level_vector& decision_levels, reason_vector& reasons,
                               const solve_request& request, std::uint64_t& conflicts, const std::uint32_t current_level) noexcept
        {
            bool changed = true;
            while (changed)
            {
                changed = false;

                for (const auto ref: active_clause_refs())
                {
                    const auto clause = clause_database_.storage_of().literals_of(ref);
                    if (clause.empty())
                    {
                        ++conflicts;
                        return conflict_limit_reached(request, conflicts) ? status::unknown : status::unsatisfiable;
                    }

                    bool satisfied = false;
                    std::uint32_t unassigned_count = 0;
                    literal unit_literal {};

                    for (const auto lit: clause)
                    {
                        const auto index = lit.variable_of().index();
                        if (index >= assignment.size())
                        {
                            continue;
                        }

                        const auto variable_value = assignment[index];
                        if (literal_is_satisfied(lit, variable_value))
                        {
                            satisfied = true;
                            break;
                        }
                        if (variable_value == unassigned_value)
                        {
                            ++unassigned_count;
                            unit_literal = lit;
                        }
                    }

                    if (satisfied)
                    {
                        continue;
                    }

                    if (unassigned_count == 0)
                    {
                        return handle_clause_conflict(ref, clause, decision_levels, reasons, request, conflicts);
                    }

                    if (unassigned_count == 1)
                    {
                        if (!assign_literal(assignment, decision_levels, reasons, unit_literal, current_level, ref))
                        {
                            return handle_clause_conflict(ref, clause, decision_levels, reasons, request, conflicts);
                        }
                        changed = true;
                    }
                }
            }

            return status::satisfiable;
        }

        static std::uint32_t pick_unassigned_variable(const assignment_vector& assignment) noexcept
        {
            for (std::uint32_t index = 1; index < assignment.size(); ++index)
            {
                if (assignment[index] == unassigned_value)
                {
                    return index;
                }
            }
            return 0;
        }

        status solve_recursive(assignment_vector& assignment, decision_level_vector& decision_levels, reason_vector& reasons,
                               const solve_request& request, std::uint64_t& conflicts, const std::uint32_t current_level) noexcept
        {
            const auto propagation_status = propagate_units(assignment, decision_levels, reasons, request, conflicts, current_level);
            if (propagation_status != status::satisfiable)
            {
                return propagation_status;
            }

            const auto decision_variable = pick_unassigned_variable(assignment);
            if (decision_variable == 0)
            {
                return status::satisfiable;
            }

            const auto branch_literal = search_coordinator_.take_branch_literal(decision_variable);
            if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
            {
                return status::unknown;
            }
            if (!branch_literal.has_value())
            {
                return search_coordinator_.current_outcome() == search_coordinator::outcome::satisfiable ? status::satisfiable :
                                                                                                           status::unknown;
            }

            for (const auto candidate_literal: {branch_literal.value(), branch_literal->negated()})
            {
                auto branch_assignment = assignment;
                auto branch_levels = decision_levels;
                auto branch_reasons = reasons;

                if (!assign_literal(branch_assignment, branch_levels, branch_reasons, candidate_literal, current_level + 1u))
                {
                    continue;
                }

                const auto branch_status =
                    solve_recursive(branch_assignment, branch_levels, branch_reasons, request, conflicts, current_level + 1u);
                if (branch_status == status::satisfiable)
                {
                    assignment = std::move(branch_assignment);
                    decision_levels = std::move(branch_levels);
                    reasons = std::move(branch_reasons);
                    return status::satisfiable;
                }
                if (branch_status == status::unknown)
                {
                    return status::unknown;
                }
            }

            return status::unsatisfiable;
        }

        void build_internal_model(const assignment_vector& assignment) noexcept
        {
            internal_model_.clear();
            if (assignment.size() <= 1)
            {
                return;
            }

            internal_model_.reserve(assignment.size() - 1);
            for (std::uint32_t index = 1; index < assignment.size(); ++index)
            {
                const auto value = assignment[index];
                const bool negated = value == false_value;
                internal_model_.push_back(literal {variable {index}, negated});
            }
        }

        void synchronize_search_outcome(const status solve_status) noexcept
        {
            switch (solve_status)
            {
                case status::satisfiable:
                    search_coordinator_.handle_sat();
                    break;
                case status::unsatisfiable:
                    search_coordinator_.handle_unsat();
                    break;
                case status::unknown:
                default:
                    search_coordinator_.handle_termination();
                    break;
            }
        }

        clause::database clause_database_ {};
        search_coordinator search_coordinator_ {};
        incremental_context incremental_context_ {};
        memory_governor memory_governor_ {};
        bank::watch_list watch_list_ {};
        variable_mapper variable_mapper_ {};
        proof_manager proof_manager_ {};
        simplify::flush_restore_manager flush_restore_manager_ {};
        simplify::forward_subsumer forward_subsumer_ {};
        simplify::scheduler::preprocess preprocess_scheduler_ {};
        simplify::scheduler::inprocess inprocess_scheduler_ {};
        std::size_t original_clause_count_ {0};
        std::vector<literal> internal_model_ {};
        std::vector<literal> failed_core_ {};
        status status_ {status::unknown};
    };
}
