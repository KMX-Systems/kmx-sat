/// @file inc/kmx/sat/cdcl/solver_core.hpp
/// @brief The main internal solver container, but without degenerating back into an opaque monolith.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <optional>
    #include <span>
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

        /// @brief Runs one solve episode under the given request, driving `search_coordinator::run_search_epoch`.
        /// @param request Solve configuration for this episode (assumptions, limits, mode flags).
        /// @return Internal-level terminal status for this episode.
        /// @throws None (noexcept).
        status solve(const solve_request& request) noexcept
        {
            return status::unknown;
        }

        /// @brief Runs one solve episode restricted to the given internal assumption literals.
        /// @param assumptions Internal assumption literals for this episode.
        /// @return Internal-level terminal status for this episode.
        /// @throws None (noexcept).
        status solve_under_assumptions(const std::span<const literal> assumptions) noexcept
        {
            return status::unknown;
        }

        /// @brief Registers an original (non-redundant) problem clause with the internal clause database.
        /// @param literals Internal literals composing the clause.
        /// @throws None (noexcept).
        void add_problem_clause(const std::span<const literal> literals) noexcept
        {
        }

        /// @brief Returns the terminal status of the most recently completed solve episode.
        /// @return Current internal status value.
        /// @throws None (noexcept).
        status current_status() const noexcept
        {
            return status_;
        }

        /// @brief Extracts the internal-variable model after a satisfiable episode.
        /// @return Read-only span of internal model literals, to be translated by `model_reconstructor`.
        /// @throws None (noexcept).
        std::span<const literal> extract_internal_model() const noexcept
        {
            return {};
        }

        /// @brief Extracts the internal-variable failed core after an unsatisfiable episode under assumptions.
        /// @return Read-only span of internal failed-assumption literals, to be translated by `failed_core_extractor`.
        /// @throws None (noexcept).
        std::span<const literal> extract_failed_core() const noexcept
        {
            return {};
        }

    private:
        clause::database clause_database_ {};
        search_coordinator search_coordinator_ {};
        status status_ {status::unknown};
    };
}
