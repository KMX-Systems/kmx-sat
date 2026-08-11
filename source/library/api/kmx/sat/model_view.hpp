/// @file api/kmx/sat/model_view.hpp
/// @brief Read-only view over the stable external model produced by a satisfiable solve.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <span>
#endif
#include <kmx/sat/literal.hpp>

namespace kmx::sat
{
    /// @brief Read-only view over the stable external model produced by a satisfiable solve.
    ///
    /// @details
    /// `model_view` is a non-owning span wrapper returned by `solve_result::model()`; it exposes the model built by
    /// `external_frontend::build_model_view()` from `model_reconstructor::reconstruct_full_model`, after eliminated
    /// variables (BVE/BCE/factoring) have been reconstructed and strictly internal-only variables introduced by
    /// `factorizer`/`gate_extractor` have been dropped by `model_reconstructor::drop_internal_only_variables`. Only
    /// externally visible variables ever appear here; internal variable identity is never exposed through this view.
    /// @warning The referenced span is only valid for the lifetime of the `solve_result` (or underlying solver state)
    /// that produced it; it must not be retained past a subsequent `solver::solve` or `solver::reset_session` call.
    class model_view final
    {
    public:
        /// @brief Constructs an empty model view.
        /// @throws None (noexcept).
        model_view() noexcept = default;
        /// @brief Constructs a model view from an external-value span.
        /// @param values Read-only span of model literals exported by the solver.
        /// @throws None (noexcept).
        explicit model_view(const std::span<const literal> values) noexcept : values_ {values}
        {
        }

        /// @brief Exposes the underlying model-literal span.
        /// @return Read-only span over model literals.
        /// @throws None (noexcept).
        std::span<const literal> values() const noexcept
        {
            return values_;
        }

    private:
        std::span<const literal> values_ {};
    };
}
