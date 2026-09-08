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
        std::optional<literal> pick_branch_literal() noexcept;

        /// @brief Selects the next branching variable using the active heuristic blend.
        /// @return Next branching literal wrapping the selected variable, or `std::nullopt` if none remains.
        /// @throws None (noexcept).
        std::optional<literal> pick_decision_variable() noexcept;

        /// @brief Selects the polarity to assign the chosen decision variable.
        /// @return True for positive phase, false for negative phase.
        /// @throws None (noexcept).
        bool pick_decision_phase() const noexcept;

        /// @brief Seeds saved branching polarities from a local-search assignment.
        void seed_saved_phases(const std::span<const std::uint8_t> assignment) noexcept { phase_.seed_saved_phases(assignment); }

        /// @brief Returns the saved branching polarities, indexed by variable.
        [[nodiscard]] std::span<const std::uint8_t> saved_phases_view() const noexcept { return phase_.saved_phases_view(); }

        /// @brief Notifies the decision heuristics that a conflict just occurred.
        /// @throws None (noexcept).
        void notify_conflict() noexcept;

        /// @brief Feeds conflict-analysis bump candidates into EVSIDS/VMTF/CHB for the next branch decision.
        /// @param variables Variables that participated in the analyzed conflict.
        /// @throws None (noexcept).
        void notify_conflict_variables(const std::span<const variable> variables) noexcept;

        /// @brief Feeds raw conflict-clause literals into heuristics, including polarity hints for saved phase.
        /// @param conflict_clause Literals from the conflicting clause.
        /// @throws None (noexcept).
        void notify_conflict_clause(const std::span<const literal> conflict_clause) noexcept;

        /// @brief Feeds propagation-implied variables into EVSIDS/VMTF/CHB so near-frontier activity affects branching.
        /// @param variables Variables implied by recent unit propagation.
        /// @throws None (noexcept).
        void notify_propagated_variables(const std::span<const variable> variables) noexcept;

        /// @brief Records an executed assignment so saved phase can be reused on later branches.
        /// @param assigned_literal Literal that has just been assigned by assumptions, propagation, or branching.
        /// @throws None (noexcept).
        void notify_assignment_literal(const literal assigned_literal) noexcept;

        /// @brief Feeds learned-clause literals into EVSIDS/VMTF/CHB using stronger weighting for shorter clauses.
        /// @param learned_clause Literals of the newly learned clause.
        /// @throws None (noexcept).
        void notify_learned_clause(const std::span<const literal> learned_clause) noexcept;

        /// @brief Notifies the decision heuristics that a restart just occurred.
        /// @throws None (noexcept).
        void notify_restart() noexcept;

        /// @brief Notifies the decision heuristics that a rephase just occurred.
        /// @throws None (noexcept).
        void notify_rephase() noexcept;

        /// @brief Re-evaluates and applies the current VMTF/EVSIDS/CHB blending policy.
        /// @throws None (noexcept).
        void select_heuristic_blend() noexcept;

        /// @brief Registers an unassigned formula variable as a heuristic branch candidate.
        /// @param var Variable to make available to VMTF selection.
        /// @throws None (noexcept).
        /// @brief Returns a variable to the candidate sources after backtracking unassigns it.
        /// @details Both sources are consumed as the trail grows: EVSIDS pops entries and VMTF is walked past
        /// assigned variables. Without this hook the candidate pool only ever shrinks, so after enough decisions
        /// the engine reports exhaustion -- which the search would otherwise read as a satisfying assignment --
        /// and restarts degrade into re-exploring a fixed variable order.
        /// @param var Variable that has just become unassigned.
        /// @throws None (noexcept).
        void notify_unassigned_variable(const variable var) noexcept;

        void activate_variable(const variable var) noexcept;

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
                                       const std::uint32_t restart_decay_interval) noexcept;

        /// @brief Returns whether a heuristic blend has been selected for the current decision cycle.
        /// @return True if `select_heuristic_blend` has run at least once.
        [[nodiscard]] bool has_active_blend() const noexcept { return selected_blend_ != 0u; }

        /// @brief Returns the variable most recently chosen by the decision engine.
        /// @return Last selected decision variable.
        std::optional<variable> last_selected_variable() const noexcept;

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
        static std::uint32_t learned_clause_bump_rounds(const std::size_t clause_size) noexcept;

        /// @brief Opens a fresh duplicate-detection scan over a batch of variables.
        /// @details These notification paths run once per propagation batch and once per conflict, so the
        /// duplicate check they need is on the solver's hottest path. The previous formulation allocated a vector
        /// per call and searched it linearly per element, which is quadratic in the batch size and made the
        /// allocator alone about twelve percent of total runtime. A monotonically increasing stamp per variable
        /// gives the same answer in constant time per element with no allocation.
        void begin_unique_scan() noexcept;

        /// @brief Records a variable in the current scan, reporting whether this is its first occurrence.
        [[nodiscard]] bool mark_first_occurrence(const variable var) noexcept;

        bool has_saved_phase(const variable var) const noexcept
        {
            return static_cast<std::size_t>(var.index()) < phase_.saved_phase_count();
        }

        bool is_selectable(const variable var) const noexcept;

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
        std::vector<variable> unique_variables_scratch_ {};
        std::vector<std::uint32_t> unique_scan_stamps_ {};
        std::uint32_t unique_scan_stamp_ {};
        selectable_predicate_t selectable_predicate_ {};
        const void* selectable_context_ {};
    };
}
