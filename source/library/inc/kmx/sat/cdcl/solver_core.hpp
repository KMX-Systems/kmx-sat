/// @file inc/kmx/sat/cdcl/solver_core.hpp
/// @brief The main internal solver container: the CDCL search engine and the episode driver around it.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstdint>
    #include <initializer_list>
    #include <limits>
    #include <span>
    #include <variant>
    #include <vector>
#endif
#include <kmx/sat/cdcl/bank/trail.hpp>
#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/incremental_context.hpp>
#include <kmx/sat/cdcl/local_search.hpp>
#include <kmx/sat/cdcl/memory_governor.hpp>
#include <kmx/sat/cdcl/model_reconstructor.hpp>
#include <kmx/sat/cdcl/propagator.hpp>
#include <kmx/sat/cdcl/search_coordinator.hpp>
#include <kmx/sat/cdcl/stack/extension.hpp>
#include <kmx/sat/cdcl/store/clause_cold.hpp>
#include <kmx/sat/cdcl/var_heap.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/flush_restore_manager.hpp>
#include <kmx/sat/simplify/scheduler/inprocess.hpp>
#include <kmx/sat/simplify/scheduler/preprocess.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief The main internal solver container: the CDCL search engine and the episode driver around it.
    /// @details
    /// `solver_core` plays the coordinating role of CaDiCaL's `Internal`: it owns the clause database, the watch
    /// lists, the simplification schedulers, the proof manager and the search state, and it runs one solve
    /// episode per `solve` call. The search itself -- unit propagation, first-UIP conflict analysis with
    /// recursive minimization, backjumping, restarts with trail reuse, and reduction with in-place arena
    /// compaction -- lives in this class so that every hot loop touches its arrays directly, with no callback or
    /// side table in between. `search_coordinator` keeps the schedules, counters and outcome of the episode.
    ///
    /// The search state is laid out the way the propagation loop reads it: assignment values are indexed by
    /// literal (so "is this literal true" is one signed load), levels, reasons and trail positions by variable,
    /// and the trail is a flat vector with a control stack recording where each decision level starts.
    /// Assumptions occupy the first decision levels of an episode, so every learned clause is implied by the
    /// formula alone and can be kept across episodes; a failed assumption is explained by walking the reasons
    /// that falsified it.
    class solver_core final
    {
    public:
        struct inprocess_telemetry_snapshot final
        {
            double conflict_density_ema {};
            double structural_gain_ema {};
            double restart_pressure_ema {};
            double reduction_pressure_ema {};
            double learned_clause_pressure_ema {};
        };

        /// @brief Enumerates the internal (pre-external-mapping) outcomes of one solve episode.
        enum class status
        {
            /// @brief The formula is satisfiable at the internal-variable level.
            satisfiable,
            /// @brief The formula is unsatisfiable at the internal-variable level.
            unsatisfiable,
            /// @brief The episode ended without a definite result.
            unknown
        };

        /// @brief Constructs a solver core with an empty clause database and a fresh search coordinator.
        /// @throws None (noexcept).
        solver_core() noexcept;

        /// @brief Resets the core to an empty, freshly bound state without invalidating internal helper pointers.
        /// @throws None (noexcept).
        void reset() noexcept;

        /// @brief Runs one solve episode under the given request.
        /// @param request Solve configuration for this episode (assumptions, limits, mode flags).
        /// @return Internal-level terminal status for this episode.
        /// @throws None (noexcept).
        status solve(const solve_request& request) noexcept;

        /// @brief Runs one solve episode restricted to the given internal assumption literals.
        /// @param assumptions Internal assumption literals for this episode.
        /// @return Internal-level terminal status for this episode.
        /// @throws None (noexcept).
        status solve_under_assumptions(const std::span<const literal> assumptions) noexcept;

        /// @brief Pre-reserves capacity for the given variable count.
        /// @param variable_bound Upper bound on variable index.
        /// @throws None (noexcept).
        void reserve(const variable::index_t variable_bound) noexcept;

        /// @brief Registers an original (non-redundant) problem clause with the internal clause database.
        /// @details Duplicate literals are dropped and a tautology is not stored at all: both would put two watches
        /// of one clause on the same literal, which the propagation loop is not built to tolerate.
        /// @param literals Internal literals composing the clause.
        /// @throws None (noexcept).
        void add_problem_clause(const std::span<const literal> literals) noexcept;

        /// @brief Convenience overload for adding a clause from a braced literal list.
        /// @param literals Internal literals composing the clause.
        /// @throws None (noexcept).
        void add_problem_clause(const std::initializer_list<literal> literals) noexcept
        {
            add_problem_clause(std::span<const literal> {literals.begin(), literals.size()});
        }

        /// @brief Returns the terminal status of the most recently completed solve episode.
        status current_status() const noexcept { return status_; }

        /// @brief Returns how many original problem clauses have been registered.
        std::size_t original_clause_count() const noexcept { return original_clause_count_; }

        /// @brief Returns how many learned clauses are currently owned by the clause database.
        std::size_t learned_clause_count() const noexcept { return clause_database_.stats_snapshot().redundant_count; }

        /// @brief Returns how many learned clauses went through minimization.
        std::uint32_t minimized_learned_clause_count() const noexcept { return minimized_clause_count_; }

        /// @brief Returns how many learned clauses minimization actually shortened.
        std::uint32_t shrunk_learned_clause_count() const noexcept { return shrunk_clause_count_; }

        /// @brief Returns how many learned clauses entered the core tier on creation.
        std::uint32_t promoted_learned_clause_count() const noexcept { return promoted_clause_count_; }

        /// @brief Extracts the internal-variable model after a satisfiable episode.
        std::span<const literal> extract_internal_model() const noexcept { return internal_model_; }

        /// @brief Extracts the internal-variable failed core after an unsatisfiable episode under assumptions.
        std::span<const literal> extract_failed_core() const noexcept { return failed_core_; }

        std::size_t proof_buffered_event_count() const noexcept { return proof_manager_.buffered_event_count(); }

        const proof::proof_event& last_proof_event() const noexcept { return proof_manager_.last_event(); }

        std::span<const proof::proof_event> buffered_proof_events() const noexcept { return proof_manager_.buffered_events(); }

        /// @brief Emits the final proof conclusion for a completed solve episode.
        void finalize_proof() noexcept
        {
            if (proof_enabled())
                proof_manager_.on_conclusion();
        }

        /// @brief Returns the latest outcome produced by the coordinator-backed search episode.
        search_coordinator::outcome current_search_outcome() const noexcept { return search_coordinator_.current_outcome(); }

        /// @brief Returns why the latest coordinator-backed episode terminated.
        search_coordinator::termination_cause current_search_termination_cause() const noexcept
        {
            return search_coordinator_.current_termination_cause();
        }

        /// @brief Returns how many learned clauses were retained across completed epochs.
        std::uint32_t retained_learned_clause_count() const noexcept { return incremental_context_.retained_learned_clauses(); }

        /// @brief Returns whether the latest solve call completed a transient-state reset.
        bool transient_state_was_reset() const noexcept { return incremental_context_.transient_state_reset(); }

        /// @brief Returns whether a persistent option subset has been recorded.
        bool persisted_option_subset() const noexcept { return incremental_context_.persisted_option_subset(); }

        /// @brief Marks the currently configured options as persistent across solve epochs.
        void persist_option_subset() noexcept { incremental_context_.persist_option_subset(); }

        /// @brief Returns how many clauses the subsumption pass removed.
        std::size_t subsumed_clause_count() const noexcept { return preprocess_scheduler_.subsumed_clause_count(); }

        /// @brief Returns how many preprocess pipeline runs have been executed.
        std::size_t preprocess_run_count() const noexcept { return preprocess_scheduler_.pipeline_run_count(); }

        /// @brief Returns how many inprocess epochs have been executed.
        counter_t inprocess_epoch_count() const noexcept { return inprocess_scheduler_.epoch_count(); }

        /// @brief Returns conflicts handled during the latest solve episode.
        counter_t conflict_event_count() const noexcept { return search_coordinator_.conflict_event_count(); }
        /// @brief Conflicts the raw-formula probe spent before the second phase discarded its state.
        counter_t raw_probe_conflict_count() const noexcept { return raw_probe_conflicts_; }

        /// @brief Returns decisions produced during the latest solve episode.
        counter_t decision_event_count() const noexcept { return search_coordinator_.decision_event_count(); }

        /// @brief Returns restarts performed during the latest solve episode.
        counter_t restart_count() const noexcept { return search_coordinator_.restart_count(); }

        /// @brief Returns learned clauses registered during the latest solve episode.
        std::size_t episode_learned_clause_count() const noexcept { return search_coordinator_.learned_clause_count(); }

        /// @brief Returns how many propagation rounds the core ran.
        std::size_t propagator_call_count() const noexcept { return propagator_.propagation_call_count(); }

        /// @brief Returns the number of assignments made by watched-literal propagation in the current core state.
        std::size_t propagation_assignment_count() const noexcept { return propagation_assignment_count_; }

        /// @brief Returns the number of watch entries examined by propagation in the current core state.
        std::size_t watch_entry_scan_count() const noexcept { return watch_entry_scan_count_; }

        /// @brief Returns how many watch-list partitions propagation queried in the current episode.
        std::size_t watch_partition_iterate_count() const noexcept { return watch_list_.iterate_call_count(); }

        /// @brief Returns how many binary watch entries were processed in the current core state.
        std::size_t binary_watch_scan_count() const noexcept { return binary_watch_scan_count_; }

        /// @brief Returns how many binary watch entries produced conflicts in the current core state.
        std::size_t binary_watch_conflict_count() const noexcept { return binary_watch_conflict_count_; }

        /// @brief Returns how many learned clauses were shortened after being recorded (always zero: clauses are
        /// minimized before they are stored).
        std::size_t learned_clause_shrink_event_count() const noexcept { return learned_clause_shrink_event_count_; }

        counter_t learned_clause_glue_total() const noexcept { return learned_clause_glue_total_; }

        counter_t learned_clause_glue_sample_count() const noexcept { return learned_clause_glue_sample_count_; }

        counter_t reduction_pass_count() const noexcept { return search_coordinator_.reduction_pass_count(); }

        std::uint64_t probe_count() const noexcept { return probe_count_; }
        std::uint64_t walk_flip_count() const noexcept { return local_search_.flip_count(); }

        std::uint64_t probe_unit_count() const noexcept { return probe_unit_count_ + probe_lifted_count_; }

        counter_t reduced_clause_count() const noexcept { return search_coordinator_.reduced_clause_count(); }

        counter_t deleted_clause_count() const noexcept { return search_coordinator_.deleted_clause_count(); }

        /// @brief Returns how many assumption propagation rounds the core ran.
        std::size_t propagator_assumption_call_count() const noexcept { return propagator_.assumption_propagation_call_count(); }

        /// @brief Exposes the flush/restore policy manager for focused integration tests.
        simplify::flush_restore_manager& flush_restore_manager() noexcept { return flush_restore_manager_; }

        /// @brief Attaches an external proof tracer to the core-owned proof manager.
        /// @details Proof identities are allocated lazily: until a consumer is attached no clause carries one, so
        /// attaching first assigns identities to every clause already in the database.
        /// @param sink External proof tracer sink.
        void attach_proof_tracer(const proof::tracer::view& sink) noexcept;

        /// @brief Returns whether any proof format is active through the core-owned proof manager.
        bool proof_enabled() const noexcept { return proof_manager_.has_enabled_formats(); }

        bool proof_checkers_valid() const noexcept { return proof_manager_.validate_checkers(); }

        /// @brief Configures decision-heuristic maintenance intervals.
        /// @param conflict_maintenance_interval Conflicts between EVSIDS renormalizations (0 disables the cadence;
        /// the heap still renormalizes on its own before its scores can overflow).
        /// @param chb_decay_interval Retained for the option surface; the search engine no longer runs CHB.
        /// @param restart_decay_interval Retained for the option surface; the search engine no longer runs CHB.
        void set_decision_maintenance_intervals(const std::uint32_t conflict_maintenance_interval, const std::uint32_t chb_decay_interval,
                                                const std::uint32_t restart_decay_interval) noexcept;

        void set_restart_interval(const counter_t interval) noexcept
        {
            stable_restart_interval_ = interval;
            search_coordinator_.set_restart_interval(interval);
        }

        /// @brief Sets the flip budget per variable of the opening walk; zero disables local search entirely.
        void set_local_search_flips_per_variable(const std::size_t flips) noexcept { local_search_flips_per_variable_ = flips; }

        /// @brief Sets the share of search effort later walks may spend, in percent; zero disables re-walks.
        void set_local_search_effort_percent(const std::size_t percent) noexcept
        {
            local_search_effort_percent_ = (percent > 100u) ? 100u : percent;
        }

        /// @brief Configures how many conflicts and restarts separate inprocessing epochs; zero keeps a window.
        void set_inprocess_trigger_windows(const counter_t conflict_window, const counter_t restart_window) noexcept
        {
            inprocess_scheduler_.set_trigger_windows(conflict_window, restart_window);
        }

        void set_reduction_interval(const counter_t interval) noexcept { search_coordinator_.set_reduction_interval(interval); }

        void set_decision_restart_interval(const counter_t interval) noexcept
        {
            search_coordinator_.set_decision_restart_interval(interval);
        }

        /// @brief Returns configured decision maintenance intervals in conflict/conflict/restart order.
        std::array<std::uint32_t, 3u> decision_maintenance_intervals() const noexcept
        {
            return search_coordinator_.decision_maintenance_intervals();
        }

        /// @brief Enables or disables the research-track CHB candidate source.
        void set_chb_enabled(const bool enabled) noexcept { search_coordinator_.set_chb_enabled(enabled); }

        /// @brief Returns whether the CHB candidate source is enabled.
        bool chb_enabled() const noexcept { return search_coordinator_.chb_enabled(); }

        /// @brief Configures the percentage of ranked learned clauses eligible for reduction.
        void set_reduction_fraction_percent(const std::uint32_t percent) noexcept
        {
            search_coordinator_.set_reduction_fraction_percent(percent);
        }

        /// @brief Returns the configured reduction quota percentage.
        std::uint32_t reduction_fraction_percent() const noexcept { return search_coordinator_.reduction_fraction_percent(); }

        /// @brief Configures activity protection for low-glue learned clauses.
        void set_activity_retention_threshold(const double threshold) noexcept
        {
            search_coordinator_.set_activity_retention_threshold(threshold);
        }

        /// @brief Returns the activity threshold protecting low-glue learned clauses.
        double activity_retention_threshold() const noexcept { return search_coordinator_.activity_retention_threshold(); }

        /// @brief Configures the glue EMA restart ratio in percentage points; zero disables the trigger.
        void set_glue_restart_threshold_percent(const std::uint32_t percent) noexcept
        {
            search_coordinator_.set_glue_restart_threshold((percent == 0u) ? 0.0 : static_cast<double>(percent) / 100.0);
        }

        /// @brief Returns the configured glue EMA restart ratio in percentage points.
        std::uint32_t glue_restart_threshold_percent() const noexcept { return search_coordinator_.glue_restart_threshold_percent(); }

        /// @brief Enables or disables research-track cold clause storage.
        void set_cold_storage_enabled(const bool enabled) noexcept { clause_cold_.set_enabled(enabled); }

        /// @brief Returns whether research-track cold clause storage is enabled.
        bool cold_storage_enabled() const noexcept { return clause_cold_.enabled(); }

        /// @brief Returns the current cold storage footprint in bytes.
        std::size_t cold_footprint_bytes() const noexcept { return clause_cold_.cold_footprint_bytes(); }

        /// @brief Returns the number of EVSIDS rescale maintenance steps observed in the branching heap.
        std::uint32_t decision_evsids_rescale_count() const noexcept { return heap_.rescale_count(); }

        /// @brief Returns the number of CHB decay maintenance steps observed in decision heuristics.
        std::uint32_t decision_chb_decay_count() const noexcept { return search_coordinator_.decision_chb_decay_count(); }

        const std::vector<simplify::scheduler::preprocess::pass_summary>& preprocess_last_reported_summaries() const noexcept
        {
            return preprocess_scheduler_.last_reported_summaries();
        }

        const std::vector<simplify::scheduler::inprocess::pass_summary>& inprocess_last_reported_summaries() const noexcept
        {
            return inprocess_scheduler_.last_reported_summaries();
        }

        inprocess_telemetry_snapshot current_inprocess_telemetry_snapshot() const noexcept;

        const simplify::preprocessing_profile_selector::pass_plan& preprocess_current_pass_plan() const noexcept
        {
            return preprocess_scheduler_.profile_selector().current_pass_plan();
        }

    private:
        // ------------------------------------------------------------------------------------------------------
        // Wiring
        // ------------------------------------------------------------------------------------------------------

        void rebind_internal_views() noexcept;

        void reset_episode_counters() noexcept;

        // ------------------------------------------------------------------------------------------------------
        // Constants and small types
        // ------------------------------------------------------------------------------------------------------

        /// @brief Variable count above which the phase-seeding probe is skipped as not worth its cost.
        static constexpr std::uint32_t local_search_variable_limit {200000u};
        /// @brief Original-clause count below which the probe cannot pay for itself and is skipped.
        /// @details The probe's cost is fixed by the flip budget, so on a formula the search closes in a few
        /// milliseconds it is pure overhead, and the instances where it is most wasteful are exactly the small
        /// crafted ones (the 160-clause aim formulas are built to defeat local search).
        static constexpr std::size_t local_search_minimum_clause_count {500u};
        /// @brief Original-clause count above which the phase-seeding probe is skipped.
        static constexpr std::size_t local_search_clause_limit {2000000u};
        /// @brief Flip budget per variable of the first walk, split over the strategies of the portfolio.
        /// @details Measured on the corpus: with the walk restarted from its best assignment every round, uniform
        /// random 3-SAT near the threshold and graph-colouring encodings both fall within about a thousand flips
        /// per variable, while a formula the walk cannot improve stops after two stalled rounds, so the budget can
        /// be generous without being paid in full on unsatisfiable or structured formulas (about 18% on
        /// unsatisfiable random 3-SAT, a few percent elsewhere).
        static constexpr std::size_t default_local_search_flips_per_variable {4000u};
        /// @brief Conflicts between the first search and the first re-walk; the interval then grows geometrically.
        static constexpr counter_t local_search_initial_interval {1000u};
        /// @brief Share of search time a re-walk may spend, in percent, before the adaptive scale is applied.
        static constexpr std::size_t default_local_search_effort_percent {8u};
        /// @brief Watch visits that cost about as much as one flip; converts search effort into a flip budget.
        static constexpr std::size_t visits_per_flip {15u};
        /// @brief Flips per variable every re-walk gets regardless of effort, so that a walk always has a chance
        /// to improve on the phases the search handed it; on a 250-variable formula this is a few milliseconds.
        static constexpr std::size_t local_search_rewalk_floor_per_variable {250u};
        /// @brief Largest opening walk, in flips; formulas whose per-variable budget exceeds it get no opening walk.
        /// @details Every formula the opening walk has ever solved has at most a few thousand variables, and on
        /// those the full per-variable budget costs well under a second. On a 10,000-40,000 variable bounded-model
        /// checking or planning formula the same per-variable budget is tens of millions of flips that creep from
        /// seven unsatisfied clauses to one and never reach zero, at a cost of one to twelve seconds on formulas
        /// the search itself finishes in a few hundred conflicts. Above the cap the walk is left to the re-walk
        /// schedule, where its budget follows the search effort actually spent.
        static constexpr std::size_t local_search_opening_flip_cap {10'000'000u};
        /// @brief Conflicts the search spends before the opening walk runs.
        /// @details Every planning, circuit and bounded-model-checking formula in the classic set that the walk
        /// cannot help is finished by the search within two thousand conflicts, in a few milliseconds, while the
        /// walk-friendly random and colouring formulas cost a few hundredths of a second for the same probe.
        /// Walking first therefore charged the structured formulas a hundred milliseconds each for nothing;
        /// probing first charges the walk winners a few percent and the held-out random set about as much.
        static constexpr counter_t local_search_opening_probe_conflicts {2000u};
        /// @brief Conflicts the search spends on the formula as stated before the preprocessing pipeline runs.
        /// @details Every instance the raw probe is meant to finish (the scheduling, circuit and small IBM
        /// instances) does so within a few hundred conflicts; a longer probe wastes work on the formulas whose
        /// search depends on preprocessing (`hole9`, `par16`), which the 2,000-conflict variant cost fourfold.
        static constexpr counter_t raw_probe_conflict_cap {500u};
        /// First-phase search budget in propagated assignments per literal of the stated formula (0 = lucky only).
        static constexpr std::size_t raw_probe_propagations_per_literal {2u};

        static constexpr bool raw_probe_enabled {true};
        /// @brief Largest learned clause whose reason-side variables are bumped along with the analyzed ones.
        static constexpr std::size_t reason_bump_size_limit {32u};
        /// @brief Trail growth allowed to failed-literal probing, per irredundant clause, plus a minimum.
        static constexpr std::size_t probe_effort_per_clause {4u};
        static constexpr std::size_t probe_minimum_effort {std::size_t {1u} << 14u};
        static constexpr std::size_t probe_rounds {3u};
        /// @brief Restart interval of the focused mode; the stable mode uses the configured interval.
        static constexpr counter_t focused_restart_interval {64u};
        /// @brief Conflicts of the first mode period; each period is twice the previous one.
        static constexpr counter_t mode_initial_period {2000u};
        static constexpr counter_t stable_period_factor {1u};
        static constexpr bool mode_switching_enabled {true};
        static constexpr bool mode_start_focused {true};
        static constexpr std::size_t probe_round_growth {4u};
        /// @brief Absolute cap on the re-walk floor: the per-variable floor is meant for small random formulas,
        /// and uncapped it alone made every re-walk on a 40,000-variable formula a ten-million-flip walk.
        static constexpr std::size_t local_search_rewalk_floor_cap {200'000u};
        /// @brief Bounds of the adaptive effort scale: it doubles after a walk that improves on every walk before
        /// it and halves otherwise, so a formula the walk keeps closing in on gets a growing share of the run and
        /// one it stalls on costs a shrinking one.
        static constexpr double local_search_scale_floor {0.125};
        static constexpr double local_search_scale_ceiling {4.0};
        /// @brief Rounds an opening strategy is split into; a strategy stops after two rounds without a new best.
        static constexpr std::size_t local_search_opening_rounds {10u};
        /// @brief Unsatisfied-clause count at or below which a walk is treated as a near miss worth more effort.
        static constexpr std::size_t local_search_near_miss_limit {2u};
        /// Walk only on formulas where failed-literal probing derived no unit: those are the ones a walk solves.
        static constexpr bool walk_only_without_probe_units {true};
        /// @brief Fixed seed: the probe feeds branching, so it must be reproducible across repeats.
        static constexpr std::uint64_t local_search_seed {0x5851f42d4c957f2dull};
        /// @brief The walk portfolio, cycled through by successive walks.
        /// @details probSAT's polynomial break rule is tuned for uniform 3-SAT and stalls on structured encodings,
        /// where low-noise WalkSAT succeeds; the two alternate, and the formula's longest clause decides which one
        /// opens (probSAT on pure 3-SAT, WalkSAT otherwise).
        static constexpr std::array<local_search::configuration, 4u> local_search_portfolio {
            local_search::configuration {local_search::strategy::probsat, 0u, 2.06, 0.9},
            local_search::configuration {local_search::strategy::walksat, 12u, 2.06, 0.9},
            local_search::configuration {local_search::strategy::probsat, 0u, 2.06, 0.9},
            local_search::configuration {local_search::strategy::walksat, 25u, 2.06, 0.9},
        };

        /// @brief Recursion bound of learned-clause minimization, as in CaDiCaL's `minimizedepth`.
        static constexpr std::uint32_t minimize_depth_limit {1000u};
        /// @brief Glue at or below which a learned clause enters the core tier on creation.
        static constexpr std::uint32_t core_glue_limit {2u};
        /// @brief Glue at or below which a learned clause enters the retained tier on creation.
        static constexpr std::uint32_t retained_glue_limit {6u};

        static constexpr std::uint8_t seen_flag {1u << 0u};
        static constexpr std::uint8_t poison_flag {1u << 1u};
        static constexpr std::uint8_t removable_flag {1u << 2u};
        static constexpr std::uint8_t keep_flag {1u << 3u};

        /// @brief One decision level: where its assignments start on the trail, and analysis scratch for it.
        struct control_frame final
        {
            std::uint32_t trail_begin {};
            literal decision {};
            std::uint32_t seen_count {};
            std::uint32_t seen_min_trail {};
        };

        [[nodiscard]] [[gnu::always_inline]] static inline std::uint32_t var_of(const literal lit) noexcept
        {
            return lit.variable_of().index();
        }

        [[nodiscard]] [[gnu::always_inline]] inline std::int8_t value_of(const literal lit) const noexcept { return values_[lit.raw()]; }

        [[nodiscard]] [[gnu::always_inline]] inline bool is_assigned(const std::uint32_t var) const noexcept
        {
            return values_[static_cast<std::size_t>(var) << 1u] != 0;
        }

        // ------------------------------------------------------------------------------------------------------
        // Episode setup
        // ------------------------------------------------------------------------------------------------------

        /// @brief Drops duplicate literals into `normalized_clause_scratch_`; returns false for a tautology.
        [[nodiscard]] bool normalize_clause(const std::span<const literal> literals) noexcept;

        [[nodiscard]] std::uint32_t find_max_variable(const std::span<const literal> assumptions) noexcept;

        void initialize_search_state(const solve_request& request) noexcept;

        /// @brief Decides whether the walk is worth running on this formula at all.
        [[nodiscard]] bool local_search_applicable(const std::uint32_t max_variable) const noexcept;

        /// @brief Runs one walk of the portfolio from the current saved phases and folds the result back into them.
        /// @details The phases are overwritten only when the walk ends closer to a model than any walk before it.
        /// A walk that merely wanders off to a different local minimum would otherwise redirect a search that was
        /// making progress on its own; measured on random 3-SAT that perturbation cost more than the walk itself.
        /// @param max_flips Flip budget for this walk.
        /// @param force Adopt the result regardless, used for the opening walk that has nothing to compare with.
        /// @return True if the walk's assignment was adopted, which includes every walk that found a model.
        bool walk_and_seed_phases(const std::size_t max_flips, const bool force) noexcept;

        /// @brief Gives every factoring-introduced variable the phase its definition implies under the current phases.
        /// @details The walk works on the formula as stated, so its assignment says nothing about the variables
        /// factoring introduced afterwards; left at a default phase they get decided against the model and the
        /// search pays thousands of conflicts to undo that (`gcp125_17`: 14,302 conflicts instead of 2,001). An
        /// introduced variable stands for "every literal of its group holds", and under a model of the original
        /// formula that phase satisfies both halves of its definition. Records are replayed in order, so a group
        /// that mentions an earlier introduced variable sees that variable's completed phase.
        void complete_introduced_phases() noexcept;

        /// @brief Runs one opening strategy in rounds, stopping as soon as the walk stops closing in on a model.
        /// @details Walk-friendly formulas (random k-SAT, colouring) improve steadily until they are solved;
        /// structured ones (bounded model checking, scheduling) stall after the first few hundred flips per
        /// variable, and on those the rest of a per-variable budget is pure overhead. Each round restarts from the
        /// best assignment so far; two rounds in a row without a new best end the strategy.
        /// @param budget Total flip budget of this strategy.
        /// @param force Adopt the first round regardless, for the very first walk of the episode.
        /// @return True if a model was found.
        bool walk_in_rounds(const std::size_t budget, const bool force) noexcept;

        /// @brief Failed-literal probing with the search's own propagator, plus lifting.
        /// @details Every unassigned variable with a binary occurrence is probed in both polarities at level 1. A
        /// conflict makes the probe's negation a root unit, learned as a unit clause (a RUP step for the proof)
        /// and propagated at the root; a literal implied by both polarities of a variable is a unit too
        /// (lifting). Kissat's probing phase derives units for 40% of the variables of `hanoi4` and `par16` and a
        /// quarter of `bmc-ibm-13`, and its ablation put that phase at 3x on `bmc-ibm-13`; the preprocessing
        /// pipeline's own `probing` pass only closes the formula under the units it already has. Effort is
        /// bounded by trail growth so the pass stays a small share of the formula.
        void probe_failed_literals() noexcept;

        /// @brief Tries the trivial assignments before any search: each polarity in forward and backward variable
        /// order, every variable a decision followed by propagation (Kissat's "lucky" phases).
        /// @details Structured encodings are often satisfied by one of these (`ii16a1`: solved outright where the
        /// search needed 2,000 conflicts), and each attempt costs at most one propagation of the whole formula.
        /// Phases are restored afterwards so a failed attempt leaves no trace; assumptions and a root conflict
        /// hand straight over to the search.
        /// @return True if an attempt assigned every variable without a conflict; the trail then holds the model.
        [[nodiscard]] bool try_lucky_assignments(const solve_request& request, const bool count_decisions = true) noexcept;

        /// @brief Arms the opening walk for this episode, on the walker prepared before preprocessing.
        /// @details Branching polarity otherwise starts from nothing, which is what makes runtime heavy-tailed on
        /// satisfiable instances near the ratio where they stop being easy. The opening walk fixes that, but it
        /// runs only after the search has spent `local_search_opening_probe_conflicts` (formulas the search
        /// finishes first never walk) and only when its budget stays under `local_search_opening_flip_cap`
        /// (larger formulas leave walking to the effort-proportional re-walk schedule).
        /// @return True if `run_opening_walk` should run once the probe budget is spent.
        [[nodiscard]] bool prepare_opening_walk() noexcept;

        /// @brief The opening walk: both strategies in rounds, phases seeded from the best assignment found.
        /// @details The first walk starts from the saved phases (all positive on a fresh solver, the previous
        /// model's neighbourhood in an incremental session); the second strategy gets the same budget whatever
        /// the first achieved; both stop early when they stall. The walk cannot affect soundness -- it only
        /// chooses which way each variable is tried first.
        /// @return True if a walk found a model, in which case the phases are that model.
        [[nodiscard]] bool run_opening_walk() noexcept;

        /// @brief Re-walks from the current phases once enough search effort has accrued since the last walk.
        /// @details The budget is a fixed share of the watch visits spent since the previous walk, so on an
        /// unsatisfiable formula the walks stay a small constant fraction of the run while on a satisfiable one
        /// they keep growing with the search until one of them lands on a model.
        void rewalk_if_due() noexcept;

        /// @brief Rebuilds every watch list and the unit list from the clause database.
        void rebuild_propagation_state() noexcept;

        /// @brief Registers a clause for propagation: two watches for size >= 2, the unit list otherwise.
        void attach_clause_for_propagation(const clause::ref_t ref) noexcept;

        /// @brief Assigns every root-level unit; returns false if one is already falsified or a clause is empty.
        [[nodiscard]] bool assign_root_units() noexcept;

        // ------------------------------------------------------------------------------------------------------
        // Search state primitives
        // ------------------------------------------------------------------------------------------------------

        [[gnu::always_inline]] inline void assign(const literal lit, const clause::ref_t reason) noexcept
        {
            const auto var = var_of(lit);
            values_[lit.raw()] = 1;
            values_[lit.raw() ^ 1u] = -1;
            levels_[var] = level_;
            reasons_[var] = reason;
            trail_positions_[var] = static_cast<std::uint32_t>(trail_.size());
            saved_phase_[var] = lit.is_negated() ? std::int8_t {-1} : std::int8_t {1};
            trail_.push_back(lit);
        }

        /// @brief Opens a new decision level; `decision` may be a placeholder for an already-true assumption.
        void new_level(const literal decision) noexcept
        {
            ++level_;
            control_.push_back(control_frame {static_cast<std::uint32_t>(trail_.size()), decision, 0u, 0u});
        }

        /// @brief Unassigns everything above `target_level` and returns those variables to the branching heap.
        void backtrack(const std::uint32_t target_level) noexcept;

        // ------------------------------------------------------------------------------------------------------
        // Unit propagation
        // ------------------------------------------------------------------------------------------------------

        /// @brief Propagates every unpropagated trail literal to fixpoint or to the first falsified clause.
        /// @details Two-watched-literal scheme with a blocking literal per watch. For each trail literal `l` the
        /// list of clauses watching `¬l` is compacted in place: a satisfied blocking literal keeps the entry
        /// untouched; a binary clause is decided from the entry alone; otherwise the other watched literal is
        /// computed by XOR (the pair is always the clause's first two literals), a replacement is searched from
        /// the third literal on and the watch moves to it, and only a clause with no replacement is unit or
        /// conflicting. Deleted clauses never appear here: every deleting pass rewrites the watch lists.
        /// @return Reference to the conflicting clause, or an invalid reference at fixpoint.
        [[nodiscard]] clause::ref_t propagate() noexcept;

        // ------------------------------------------------------------------------------------------------------
        // Conflict analysis, minimization and learning
        // ------------------------------------------------------------------------------------------------------

        void bump_clause(const clause::ref_t ref) noexcept;

        [[gnu::always_inline]] inline void analyze_literal(const literal lit, std::uint32_t& open, const bool want_chain) noexcept
        {
            const auto var = var_of(lit);
            const auto lvl = levels_[var];
            if (lvl == 0u)
            {
                if (want_chain)
                    collect_root_chain(var);
                return;
            }
            if ((flags_[var] & seen_flag) != 0u)
                return;
            flags_[var] |= seen_flag;
            analyzed_.push_back(var);
            auto& frame = control_[lvl];
            const auto position = trail_positions_[var];
            if (frame.seen_count++ == 0u)
            {
                levels_touched_.push_back(lvl);
                frame.seen_min_trail = position;
            }
            else if (position < frame.seen_min_trail)
                frame.seen_min_trail = position;
            if (lvl < level_)
                learned_.push_back(lit);
            else
                ++open;
        }

        /// @brief Records, in dependency order, the clauses that derive a root-level literal (proof chains only).
        void collect_root_chain(const std::uint32_t var) noexcept;

        /// @brief Records the clauses that justify removing a minimized literal (proof chains only).
        void collect_minimize_chain(const std::uint32_t var) noexcept;

        /// @brief Recursive removability test of a learned-clause literal through its implication ancestry.
        /// @details A literal is redundant when every literal of its reason is itself in the clause, fixed at the
        /// root, or recursively redundant. Two early rejections from CaDiCaL prune the search: a literal that is
        /// the only one of its level in the clause, or the earliest one of its level on the trail, cannot be
        /// implied by the others. `poison` caches failures and `removable` successes for the rest of this clause.
        /// @brief Whether a learned-clause literal is implied by the others (recursive minimization), marking what
        /// it visits.
        /// @details A depth-first walk over the implication graph with an explicit stack: a variable is removable
        /// when every literal of its reason is removable in turn, and the walk stops at the first one that is not,
        /// poisoning every variable on the path (each is settled once it is left, in post-order, exactly as the
        /// recursive formulation did). Written without recursion so the cost does not depend on whether the
        /// compiler inlines a recursion level: one build did and one did not, at a million calls apart.
        /// @param var Variable of the literal to test, at the top of the walk.
        /// @return True if the literal can be dropped from the learned clause.
        [[nodiscard]] bool minimize_literal(const std::uint32_t var) noexcept;

        /// @brief Runs first-UIP analysis from `conflict`, minimizes, bumps, and leaves the result in `learned_`.
        /// @details Resolution walks the trail backwards: literals of the current level are counted as `open`
        /// and their reasons resolved in trail order until one remains, the first UIP; lower-level literals are
        /// collected into the clause. Level-zero literals are dropped as they can never be falsified again.
        /// @return The glue (number of distinct decision levels) of the learned clause.
        [[nodiscard]] std::uint32_t analyze_conflict(const clause::ref_t conflict) noexcept;

        /// @brief Stores the learned clause, backjumps, asserts its first literal, and reports the proof step.
        void learn_clause(const std::uint32_t glue) noexcept;

        // ------------------------------------------------------------------------------------------------------
        // Assumptions
        // ------------------------------------------------------------------------------------------------------

        /// @brief Explains a falsified assumption by the assumptions that imply its negation.
        void analyze_final(const literal failed) noexcept;

        // ------------------------------------------------------------------------------------------------------
        // Restarts, reduction, inprocessing
        // ------------------------------------------------------------------------------------------------------

        /// @brief Restarts, keeping the trail prefix whose decisions the heap would repeat anyway.
        void restart() noexcept;

        void protect_trail_reasons(const bool protect) noexcept;

        /// @brief Reduces the learned-clause database and compacts the arena.
        void reduce() noexcept;

        /// @brief Slides live clauses down the arena and re-targets every watch, reason and unit reference.
        void collect_garbage() noexcept;

        /// @brief Runs an inprocessing epoch at the root when the scheduler says one is due.
        /// @return `unsatisfiable` if the simplified formula is refuted at the root, `unknown` otherwise.
        [[nodiscard]] status run_inprocess_if_due() noexcept;

        // ------------------------------------------------------------------------------------------------------
        // The search loop
        // ------------------------------------------------------------------------------------------------------

        /// @brief Per-conflict bookkeeping shared by search conflicts and root conflicts.
        void note_conflict(const std::uint32_t glue) noexcept;

        /// @brief Accounts for a root-level conflict: the formula is refuted, unless the conflict budget ran out on
        /// this very conflict, in which case the episode reports the limit like any other conflict would.
        [[nodiscard]] status root_conflict() noexcept;

        /// @brief Runs the CDCL search to a terminal outcome for one episode.
        /// @details Iterative by construction: a conflict is resolved by analysis, learning and a backjump applied
        /// in place, so the decision level is data rather than C++ stack depth. Assumptions occupy the first
        /// decision levels; a falsified assumption ends the episode with its core, a root-level conflict with the
        /// empty core.
        [[nodiscard]] status run_search(const solve_request& request) noexcept;

        // ------------------------------------------------------------------------------------------------------
        // Results
        // ------------------------------------------------------------------------------------------------------

        void build_internal_model() noexcept;

        void synchronize_search_outcome(const status solve_status) noexcept;

        // ------------------------------------------------------------------------------------------------------
        // Members
        // ------------------------------------------------------------------------------------------------------

        clause::database clause_database_ {};
        local_search local_search_ {};
        std::vector<variable> frozen_variables_scratch_ {};
        search_coordinator search_coordinator_ {};
        propagator propagator_ {};
        incremental_context incremental_context_ {};
        memory_governor memory_governor_ {};
        bank::watch_list watch_list_ {};
        variable_mapper variable_mapper_ {};
        proof_manager proof_manager_ {};
        simplify::flush_restore_manager flush_restore_manager_ {};
        simplify::scheduler::preprocess preprocess_scheduler_ {};
        simplify::scheduler::inprocess inprocess_scheduler_ {};
        store::clause_cold clause_cold_ {};
        stack::extension extension_stack_ {};
        model_reconstructor model_reconstructor_ {};

        // Search state, sized per episode.
        std::vector<std::int8_t> values_ {};
        std::vector<std::uint32_t> levels_ {};
        std::vector<clause::ref_t> reasons_ {};
        std::vector<std::uint32_t> trail_positions_ {};
        bank::trail trail_ {};
        std::vector<control_frame> control_ {};
        std::size_t propagated_ {};
        std::uint32_t level_ {};
        std::uint32_t variable_count_ {};
        std::uint32_t assumption_count_ {};
        std::uint32_t evsids_maintenance_interval_ {16u};

        // Branching.
        var_heap heap_ {};
        std::vector<std::int8_t> saved_phase_ {};
        std::vector<std::uint8_t> phase_scratch_ {};
        bool local_search_enabled_ {};
        std::size_t local_search_flips_per_variable_ {default_local_search_flips_per_variable};
        std::size_t local_search_effort_percent_ {default_local_search_effort_percent};
        std::size_t local_search_round_ {};
        counter_t next_local_search_at_ {};
        std::size_t visits_at_last_walk_ {};
        bool opening_walk_pending_ {};
        counter_t opening_walk_due_at_ {};
        bool local_search_prepared_ {};
        std::uint32_t local_search_variable_count_ {};
        bool raw_probe_ran_ {};
        counter_t raw_probe_conflicts_ {};
        std::vector<std::int8_t> entry_phase_snapshot_ {};
        std::size_t raw_probe_propagation_limit_ {};
        bool raw_phase_active_ {};
        std::size_t database_literal_count_ {};
        std::vector<std::uint32_t> watch_count_scratch_ {};
        var_heap heap_snapshot_ {};
        std::vector<std::uint8_t> arena_image_ {};
        std::uint64_t lucky_success_count_ {};
        std::uint64_t probe_count_ {};
        std::uint64_t probe_unit_count_ {};
        std::uint64_t probe_lifted_count_ {};
        bool root_refuted_ {};
        bool focused_mode_ {};
        counter_t mode_period_ {};
        counter_t mode_switch_at_ {};
        counter_t stable_restart_interval_ {4096u};
        std::vector<std::uint8_t> probe_marks_ {};
        std::vector<literal> lifted_scratch_ {};
        std::vector<literal> marked_scratch_ {};
        struct minimize_frame final
        {
            std::uint32_t var;
            const literal* cursor;
            const literal* end;
        };
        std::vector<minimize_frame> minimize_stack_ {};
        std::vector<std::int8_t> probe_phase_scratch_ {};
        std::vector<std::int8_t> opening_phase_snapshot_ {};
        std::size_t best_walk_unsatisfied_ {};
        std::size_t best_walk_config_ {};
        double local_search_scale_ {1.0};

        // Analysis scratch.
        std::vector<std::uint8_t> flags_ {};
        std::vector<std::uint32_t> analyzed_ {};
        std::vector<std::uint32_t> minimized_ {};
        std::vector<std::uint32_t> levels_touched_ {};
        std::vector<literal> learned_ {};
        std::vector<std::uint32_t> level_stamps_ {};
        std::uint32_t glue_stamp_ {};
        std::vector<std::uint32_t> chain_stamps_ {};
        std::uint32_t chain_stamp_ {};
        std::vector<clause::ref_t> resolution_chain_refs_ {};
        std::vector<clause::ref_t> root_chain_refs_ {};
        std::vector<clause::ref_t> minimize_chain_refs_ {};
        std::vector<proof::clause::id> antecedents_scratch_ {};

        // Clause bookkeeping.
        std::vector<clause::ref_t> unit_clause_refs_ {};
        std::vector<std::uint32_t> forwarding_ {};
        std::vector<literal> normalized_clause_scratch_ {};
        std::vector<std::uint32_t> literal_stamps_ {};
        std::uint32_t literal_stamp_ {};
        std::size_t original_clause_count_ {};
        /// @brief Highest variable index the problem was stated over, independent of later simplification.
        std::uint32_t max_problem_variable_ {};

        // Results and counters.
        std::vector<literal> internal_model_ {};
        std::vector<literal> failed_core_ {};
        std::size_t propagation_assignment_count_ {};
        std::size_t watch_entry_scan_count_ {};
        std::size_t binary_watch_scan_count_ {};
        std::size_t binary_watch_conflict_count_ {};
        std::size_t learned_clause_shrink_event_count_ {};
        counter_t learned_clause_glue_total_ {};
        counter_t learned_clause_glue_sample_count_ {};
        std::uint32_t minimized_clause_count_ {};
        std::uint32_t shrunk_clause_count_ {};
        std::uint32_t promoted_clause_count_ {};
        status status_ {status::unknown};
    };
}
