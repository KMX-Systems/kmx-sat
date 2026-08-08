/// @file inc/kmx/sat/cdcl/clause/learner.hpp
/// @brief Single entry point for all clauses derived from conflict analysis.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <span>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::clause
{
    /// @brief Single entry point for all clauses derived from conflict analysis.
    ///
    /// `clause::learner` is the funnel every clause produced by `conflict_analyzer`/`clause::minimizer` passes
    /// through before it becomes part of `clause::database`: `learn_clause` handles the general case, while
    /// `register_unit`/`register_binary`/`register_large` provide specialized fast paths matching CaDiCaL/Kissat's
    /// convention of treating unit and binary clauses without full arena-allocated storage where possible.
    /// `assign_asserting_literal` performs the implied assignment of the derived clause's asserting (1-UIP) literal
    /// at the computed backjump level immediately after `engine::backtrack` unwinds, completing the conflict-driven
    /// learning cycle before propagation resumes.
    class learner final
    {
    public:
        /// @brief Constructs a clause learner with no pending clause.
        /// @throws None (noexcept).
        learner() noexcept = default;

        /// @brief Registers a general learned clause with the clause database.
        /// @param literals Literals composing the learned clause.
        /// @return Reference to the newly registered clause.
        /// @throws None (noexcept).
        ref_t learn_clause(const std::span<const literal> literals) noexcept
        {
            return {};
        }

        /// @brief Registers a learned unit clause using the specialized unit fast path.
        /// @param lit The single literal of the unit clause.
        /// @throws None (noexcept).
        void register_unit(const literal lit) noexcept
        {
        }

        /// @brief Registers a learned binary clause using the specialized binary fast path.
        /// @param first First literal of the binary clause.
        /// @param second Second literal of the binary clause.
        /// @throws None (noexcept).
        void register_binary(const literal first, const literal second) noexcept
        {
        }

        /// @brief Registers a learned clause with three or more literals.
        /// @param literals Literals composing the learned clause.
        /// @return Reference to the newly registered clause.
        /// @throws None (noexcept).
        ref_t register_large(const std::span<const literal> literals) noexcept
        {
            return {};
        }

        /// @brief Assigns the derived clause's asserting literal at the post-backjump decision level.
        /// @param lit Asserting literal to assign.
        /// @throws None (noexcept).
        void assign_asserting_literal(const literal lit) noexcept
        {
        }
    };
}
