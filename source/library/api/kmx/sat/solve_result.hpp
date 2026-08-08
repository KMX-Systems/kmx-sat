/// @file api/kmx/sat/solve_result.hpp
/// @brief Unified output object for SAT, UNSAT, and terminated/unknown states.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/failed_core_view.hpp>
#include <kmx/sat/model_view.hpp>
#include <kmx/sat/telemetry/solver_statistics.hpp>

namespace kmx::sat
{
    /// @brief Unified output object for SAT, UNSAT, and terminated/unknown states.
    ///
    /// `solve_result` is the single return type of `solver::solve`, covering every terminal outcome of one episode so
    /// callers never need to branch on separate SAT/UNSAT return types: a satisfiable outcome exposes `model()`, an
    /// unsatisfiable outcome under assumptions exposes `failed_core()`, and every outcome carries a statistics
    /// snapshot and a proof-activity summary. `status::terminated` additionally covers the defined terminal outcome
    /// produced when the hard memory ceiling in `memory_governor` cannot be resolved through shedding, and
    /// `status::unknown` covers conflict/decision-limit exhaustion from `solve_request`.
    class solve_result final
    {
    public:
        /// @brief Enumerates the terminal outcomes a solve episode can produce.
        enum class status
        {
            /// @brief The formula (with active assumptions) is satisfiable; `model()` is populated.
            satisfiable,
            /// @brief The formula (with active assumptions) is unsatisfiable; `failed_core()` is populated when
            /// assumptions were active.
            unsatisfiable,
            /// @brief The episode ended without a definite result, for example due to `conflict_limit` or
            /// `decision_limit` exhaustion, or an external termination callback returning true.
            unknown,
            /// @brief The episode was stopped by the runtime itself (for example an unrecoverable resource ceiling),
            /// as distinct from a caller-requested/limit-based `unknown` outcome.
            terminated
        };

        /// @brief Minimal summary of proof activity for one solve episode.
        ///
        /// Reflects only whether proof tracing and/or online checking were active for this episode; the concrete
        /// proof byte stream, if any, is delivered separately through the attached `proof::tracer::view` sink rather
        /// than through this result object.
        struct proof_summary final
        {
            /// @brief True when at least one proof tracer format was active for this episode.
            bool proof_enabled {};
            /// @brief True when an online/forward proof checker validated the derivations of this episode.
            bool proof_checked {};
        };

        /// @brief Constructs a default solve result with unknown status.
        /// @throws None (noexcept).
        solve_result() noexcept = default;

        /// @brief Returns the terminal status produced by the last solve episode.
        /// @return Current solve status value.
        /// @throws None (noexcept).
        status status_of() const noexcept
        {
            return status_;
        }

        /// @brief Returns the exported model view when satisfiable.
        /// @return Read-only model view object.
        /// @throws None (noexcept).
        model_view model() const noexcept
        {
            return model_;
        }

        /// @brief Returns the failed-core view when unsatisfiable under assumptions.
        /// @return Read-only failed-core view object.
        /// @throws None (noexcept).
        failed_core_view failed_core() const noexcept
        {
            return failed_core_;
        }

        /// @brief Returns the statistics snapshot captured for this result.
        /// @return Immutable statistics snapshot.
        /// @throws None (noexcept).
        telemetry::solver_statistics::snapshot statistics_snapshot() const noexcept
        {
            return statistics_snapshot_;
        }

        /// @brief Returns a compact summary of proof generation/checking activity.
        /// @return Proof summary record for this solve result.
        /// @throws None (noexcept).
        proof_summary proof_summary_of() const noexcept
        {
            return proof_summary_;
        }

    private:
        status status_ {status::unknown};
        model_view model_ {};
        failed_core_view failed_core_ {};
        telemetry::solver_statistics::snapshot statistics_snapshot_ {};
        proof_summary proof_summary_ {};
    };
}
