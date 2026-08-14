/// @file inc/kmx/sat/cdcl/engine/decision.hpp
/// @brief Combines VMTF, EVSIDS, phase policies, randomization, and optionally CHB scores under an explicit
/// benchmarked blending policy rather than a silent heuristic swap.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <optional>
    #include <span>
    #include <vector>
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
        using selectable_predicate_t = bool (*)(variable, const void*) noexcept;

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
                return {};

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
                if (chb_enabled_)
                {
                    const auto chb_candidate = chb_.best_candidate([this](const variable var) noexcept { return is_selectable(var); });
                    if (chb_candidate.has_value())
                    {
                        last_decision_variable_ = chb_candidate.value();
                        return literal {chb_candidate.value(), false};
                    }
                }

                for (;;)
                {
                    const auto vmtf_candidate = vmtf_.front_candidate();
                    if (!vmtf_candidate.has_value())
                        break;
                    if (is_selectable(*vmtf_candidate))
                    {
                        last_decision_variable_ = *vmtf_candidate;
                        return literal {*vmtf_candidate, false};
                    }
                    vmtf_.remove(*vmtf_candidate);
                }

                for (;;)
                {
                    const auto evsids_candidate = evsids_.extract_best();
                    if (!evsids_candidate.has_value())
                        break;
                    if (is_selectable(*evsids_candidate))
                    {
                        last_decision_variable_ = *evsids_candidate;
                        return literal {*evsids_candidate, false};
                    }
                }
            }

            if (next_variable_ == 0 || !is_selectable(variable {next_variable_}))
                return {};
            last_decision_variable_ = variable {next_variable_};
            return literal {variable {next_variable_}, false};
        }

        /// @brief Selects the polarity to assign the chosen decision variable.
        /// @return True for positive phase, false for negative phase.
        /// @throws None (noexcept).
        bool pick_decision_phase() const noexcept
        {
            if (last_decision_variable_.index() != 0u && has_saved_phase(last_decision_variable_))
                return phase_.saved_phase(last_decision_variable_);
            return phase_bias_;
        }

        /// @brief Notifies the decision heuristics that a conflict just occurred.
        /// @throws None (noexcept).
        void notify_conflict() noexcept
        {
            ++conflict_count_;
            phase_bias_ = (conflict_count_ % 2u) == 0u;
            selected_blend_ = 1u;

            if (conflict_maintenance_interval_ != 0u && (conflict_count_ % conflict_maintenance_interval_) == 0u)
            {
                evsids_.rescale();
                ++evsids_rescale_count_;
            }

            if (chb_decay_interval_ != 0u && (conflict_count_ % chb_decay_interval_) == 0u)
            {
                chb_.decay_step();
                ++chb_decay_count_;
            }

            if (last_decision_variable_.index() != 0u)
            {
                const auto inverted_saved_phase =
                    has_saved_phase(last_decision_variable_) ? !phase_.saved_phase(last_decision_variable_) : phase_bias_;
                phase_.set_saved_phase(last_decision_variable_, inverted_saved_phase);
                evsids_.increase_score(last_decision_variable_);
                chb_.update_on_conflict(last_decision_variable_);
                vmtf_.activate(last_decision_variable_);
                vmtf_.bump(last_decision_variable_);
            }
        }

        /// @brief Feeds conflict-analysis bump candidates into EVSIDS/VMTF/CHB for the next branch decision.
        /// @param variables Variables that participated in the analyzed conflict.
        /// @throws None (noexcept).
        void notify_conflict_variables(const std::span<const variable> variables) noexcept
        {
            if (variables.empty())
                return;

            selected_blend_ = 1u;
            std::vector<variable> seen_variables {};
            seen_variables.reserve(variables.size());

            for (auto it = variables.rbegin(); it != variables.rend(); ++it)
            {
                const auto seen_it = std::find_if(seen_variables.begin(), seen_variables.end(),
                                                  [it](const variable existing) noexcept { return existing.index() == it->index(); });
                if (seen_it != seen_variables.end())
                    continue;
                seen_variables.push_back(*it);

                evsids_.increase_score(*it);
                chb_.update_on_conflict(*it);
                vmtf_.activate(*it);
                vmtf_.bump(*it);
            }
        }

        /// @brief Feeds raw conflict-clause literals into heuristics, including polarity hints for saved phase.
        /// @param conflict_clause Literals from the conflicting clause.
        /// @throws None (noexcept).
        void notify_conflict_clause(const std::span<const literal> conflict_clause) noexcept
        {
            if (conflict_clause.empty())
                return;

            selected_blend_ = 1u;
            std::vector<variable> seen_variables {};
            seen_variables.reserve(conflict_clause.size());

            for (auto it = conflict_clause.rbegin(); it != conflict_clause.rend(); ++it)
            {
                const auto var = it->variable_of();
                const auto seen_it = std::find_if(seen_variables.begin(), seen_variables.end(),
                                                  [var](const variable existing) noexcept { return existing.index() == var.index(); });
                if (seen_it != seen_variables.end())
                    continue;
                seen_variables.push_back(var);

                phase_.set_saved_phase(var, !it->is_negated());
                evsids_.increase_score(var);
                chb_.update_on_conflict(var);
                vmtf_.activate(var);
                vmtf_.bump(var);
            }
        }

        /// @brief Feeds propagation-implied variables into EVSIDS/VMTF/CHB so near-frontier activity affects branching.
        /// @param variables Variables implied by recent unit propagation.
        /// @throws None (noexcept).
        void notify_propagated_variables(const std::span<const variable> variables) noexcept
        {
            if (variables.empty())
                return;

            selected_blend_ = 1u;
            std::vector<variable> seen_variables {};
            seen_variables.reserve(variables.size());

            for (auto it = variables.rbegin(); it != variables.rend(); ++it)
            {
                const auto seen_it = std::find_if(seen_variables.begin(), seen_variables.end(),
                                                  [it](const variable existing) noexcept { return existing.index() == it->index(); });
                if (seen_it != seen_variables.end())
                    continue;
                seen_variables.push_back(*it);

                evsids_.increase_score(*it);
                chb_.update_on_assignment(*it);
                vmtf_.activate(*it);
                vmtf_.bump(*it);
            }
        }

        /// @brief Records an executed assignment so saved phase can be reused on later branches.
        /// @param assigned_literal Literal that has just been assigned by assumptions, propagation, or branching.
        /// @throws None (noexcept).
        void notify_assignment_literal(const literal assigned_literal) noexcept
        {
            const auto var = assigned_literal.variable_of();
            phase_.set_saved_phase(var, !assigned_literal.is_negated());
            chb_.update_on_assignment(var);
            vmtf_.activate(var);
            vmtf_.bump(var);
            selected_blend_ = 1u;
        }

        /// @brief Feeds learned-clause literals into EVSIDS/VMTF/CHB using stronger weighting for shorter clauses.
        /// @param learned_clause Literals of the newly learned clause.
        /// @throws None (noexcept).
        void notify_learned_clause(const std::span<const literal> learned_clause) noexcept
        {
            if (learned_clause.empty())
                return;

            selected_blend_ = 1u;
            const auto bump_rounds = learned_clause_bump_rounds(learned_clause.size());
            const auto asserting_literal = learned_clause.front();
            const auto asserting_variable = asserting_literal.variable_of();
            phase_.set_saved_phase(asserting_variable, !asserting_literal.is_negated());

            std::vector<variable> unique_variables {};
            unique_variables.reserve(learned_clause.size());
            for (auto it = learned_clause.rbegin(); it != learned_clause.rend(); ++it)
            {
                const auto var = it->variable_of();
                const auto seen_it = std::find_if(unique_variables.begin(), unique_variables.end(),
                                                  [var](const variable existing) noexcept { return existing.index() == var.index(); });
                if (seen_it == unique_variables.end())
                    unique_variables.push_back(var);
            }

            for (std::uint32_t round = 0; round < bump_rounds; ++round)
            {
                for (const auto var: unique_variables)
                {
                    evsids_.increase_score(var);
                    chb_.update_on_conflict(var);
                    vmtf_.activate(var);
                    vmtf_.bump(var);
                }

                // Keep asserting literals near the branch frontier after each weighted sweep.
                evsids_.increase_score(asserting_variable);
                chb_.update_on_conflict(asserting_variable);
                vmtf_.activate(asserting_variable);
                vmtf_.bump(asserting_variable);
            }
        }

        /// @brief Notifies the decision heuristics that a restart just occurred.
        /// @throws None (noexcept).
        void notify_restart() noexcept
        {
            ++restart_count_;
            phase_bias_ = !phase_bias_;

            vmtf_.shuffle(restart_count_);

            if (restart_decay_interval_ != 0u && (restart_count_ % restart_decay_interval_) == 0u)
            {
                chb_.decay_step();
                ++chb_decay_count_;
            }

            if (last_decision_variable_.index() != 0u)
                chb_.update_on_assignment(last_decision_variable_);
        }

        /// @brief Notifies the decision heuristics that a rephase just occurred.
        /// @throws None (noexcept).
        void notify_rephase() noexcept
        {
            ++rephase_count_;
            phase_bias_ = (rephase_count_ % 2u) == 0u;
            chb_.decay_step();
        }

        /// @brief Re-evaluates and applies the current VMTF/EVSIDS/CHB blending policy.
        /// @throws None (noexcept).
        void select_heuristic_blend() noexcept
        {
            selected_blend_ = 1u;
            if (next_variable_ != 0u)
                vmtf_.activate(variable {next_variable_});
        }

        /// @brief Registers an unassigned formula variable as a heuristic branch candidate.
        /// @param var Variable to make available to VMTF selection.
        /// @throws None (noexcept).
        void activate_variable(const variable var) noexcept
        {
            if (var.index() == 0u)
                return;
            vmtf_.activate(var);
            selected_blend_ = 1u;
        }

        /// @brief Enables or disables CHB as the first candidate source in the explicit blend policy.
        void set_chb_enabled(const bool enabled) noexcept { chb_enabled_ = enabled; }

        /// @brief Returns whether CHB candidate selection is enabled.
        bool chb_enabled() const noexcept { return chb_enabled_; }

        /// @brief Seeds the next-variable fallback used by the current lightweight branch picker.
        /// @param variable Variable index selected by an external coordinator.
        /// @throws None (noexcept).
        void set_next_variable(const std::uint32_t variable) noexcept { next_variable_ = variable; }

        /// @brief Configures an optional variable-selectability filter for branch candidate selection.
        /// @param predicate Callback returning true when a candidate variable is selectable.
        /// @param context Opaque context passed to the callback (may be null).
        /// @throws None (noexcept).
        void set_selectability_filter(const selectable_predicate_t predicate, const void* context) noexcept
        {
            selectable_predicate_ = predicate;
            selectable_context_ = context;
        }

        /// @brief Clears any previously configured variable-selectability filter.
        /// @throws None (noexcept).
        void clear_selectability_filter() noexcept
        {
            selectable_predicate_ = nullptr;
            selectable_context_ = nullptr;
        }

        /// @brief Configures heuristic maintenance intervals.
        /// @param conflict_maintenance_interval EVSIDS rescale interval in conflicts (0 disables).
        /// @param chb_decay_interval CHB decay interval in conflicts (0 disables).
        /// @param restart_decay_interval CHB decay interval in restarts (0 disables).
        void set_maintenance_intervals(const std::uint32_t conflict_maintenance_interval, const std::uint32_t chb_decay_interval,
                                       const std::uint32_t restart_decay_interval) noexcept
        {
            conflict_maintenance_interval_ = conflict_maintenance_interval;
            chb_decay_interval_ = chb_decay_interval;
            restart_decay_interval_ = restart_decay_interval;
        }

        /// @brief Returns whether a heuristic blend has been selected for the current decision cycle.
        /// @return True if `select_heuristic_blend` has run at least once.
        [[nodiscard]] bool has_active_blend() const noexcept { return selected_blend_ != 0u; }

        /// @brief Returns the variable most recently chosen by the decision engine.
        /// @return Last selected decision variable.
        std::optional<variable> last_selected_variable() const noexcept
        {
            if (last_decision_variable_.index() == 0u)
                return {};
            return last_decision_variable_;
        }

        /// @brief Returns how many EVSIDS rescale maintenance steps were executed.
        /// @return Number of rescale maintenance events.
        std::uint32_t evsids_rescale_count() const noexcept { return evsids_rescale_count_; }

        /// @brief Returns how many CHB decay maintenance steps were executed.
        /// @return Number of CHB decay maintenance events.
        std::uint32_t chb_decay_count() const noexcept { return chb_decay_count_; }

        /// @brief Returns the configured EVSIDS conflict maintenance interval.
        std::uint32_t conflict_maintenance_interval() const noexcept { return conflict_maintenance_interval_; }

        /// @brief Returns the configured CHB conflict decay interval.
        std::uint32_t chb_decay_interval() const noexcept { return chb_decay_interval_; }

        /// @brief Returns the configured CHB restart decay interval.
        std::uint32_t restart_decay_interval() const noexcept { return restart_decay_interval_; }

    private:
        static std::uint32_t learned_clause_bump_rounds(const std::size_t clause_size) noexcept
        {
            if (clause_size <= 2u)
                return 3u;
            if (clause_size <= 4u)
                return 2u;
            return 1u;
        }

        bool has_saved_phase(const variable var) const noexcept
        {
            return static_cast<std::size_t>(var.index()) < phase_.saved_phase_count();
        }

        bool is_selectable(const variable var) const noexcept
        {
            if (selectable_predicate_ == nullptr)
                return true;
            return selectable_predicate_(var, selectable_context_);
        }

        vmtf_queue vmtf_ {};
        evsids_heap evsids_ {};
        chb_tracker chb_ {};
        store::phase phase_ {};
        std::uint32_t next_variable_ {};
        std::uint32_t conflict_count_ {};
        std::uint32_t restart_count_ {};
        std::uint32_t rephase_count_ {};
        std::uint32_t selected_blend_ {};
        bool chb_enabled_ {};
        std::uint32_t conflict_maintenance_interval_ {16u};
        std::uint32_t chb_decay_interval_ {8u};
        std::uint32_t restart_decay_interval_ {4u};
        std::uint32_t evsids_rescale_count_ {};
        std::uint32_t chb_decay_count_ {};
        bool phase_bias_ {true};
        variable last_decision_variable_ {};
        selectable_predicate_t selectable_predicate_ {};
        const void* selectable_context_ {};
    };
}
