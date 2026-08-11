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
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/search_coordinator.hpp>
#include <kmx/sat/literal.hpp>
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
        solver_core() noexcept = default;

        /// @brief Runs one solve episode under the given request.
        /// @param request Solve configuration for this episode (assumptions, limits, mode flags).
        /// @return Internal-level terminal status for this episode.
        /// @throws None (noexcept).
        status solve(const solve_request& request) noexcept
        {
            internal_model_.clear();
            failed_core_.clear();

            search_coordinator_.apply_assumptions(request);

            const auto max_variable = find_max_variable(request.assumptions);
            assignment_vector assignment(static_cast<std::size_t>(max_variable + 1u), unassigned_value);
            decision_level_vector decision_levels(static_cast<std::size_t>(max_variable + 1u), 0u);

            for (const auto assumption : request.assumptions)
            {
                if (!assign_literal(assignment, decision_levels, assumption, 0u))
                {
                    status_ = status::unsatisfiable;
                    failed_core_ = request.assumptions;
                    synchronize_search_outcome(status_);
                    return status_;
                }
            }

            std::uint64_t conflicts = 0;
            std::uint64_t decisions = 0;
            status_ = solve_recursive(assignment, decision_levels, request, conflicts, decisions, 0u);

            if (status_ == status::satisfiable)
            {
                build_internal_model(assignment);
            }
            else if (status_ == status::unsatisfiable)
            {
                failed_core_ = request.assumptions;
            }

            synchronize_search_outcome(status_);
            return status_;
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
        status current_status() const noexcept
        {
            return status_;
        }

        /// @brief Returns how many original problem clauses have been registered.
        /// @return Number of clauses added via `add_problem_clause`.
        /// @throws None (noexcept).
        std::size_t original_clause_count() const noexcept
        {
            return original_clause_count_;
        }

        /// @brief Returns how many learned clauses have been registered in the clause database.
        /// @return Number of redundant clauses currently owned by the database.
        /// @throws None (noexcept).
        std::size_t learned_clause_count() const noexcept
        {
            return clause_database_.stats_snapshot().redundant_count;
        }

        /// @brief Extracts the internal-variable model after a satisfiable episode.
        /// @return Read-only span of internal model literals, to be translated by `model_reconstructor`.
        /// @throws None (noexcept).
        std::span<const literal> extract_internal_model() const noexcept
        {
            return internal_model_;
        }

        /// @brief Extracts the internal-variable failed core after an unsatisfiable episode under assumptions.
        /// @return Read-only span of internal failed-assumption literals, to be translated by `failed_core_extractor`.
        /// @throws None (noexcept).
        std::span<const literal> extract_failed_core() const noexcept
        {
            return failed_core_;
        }

        /// @brief Returns the latest outcome produced by the coordinator-backed search episode.
        /// @return Coordinator outcome for the most recent solve episode.
        /// @throws None (noexcept).
        search_coordinator::outcome current_search_outcome() const noexcept
        {
            return search_coordinator_.current_outcome();
        }

    private:
        using assignment_vector = std::vector<std::int8_t>;
        using decision_level_vector = std::vector<std::uint32_t>;

        static constexpr std::int8_t unassigned_value = -1;
        static constexpr std::int8_t false_value = 0;
        static constexpr std::int8_t true_value = 1;

        std::vector<clause::ref_t> active_clause_refs() const noexcept
        {
            const auto stats = clause_database_.stats_snapshot();
            std::vector<clause::ref_t> refs {};
            refs.reserve(stats.irredundant_count + stats.redundant_count);

            clause_database_.iterate_irredundant([&](const clause::ref_t ref) noexcept {
                refs.push_back(ref);
            });
            clause_database_.iterate_redundant([&](const clause::ref_t ref) noexcept {
                refs.push_back(ref);
            });

            return refs;
        }

        std::uint32_t find_max_variable(const std::span<const literal> assumptions) const noexcept
        {
            std::uint32_t max_variable = 0;

            for (const auto ref : active_clause_refs())
            {
                for (const auto lit : clause_database_.storage_of().literals_of(ref))
                {
                    if (lit.variable_of().index() > max_variable)
                    {
                        max_variable = lit.variable_of().index();
                    }
                }
            }

            for (const auto lit : assumptions)
            {
                if (lit.variable_of().index() > max_variable)
                {
                    max_variable = lit.variable_of().index();
                }
            }

            return max_variable;
        }

        static bool assign_literal(
            assignment_vector& assignment,
            decision_level_vector& decision_levels,
            const literal lit,
            const std::uint32_t decision_level) noexcept
        {
            const auto index = lit.variable_of().index();
            if (index >= assignment.size())
            {
                return false;
            }

            const std::int8_t required_value = lit.is_negated() ? false_value : true_value;
            const auto current_value = assignment[index];
            if (current_value == unassigned_value)
            {
                assignment[index] = required_value;
                decision_levels[index] = decision_level;
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

        status handle_clause_conflict(
            const clause::ref_t ref,
            const std::span<const literal> conflict_clause,
            const decision_level_vector& decision_levels,
            const solve_request& request,
            std::uint64_t& conflicts) noexcept
        {
            (void)ref;

            const auto learned_clause_count_before = search_coordinator_.learned_clause_count();

            search_coordinator_.seed_conflict_clause(conflict_clause);
            for (const auto lit : conflict_clause)
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
                }
            }

            ++conflicts;
            if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
            {
                return status::unknown;
            }
            return conflict_limit_reached(request, conflicts) ? status::unknown : status::unsatisfiable;
        }

        status propagate_units(
            assignment_vector& assignment,
            decision_level_vector& decision_levels,
            const solve_request& request,
            std::uint64_t& conflicts,
            const std::uint32_t current_level) noexcept
        {
            bool changed = true;
            while (changed)
            {
                changed = false;

                for (const auto ref : active_clause_refs())
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

                    for (const auto lit : clause)
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
                        return handle_clause_conflict(ref, clause, decision_levels, request, conflicts);
                    }

                    if (unassigned_count == 1)
                    {
                        if (!assign_literal(assignment, decision_levels, unit_literal, current_level))
                        {
                            return handle_clause_conflict(ref, clause, decision_levels, request, conflicts);
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

        status solve_recursive(
            assignment_vector& assignment,
            decision_level_vector& decision_levels,
            const solve_request& request,
            std::uint64_t& conflicts,
            std::uint64_t& decisions,
            const std::uint32_t current_level) noexcept
        {
            const auto propagation_status = propagate_units(assignment, decision_levels, request, conflicts, current_level);
            if (propagation_status != status::satisfiable)
            {
                return propagation_status;
            }

            const auto decision_variable = pick_unassigned_variable(assignment);
            if (decision_variable == 0)
            {
                return status::satisfiable;
            }

            ++decisions;
            if (decision_limit_reached(request, decisions))
            {
                return status::unknown;
            }

            const auto branch_literal = search_coordinator_.next_branch_literal(decision_variable);
            if (!branch_literal.has_value())
            {
                return status::satisfiable;
            }

            for (const auto candidate_literal : {branch_literal.value(), branch_literal->negated()})
            {
                auto branch_assignment = assignment;
                auto branch_levels = decision_levels;

                if (!assign_literal(branch_assignment, branch_levels, candidate_literal, current_level + 1u))
                {
                    continue;
                }

                const auto branch_status = solve_recursive(
                    branch_assignment,
                    branch_levels,
                    request,
                    conflicts,
                    decisions,
                    current_level + 1u);
                if (branch_status == status::satisfiable)
                {
                    assignment = std::move(branch_assignment);
                    decision_levels = std::move(branch_levels);
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
        std::size_t original_clause_count_ {0};
        std::vector<literal> internal_model_ {};
        std::vector<literal> failed_core_ {};
        status status_ {status::unknown};
    };
}
