/// @file api/kmx/sat/failed_core_view.hpp
/// @brief Read-only view over the failed-assumptions core produced by an unsatisfiable incremental solve.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <span>
#endif
#include <kmx/sat/literal.hpp>

namespace kmx::sat
{
    /// @brief Read-only view over the failed-assumptions core produced by an unsatisfiable incremental solve.
    ///
    /// `failed_core_view` is a non-owning span wrapper returned by `solve_result::failed_core()`; it exposes the
    /// minimal subset of the active `solve_request::assumptions` that `failed_core_extractor::build_failed_core`
    /// determined to be jointly responsible for the unsatisfiable outcome, following analysis by `conflict_analyzer`
    /// against the assumption-level conflict. It is only meaningful when `solve_result::status_of()` is
    /// `solve_result::status::unsatisfiable` and assumptions were active for that episode; `assumption_store` keeps
    /// assumptions strictly separate from the permanent clause database so this core never references original
    /// problem clauses directly.
    /// @warning The referenced span is only valid for the lifetime of the `solve_result` (or underlying solver state)
    /// that produced it; it must not be retained past a subsequent `solver::solve` or `solver::reset_session` call.
    class failed_core_view final
    {
    public:
        /// @brief Constructs an empty failed-core view.
        /// @throws None (noexcept).
        failed_core_view() noexcept = default;
        /// @brief Constructs a failed-core view from an assumption-literal span.
        /// @param assumptions Read-only span representing the extracted failed assumptions.
        /// @throws None (noexcept).
        explicit failed_core_view(const std::span<const literal> assumptions) noexcept : assumptions_ {assumptions}
        {
        }

        /// @brief Exposes the underlying failed-assumption span.
        /// @return Read-only span over failed assumptions.
        /// @throws None (noexcept).
        std::span<const literal> assumptions() const noexcept
        {
            return assumptions_;
        }

    private:
        std::span<const literal> assumptions_ {};
    };
}
