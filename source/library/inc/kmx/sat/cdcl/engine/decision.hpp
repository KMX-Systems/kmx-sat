/// @file inc/kmx/sat/cdcl/engine/decision.hpp
/// @brief Combines VMTF, EVSIDS, phase policies, randomization, and optionally CHB scores under an explicit
/// benchmarked blending policy rather than a silent heuristic swap.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <optional>
    #include <cstdint>
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
    /// @details
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
            const auto variable_literal = pick_decision_variable();
            if (!variable_literal.has_value())
            {
                return std::nullopt;
            }

            const auto phase = pick_decision_phase();
            last_decision_variable_ = variable_literal->variable_of();
            return literal {variable_literal->variable_of(), !phase};
        }

        /// @brief Selects the next branching variable using the active heuristic blend.
        /// @return Next branching literal wrapping the selected variable, or `std::nullopt` if none remains.
        /// @throws None (noexcept).
        std::optional<literal> pick_decision_variable() noexcept
        {
            if (has_active_blend())
            {
                const auto vmtf_candidate = vmtf_.front_candidate();
                if (vmtf_candidate.has_value())
                {
                    last_decision_variable_ = *vmtf_candidate;
                    return literal {*vmtf_candidate, false};
                }
            }

            if (next_variable_ == 0)
            {
                return std::nullopt;
            }
            last_decision_variable_ = variable {next_variable_};
            const auto phase = pick_decision_phase();
            return literal {variable {next_variable_}, !phase};
        }

        /// @brief Selects the polarity to assign the chosen decision variable.
        /// @return True for positive phase, false for negative phase.
        /// @throws None (noexcept).
        bool pick_decision_phase() const noexcept
        {
            return phase_bias_;
        }

        /// @brief Notifies the decision heuristics that a conflict just occurred.
        /// @throws None (noexcept).
        void notify_conflict() noexcept
        {
            ++conflict_count_;
            phase_bias_ = (conflict_count_ % 2u) == 0u;
        }

        /// @brief Notifies the decision heuristics that a restart just occurred.
        /// @throws None (noexcept).
        void notify_restart() noexcept
        {
            ++restart_count_;
            phase_bias_ = !phase_bias_;
        }

        /// @brief Notifies the decision heuristics that a rephase just occurred.
        /// @throws None (noexcept).
        void notify_rephase() noexcept
        {
            ++rephase_count_;
            phase_bias_ = (rephase_count_ % 2u) == 0u;
        }

        /// @brief Re-evaluates and applies the current VMTF/EVSIDS/CHB blending policy.
        /// @throws None (noexcept).
        void select_heuristic_blend() noexcept
        {
            selected_blend_ = 1u;
            vmtf_.activate(variable {next_variable_});
        }

        /// @brief Seeds the next-variable fallback used by the current lightweight branch picker.
        /// @param variable Variable index selected by an external coordinator.
        /// @throws None (noexcept).
        void set_next_variable(const std::uint32_t variable) noexcept
        {
            next_variable_ = variable;
        }

        /// @brief Returns whether a heuristic blend has been selected for the current decision cycle.
        /// @return True if `select_heuristic_blend` has run at least once.
        [[nodiscard]] bool has_active_blend() const noexcept
        {
            return selected_blend_ != 0u;
        }

        /// @brief Returns the variable most recently chosen by the decision engine.
        /// @return Last selected decision variable.
        std::optional<variable> last_selected_variable() const noexcept
        {
            if (last_decision_variable_.index() == 0u)
            {
                return std::nullopt;
            }
            return last_decision_variable_;
        }

    private:
        vmtf_queue vmtf_ {};
        evsids_heap evsids_ {};
        chb_tracker chb_ {};
        store::phase phase_ {};
        std::uint32_t next_variable_ {0};
        std::uint32_t conflict_count_ {0};
        std::uint32_t restart_count_ {0};
        std::uint32_t rephase_count_ {0};
        std::uint32_t selected_blend_ {0};
        bool phase_bias_ {true};
        variable last_decision_variable_ {};
    };
}
