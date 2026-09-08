/// @file library/src/kmx/sat/cdcl/engine/decision.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/engine/decision.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/engine/decision.hpp>

namespace kmx::sat::cdcl::engine
{
    std::optional<literal> decision::pick_branch_literal() noexcept
    {
        const auto variable_literal = pick_decision_variable();
        if (!variable_literal.has_value())
            return {};

        const auto phase = pick_decision_phase();
        last_decision_variable_ = variable_literal->variable_of();
        return literal {variable_literal->variable_of(), !phase};
    }

    std::optional<literal> decision::pick_decision_variable() noexcept
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

            // Activity scores are consulted first, and the move-to-front queue is the fallback behind them.
            // The order matters more than anything else in this class: with the queue first it always had a
            // candidate, so the activity heap was never reached and its scores went unused. Measured on random
            // 3-SAT n=100..160, putting activity first cut a 20-instance sweep from 36.4 s to 1.0 s, and
            // r3_200 from 344,408 conflicts to 27,106.
            //
            // EVSIDS pops are destructive. A variable popped and rejected here was rejected because it is
            // already assigned, and re-inserting it only guarantees popping it again on the next decision --
            // an O(log n) round trip per assigned variable per decision, repeated until something unassigns
            // it. `notify_unassigned_variable` is what puts a variable back, and backtracking calls it for
            // every entry it retracts, so the heap is repopulated exactly when the variable becomes a
            // candidate again. Activity survives in `retained_activity_`, so re-entry costs it no ranking.
            std::optional<variable> evsids_selected {};
            for (;;)
            {
                const auto evsids_candidate = evsids_.extract_best();
                if (!evsids_candidate.has_value())
                    break;
                if (is_selectable(*evsids_candidate))
                {
                    evsids_selected = evsids_candidate;
                    break;
                }
            }
            if (evsids_selected.has_value())
            {
                last_decision_variable_ = *evsids_selected;
                return literal {*evsids_selected, false};
            }

            const auto vmtf_candidate = vmtf_.front_candidate_if([this](const variable var) noexcept { return is_selectable(var); });
            if (vmtf_candidate.has_value())
            {
                last_decision_variable_ = *vmtf_candidate;
                return literal {*vmtf_candidate, false};
            }
        }

        if ((next_variable_ == 0u) || !is_selectable(variable {next_variable_}))
            return {};
        last_decision_variable_ = variable {next_variable_};
        return literal {variable {next_variable_}, false};
    }

    bool decision::pick_decision_phase() const noexcept
    {
        if ((last_decision_variable_.index() != 0u) && has_saved_phase(last_decision_variable_))
            return phase_.saved_phase(last_decision_variable_);
        return phase_bias_;
    }

    void decision::notify_conflict() noexcept
    {
        ++conflict_count_;
        phase_bias_ = (conflict_count_ % 2u) == 0u;
        selected_blend_ = 1u;

        // Age the activity scores once per conflict so recent conflicts outweigh old ones.
        evsids_.decay();

        if ((conflict_maintenance_interval_ != 0u) && ((conflict_count_ % conflict_maintenance_interval_) == 0u))
        {
            evsids_.rescale();
            ++evsids_rescale_count_;
        }

        if ((chb_decay_interval_ != 0u) && ((conflict_count_ % chb_decay_interval_) == 0u))
        {
            chb_.decay_step();
            ++chb_decay_count_;
        }

        if (last_decision_variable_.index() != 0u)
        {
            // The saved phase is deliberately left alone. Phase saving exists so that re-descent after a
            // backjump or restart reproduces the assignment the variable last held; inverting it on every
            // conflict forces the opposite branch each time, which cancels that benefit and makes restarts a
            // net loss. `notify_assignment_literal` already records the phase whenever the variable is set.
            if (!has_saved_phase(last_decision_variable_))
                phase_.set_saved_phase(last_decision_variable_, phase_bias_);
            evsids_.increase_score(last_decision_variable_);
            chb_.update_on_conflict(last_decision_variable_);
            vmtf_.activate(last_decision_variable_);
            vmtf_.bump(last_decision_variable_);
        }
    }

    void decision::notify_conflict_variables(const std::span<const variable> variables) noexcept
    {
        if (variables.empty())
            return;

        selected_blend_ = 1u;
        begin_unique_scan();

        for (auto it = variables.rbegin(); it != variables.rend(); ++it)
        {
            if (!mark_first_occurrence(*it))
                continue;

            evsids_.increase_score(*it);
            chb_.update_on_conflict(*it);
            vmtf_.activate(*it);
            vmtf_.bump(*it);
        }
    }

    void decision::notify_conflict_clause(const std::span<const literal> conflict_clause) noexcept
    {
        if (conflict_clause.empty())
            return;

        selected_blend_ = 1u;
        begin_unique_scan();

        for (auto it = conflict_clause.rbegin(); it != conflict_clause.rend(); ++it)
        {
            const auto var = it->variable_of();
            if (!mark_first_occurrence(var))
                continue;

            // Note: every literal of a conflicting clause is false under the current assignment, so this
            // writes the polarity *opposite* to the one the variable holds -- it inverts the saved phase of
            // each variable in the clause on every conflict, which reads as contradicting the phase-saving
            // rationale in `notify_conflict`. Removing it was measured and is a net loss (36-instance random
            // 3-SAT set: 24.0 s -> 26.4 s, reproducible), because it is currently the solver's only
            // diversification mechanism: `controller::rephase` is unimplemented scaffolding and is not wired
            // into the search. It should be removed together with real rephasing, not before it.
            phase_.set_saved_phase(var, !it->is_negated());
            evsids_.increase_score(var);
            chb_.update_on_conflict(var);
            vmtf_.activate(var);
            vmtf_.bump(var);
        }
    }

    void decision::notify_propagated_variables(const std::span<const variable> variables) noexcept
    {
        if (variables.empty())
            return;

        selected_blend_ = 1u;
        begin_unique_scan();

        for (auto it = variables.rbegin(); it != variables.rend(); ++it)
        {
            if (!mark_first_occurrence(*it))
                continue;

            // Propagation is not evidence of importance: every implied literal would otherwise be scored as
            // highly as a variable the conflict actually turned on, flattening both rankings. CHB is the one
            // heuristic whose model genuinely updates on assignment.
            chb_.update_on_assignment(*it);
            vmtf_.activate(*it);
        }
    }

    void decision::notify_assignment_literal(const literal assigned_literal) noexcept
    {
        const auto var = assigned_literal.variable_of();
        phase_.set_saved_phase(var, !assigned_literal.is_negated());
        chb_.update_on_assignment(var);

        // Activate but deliberately do not bump. VMTF ranks variables by how recently they took part in a
        // conflict; bumping on assignment overwrites that with "most recently assigned", which is dominated by
        // propagation order and carries no information about where the search is stuck.
        vmtf_.activate(var);
        selected_blend_ = 1u;
    }

    void decision::notify_learned_clause(const std::span<const literal> learned_clause) noexcept
    {
        if (learned_clause.empty())
            return;

        selected_blend_ = 1u;
        const auto bump_rounds = learned_clause_bump_rounds(learned_clause.size());
        const auto asserting_literal = learned_clause.front();
        const auto asserting_variable = asserting_literal.variable_of();
        phase_.set_saved_phase(asserting_variable, !asserting_literal.is_negated());

        begin_unique_scan();
        unique_variables_scratch_.clear();
        for (auto it = learned_clause.rbegin(); it != learned_clause.rend(); ++it)
        {
            const auto var = it->variable_of();
            if (mark_first_occurrence(var))
                unique_variables_scratch_.push_back(var);
        }

        for (std::uint32_t round = 0u; round < bump_rounds; ++round)
        {
            for (const auto var: unique_variables_scratch_)
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

    void decision::notify_restart() noexcept
    {
        ++restart_count_;
        phase_bias_ = !phase_bias_;

        vmtf_.shuffle(restart_count_);

        if ((restart_decay_interval_ != 0u) && ((restart_count_ % restart_decay_interval_) == 0u))
        {
            chb_.decay_step();
            ++chb_decay_count_;
        }

        if (last_decision_variable_.index() != 0u)
            chb_.update_on_assignment(last_decision_variable_);
    }

    void decision::notify_rephase() noexcept
    {
        ++rephase_count_;
        phase_bias_ = (rephase_count_ % 2u) == 0u;
        chb_.decay_step();
    }

    void decision::select_heuristic_blend() noexcept
    {
        selected_blend_ = 1u;
        if (next_variable_ != 0u)
            vmtf_.activate(variable {next_variable_});
    }

    void decision::notify_unassigned_variable(const variable var) noexcept
    {
        if (var.index() == 0u)
            return;
        vmtf_.activate(var);
        evsids_.insert(var);
    }

    void decision::activate_variable(const variable var) noexcept
    {
        if (var.index() == 0u)
            return;
        vmtf_.activate(var);
        selected_blend_ = 1u;
    }

    void decision::set_maintenance_intervals(const std::uint32_t conflict_maintenance_interval, const std::uint32_t chb_decay_interval,
                                             const std::uint32_t restart_decay_interval) noexcept
    {
        conflict_maintenance_interval_ = conflict_maintenance_interval;
        chb_decay_interval_ = chb_decay_interval;
        restart_decay_interval_ = restart_decay_interval;
    }

    std::optional<variable> decision::last_selected_variable() const noexcept
    {
        if (last_decision_variable_.index() == 0u)
            return {};
        return last_decision_variable_;
    }

    std::uint32_t decision::learned_clause_bump_rounds(const std::size_t clause_size) noexcept
    {
        if (clause_size <= 2u)
            return 3u;
        if (clause_size <= 4u)
            return 2u;
        return 1u;
    }

    void decision::begin_unique_scan() noexcept
    {
        if (++unique_scan_stamp_ == 0u)
        {
            // The stamp wrapped, so every recorded value is indistinguishable from the new scan's; clearing
            // is the only way to keep "seen" meaningful, and at one clear per four billion scans it is free.
            std::fill(unique_scan_stamps_.begin(), unique_scan_stamps_.end(), 0u);
            unique_scan_stamp_ = 1u;
        }
    }

    [[nodiscard]] bool decision::mark_first_occurrence(const variable var) noexcept
    {
        const auto index = static_cast<std::size_t>(var.index());
        if (index >= unique_scan_stamps_.size())
            unique_scan_stamps_.resize(index + 1u, 0u);
        if (unique_scan_stamps_[index] == unique_scan_stamp_)
            return false;
        unique_scan_stamps_[index] = unique_scan_stamp_;
        return true;
    }

    bool decision::is_selectable(const variable var) const noexcept
    {
        if (selectable_predicate_ == nullptr) [[unlikely]]
            return true;
        return selectable_predicate_(var, selectable_context_);
    }
}
