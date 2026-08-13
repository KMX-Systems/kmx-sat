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
    /// @details
    /// `watch` records the clause reference plus the blocking literal used by the two-watched-literal propagator.
    /// For binary clauses, `is_binary`/`binary_literal` provide the fast path where no full clause fetch is needed.
    class watch final
    {
    public:
        watch() noexcept = default;

        watch(const literal blocking, const clause::ref_t clause_ref, const bool is_binary = false) noexcept:
            blocking_literal_ {blocking},
            clause_ref_ {clause_ref},
            is_binary_ {is_binary}
        {
        }

        literal blocking_literal() const noexcept { return blocking_literal_; }

        bool is_binary() const noexcept { return is_binary_; }

        literal binary_literal() const noexcept { return binary_literal_; }

        clause::ref_t clause_ref() const noexcept { return clause_ref_; }

        void set_binary_literal(const literal lit) noexcept { binary_literal_ = lit; }

        /// @brief Compares two watch entries by the clause reference they identify.
        /// @return True if both entries refer to the same clause.
        /// @throws None (noexcept).
        bool operator==(const watch& other) const noexcept { return clause_ref_ == other.clause_ref_; }

    private:
        literal blocking_literal_ {};
        literal binary_literal_ {};
        clause::ref_t clause_ref_ {};
        bool is_binary_ {};
    };
}
