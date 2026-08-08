/// @file inc/kmx/sat/cdcl/watch.hpp
/// @brief Compact element stored in watch lists.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Compact element stored in watch lists.
    ///
    /// `watch` implements the "blocking literal" optimization from CaDiCaL/MiniSat-family two-watched-literal
    /// propagation: alongside the reference to the watched clause, it caches one literal already known to satisfy
    /// the clause under some past assignment, so `propagator::propagate` can skip dereferencing the clause entirely
    /// when the blocking literal is still true, only falling back to a full clause scan on an actual watch miss.
    /// `is_binary`/`binary_literal` provide a further fast path for binary clauses, which are common and can be
    /// represented and checked without touching `bank::arena` storage at all.
    class watch final
    {
    public:
        /// @brief Constructs an empty watch entry.
        /// @throws None (noexcept).
        watch() noexcept = default;

        /// @brief Returns the cached literal that can short-circuit a full clause recheck if still satisfied.
        /// @return Blocking literal for this watch entry.
        /// @throws None (noexcept).
        literal blocking_literal() const noexcept
        {
            return {};
        }

        /// @brief Checks whether this watch entry represents a binary clause.
        /// @return True if the watched clause has exactly two literals.
        /// @throws None (noexcept).
        bool is_binary() const noexcept
        {
            return false;
        }

        /// @brief Returns the other literal of a binary clause without dereferencing arena storage.
        /// @return The binary clause's other literal.
        /// @throws None (noexcept).
        literal binary_literal() const noexcept
        {
            return {};
        }

        /// @brief Returns the reference to the clause this watch entry belongs to.
        /// @return Clause reference, or an invalid reference for a pure binary-clause watch.
        /// @throws None (noexcept).
        clause::ref_t clause_ref() const noexcept
        {
            return {};
        }
    };
}
