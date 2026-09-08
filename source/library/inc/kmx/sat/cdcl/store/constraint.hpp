/// @file inc/kmx/sat/cdcl/store/constraint.hpp
/// @brief Support for temporary constraints or incremental clauses.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <optional>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::store
{
    /// @brief A view over a constraint's literals, absent when the constraint is unknown.
    using optional_literal_span_t = std::optional<std::span<const literal>>;

    /// @brief Support for temporary constraints or incremental clauses.
    /// @details
    /// Some incremental workflows (for example IPASIR-UP-style external propagators, or CaDiCaL's single "constraint"
    /// clause feature) need one extra clause that is scoped to a single solve episode without being permanently
    /// added to the clause database. `store::constraint` holds that at most one clause between
    /// `set_constraint_clause` (set before solving) and `clear_constraint_clause` (cleared after the episode ends),
    /// so `search_coordinator` can treat it like any other clause during propagation while `proof_manager` can decide
    /// its proof-event visibility independently of ordinary clauses, per the temporary-constraint input-validation
    /// rules.
    class constraint final
    {
    public:
        /// @brief Constructs a constraint store with no active constraint clause.
        /// @throws None (noexcept).
        constraint() noexcept = default;

        /// @brief Sets the temporary constraint clause for the upcoming solve episode.
        /// @param clause Read-only span containing the constraint clause literals.
        /// @throws None (noexcept).
        void set_constraint_clause(const std::span<const literal> clause) noexcept
        {
            clause_.assign(clause.begin(), clause.end());
            has_clause_ = true;
        }

        /// @brief Clears the currently active constraint clause, for example at the end of a solve episode.
        /// @throws None (noexcept).
        void clear_constraint_clause() noexcept
        {
            clause_.clear();
            has_clause_ = false;
        }

        /// @brief Checks whether a constraint clause is currently active.
        /// @return True if a constraint clause has been set and not yet cleared.
        /// @throws None (noexcept).
        bool has_constraint_clause() const noexcept { return has_clause_; }

        /// @brief Returns a read-only reference to the currently active constraint clause, if any.
        /// @return The constraint clause literals, or `std::nullopt` when none is active.
        /// @throws None (noexcept).
        optional_literal_span_t constraint_clause_ref() const noexcept;

    private:
        std::vector<literal> clause_ {};
        bool has_clause_ {};
    };
}
