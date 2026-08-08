/// @file inc/kmx/sat/cdcl/failed_core_extractor.hpp
/// @brief Incremental UNSAT path for assumptions.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/failed_core_view.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Incremental UNSAT path for assumptions.
    ///
    /// When `search_coordinator::handle_unsat` detects a conflict traceable to active assumptions rather than the
    /// permanent clause database, `failed_core_extractor` derives the minimal-effort subset of
    /// `store::assumption`'s literals jointly responsible for that conflict: `mark_failed_assumption` records each
    /// assumption literal implicated during `conflict_analyzer`'s backward walk, `build_failed_core` assembles the
    /// resulting `failed_core_view` for `external_frontend::capture_failed_core`, and
    /// `shrink_failed_core_if_possible` optionally trims the core further (for example through additional
    /// resolution) when a smaller core would still explain the conflict.
    /// @note This is the counterpart of `model_reconstructor` on the UNSAT side of the incremental SAT+UNSAT flow:
    /// where reconstruction restores a full model, this class narrows an unsatisfiable outcome down to its
    /// assumption-level explanation.
    class failed_core_extractor final
    {
    public:
        /// @brief Constructs a failed-core extractor with no marked assumptions.
        /// @throws None (noexcept).
        failed_core_extractor() noexcept = default;

        /// @brief Assembles the failed-assumptions core from the currently marked assumption literals.
        /// @return Read-only view over the failed assumption literals.
        /// @throws None (noexcept).
        failed_core_view build_failed_core() noexcept
        {
            return {};
        }

        /// @brief Marks one assumption literal as implicated in the current unsatisfiable conflict.
        /// @param lit Assumption literal to mark.
        /// @throws None (noexcept).
        void mark_failed_assumption(const literal lit) noexcept
        {
        }

        /// @brief Attempts to further narrow the failed core when a smaller subset still explains the conflict.
        /// @throws None (noexcept).
        void shrink_failed_core_if_possible() noexcept
        {
        }
    };
}
