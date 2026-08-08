/// @file inc/kmx/sat/cdcl/engine/decision.hpp
/// @brief Combines VMTF, EVSIDS, phase policies, randomization, and optionally chb_tracker scores under an explicit,
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <optional>
#endif
#include <kmx/sat/cdcl/chb_tracker.hpp>
#include <kmx/sat/cdcl/evsids_heap.hpp>
#include <kmx/sat/cdcl/store/phase.hpp>
#include <kmx/sat/cdcl/vmtf_queue.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/telemetry/solver_options.hpp>

namespace kmx::sat::cdcl::engine
{
    /// @brief Combines VMTF, EVSIDS, phase policies, randomization, and optionally chb_tracker scores under an explicit,
    /// benchmarked blending policy rather than a silent heuristic swap.
    ///
    /// `engine::decision` is the single point `search_coordinator` calls to obtain the next branching literal:
    /// `pick_decision_variable` selects the variable (currently querying `vmtf_` and `evsids_` per
    /// `select_heuristic_blend`'s policy, with `chb_` as an optional research-track third input),
    /// `pick_decision_phase` selects its polarity from `phase_` (saved/best/target as appropriate for the active
    /// rephasing state), and `pick_branch_literal` composes both into the literal handed to
    /// `stack::decision_frame::push_frame`. `notify_conflict`/`notify_restart`/`notify_rephase` let the owning
    /// heuristics react to search-mode transitions signaled by `controller::restart`/`controller::rephase`.
    /// @note Per the architecture's composition strategy, blending between VMTF, EVSIDS, and CHB must follow an
    /// explicit, benchmarked policy in `select_heuristic_blend` rather than an undocumented runtime heuristic swap;
    /// any change to that policy must be validated on the comparative benchmarking harness.
    class decision final
    {
    public:
        /// @brief Constructs a decision engine with empty VMTF/EVSIDS/CHB state and a default phase store.
        /// @throws None (noexcept).
        decision() noexcept = default;

        /// @brief Selects the next branching literal by combining variable and phase selection.
        /// @return Next branching literal, or `std::nullopt` if no unassigned variable remains.
        /// @throws None (noexcept).
        std::optional<literal> pick_branch_literal() noexcept
        {
            return std::nullopt;
        }

        /// @brief Selects the next branching variable using the active heuristic blend.
        /// @return Next branching literal wrapping the selected variable, or `std::nullopt` if none remains.
        /// @throws None (noexcept).
        std::optional<literal> pick_decision_variable() noexcept
        {
            return std::nullopt;
        }

        /// @brief Selects the polarity to assign the chosen decision variable.
        /// @return True for positive phase, false for negative phase.
        /// @throws None (noexcept).
        bool pick_decision_phase() const noexcept
        {
            return false;
        }

        /// @brief Notifies the decision heuristics that a conflict just occurred.
        /// @throws None (noexcept).
        void notify_conflict() noexcept
        {
        }

        /// @brief Notifies the decision heuristics that a restart just occurred.
        /// @throws None (noexcept).
        void notify_restart() noexcept
        {
        }

        /// @brief Notifies the decision heuristics that a rephase just occurred.
        /// @throws None (noexcept).
        void notify_rephase() noexcept
        {
        }

        /// @brief Re-evaluates and applies the current VMTF/EVSIDS/CHB blending policy.
        /// @throws None (noexcept).
        void select_heuristic_blend() noexcept
        {
        }

    private:
        vmtf_queue vmtf_ {};
        evsids_heap evsids_ {};
        chb_tracker chb_ {};
        store::phase phase_ {};
    };
}
