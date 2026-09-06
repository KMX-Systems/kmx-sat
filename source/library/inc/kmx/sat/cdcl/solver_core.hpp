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
        solver_core() noexcept
        {
            proof_manager_.set_event_buffering(false);
            clause_database_.storage_of().set_proof_id_tracking(false);
            rebind_internal_views();
        }

        /// @brief Resets the core to an empty, freshly bound state without invalidating internal helper pointers.
        /// @throws None (noexcept).
        void reset() noexcept
        {
            clause_database_ = {};
            clause_database_.storage_of().set_proof_id_tracking(false);
            search_coordinator_ = {};
            propagator_ = {};
            proof_manager_ = {};
            proof_manager_.set_event_buffering(false);
            incremental_context_ = {};
            memory_governor_ = {};
            watch_list_ = {};
            variable_mapper_ = {};
            clause_cold_ = {};
            flush_restore_manager_ = {};
            extension_stack_.clear_all();
            preprocess_scheduler_ = {};
            inprocess_scheduler_ = {};
            heap_ = {};
            saved_phase_.clear();
            unit_clause_refs_.clear();
            original_clause_count_ = 0u;
            max_problem_variable_ = 0u;
            internal_model_.clear();
            failed_core_.clear();
            reset_episode_counters();
            watch_list_.reset_diagnostics();
            status_ = status::unknown;
            rebind_internal_views();
        }

        /// @brief Runs one solve episode under the given request.
        /// @param request Solve configuration for this episode (assumptions, limits, mode flags).
        /// @return Internal-level terminal status for this episode.
        /// @throws None (noexcept).
        status solve(const solve_request& request) noexcept
        {
            internal_model_.clear();
            failed_core_.clear();
            reset_episode_counters();
            clause_cold_.reset();

            incremental_context_.begin_solve_epoch();
            memory_governor_.reset_epoch_usage();
            clause_database_.clear_reason_clauses();
            preprocess_scheduler_.clear_abort();
            preprocess_scheduler_.set_inprocess_telemetry_snapshot(
                inprocess_scheduler_.conflict_density_ema(), inprocess_scheduler_.structural_gain_ema(),
                inprocess_scheduler_.restart_pressure_ema(), inprocess_scheduler_.reduction_pressure_ema(),
                inprocess_scheduler_.learned_clause_pressure_ema());
            // A caller-supplied pass set replaces the default one; zero keeps the default rather than meaning
            // "no passes", matching how `solve_request::enabled_pass_mask` is documented and defaulted.
            if (request.enabled_pass_mask != 0u)
                preprocess_scheduler_.set_enabled_pass_mask(request.enabled_pass_mask);

            // Anything the caller can still name after simplification must survive it. Assumption variables are
            // exactly that: eliminating one discards the clauses that constrain it, and the episode then reports
            // satisfiable with a model that contradicts the assumption it was given.
            frozen_variables_scratch_.clear();
            frozen_variables_scratch_.reserve(request.assumptions.size());
            for (const auto assumption: request.assumptions)
                frozen_variables_scratch_.push_back(assumption.variable_of());
            preprocess_scheduler_.set_frozen_variables(frozen_variables_scratch_);
            preprocess_scheduler_.set_problem_variable_count(find_max_variable(request.assumptions));

            // The walker takes the formula as stated, before preprocessing rewrites it. Factoring replaces the
            // at-most-one clauses of a colouring instance with definitions of fresh variables, and on that formula
            // the walk that solves `gcp125_17` in a third of a second finds nothing in thirty. A model of the
            // original formula seeds the original variables' phases; the fresh variables follow by propagation.
            local_search_variable_count_ = find_max_variable(request.assumptions);
            local_search_prepared_ = local_search_applicable(local_search_variable_count_) &&
                                     local_search_.prepare(clause_database_, local_search_variable_count_);

            preprocess_scheduler_.run_initial_pipeline();
            preprocess_scheduler_.report_pass_summary();
            inprocess_scheduler_.clear_abort();

            const auto finalize_epoch = [this](const status result) noexcept
            {
                status_ = result;
                incremental_context_.end_solve_epoch();
                incremental_context_.reset_transient_state();
                search_coordinator_.clear_variable_selectability_filter();
                synchronize_search_outcome(status_);
                return status_;
            };

            variable_count_ = find_max_variable(request.assumptions);
            initialize_search_state(request);
            opening_walk_pending_ = prepare_opening_walk();
            opening_walk_due_at_ = search_coordinator_.conflict_event_count() + local_search_opening_probe_conflicts;
            rebuild_propagation_state();
            root_refuted_ = false;
            probe_failed_literals();

            search_coordinator_.apply_assumptions(request);
            propagator_.reset_episode_state();
            propagator_.set_pending_assumption_count(request.assumptions.size());
            (void) propagator_.propagate_assumptions();

            // A conflict found by root propagation before the search is consumed by the propagator (the trail
            // literal that exposed it counts as propagated), so it is reported here rather than rediscovered.
            status_ = root_refuted_ ? root_conflict() : try_lucky_assignments(request) ? status::satisfiable : run_search(request);

            if (status_ == status::satisfiable)
                build_internal_model();

            (void) run_inprocess_if_due();

            return finalize_epoch(status_);
        }

        /// @brief Runs one solve episode restricted to the given internal assumption literals.
        /// @param assumptions Internal assumption literals for this episode.
        /// @return Internal-level terminal status for this episode.
        /// @throws None (noexcept).
        status solve_under_assumptions(const std::span<const literal> assumptions) noexcept
        {
            solve_request request {};
            request.assumptions.assign(assumptions.begin(), assumptions.end());
            return solve(request);
        }

        /// @brief Pre-reserves capacity for the given variable count.
        /// @param variable_bound Upper bound on variable index.
        /// @throws None (noexcept).
        void reserve(const variable::index_t variable_bound) noexcept
        {
            if (variable_bound == 0)
                return;
            watch_list_.reserve(static_cast<std::size_t>(variable_bound) * 2u + 2u);
            heap_.resize(variable_bound);
            saved_phase_.reserve(static_cast<std::size_t>(variable_bound) + 1u);
        }

        /// @brief Registers an original (non-redundant) problem clause with the internal clause database.
        /// @details Duplicate literals are dropped and a tautology is not stored at all: both would put two watches
        /// of one clause on the same literal, which the propagation loop is not built to tolerate.
        /// @param literals Internal literals composing the clause.
        /// @throws None (noexcept).
        void add_problem_clause(const std::span<const literal> literals) noexcept
        {
            ++original_clause_count_;
            for (const auto lit: literals)
                if (lit.variable_of().index() > max_problem_variable_)
                    max_problem_variable_ = lit.variable_of().index();

            if (!normalize_clause(literals))
                return;
            const auto ref = clause_database_.add_clause(normalized_clause_scratch_, false);
            proof_manager_.on_add_original(ref, normalized_clause_scratch_);
        }

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
        void attach_proof_tracer(const proof::tracer::view& sink) noexcept
        {
            auto& storage = clause_database_.storage_of();
            storage.enable_proof_ids(clause_database_.irredundant_refs());
            for (const auto ref: clause_database_.redundant_refs())
                if (storage.is_alive(ref))
                    (void) storage.assign_proof_id(ref);
            proof_manager_.register_tracer(sink);
            // Ids for everything already in the database: events were not dispatched while no consumer existed.
            const auto adopt = [this](const clause::ref_t ref) noexcept { proof_manager_.adopt_clause(ref); };
            clause_database_.iterate_irredundant(adopt);
            clause_database_.iterate_redundant(adopt);
        }

        /// @brief Returns whether any proof format is active through the core-owned proof manager.
        bool proof_enabled() const noexcept { return proof_manager_.has_enabled_formats(); }

        bool proof_checkers_valid() const noexcept { return proof_manager_.validate_checkers(); }

        /// @brief Configures decision-heuristic maintenance intervals.
        /// @param conflict_maintenance_interval Conflicts between EVSIDS renormalizations (0 disables the cadence;
        /// the heap still renormalizes on its own before its scores can overflow).
        /// @param chb_decay_interval Retained for the option surface; the search engine no longer runs CHB.
        /// @param restart_decay_interval Retained for the option surface; the search engine no longer runs CHB.
        void set_decision_maintenance_intervals(const std::uint32_t conflict_maintenance_interval, const std::uint32_t chb_decay_interval,
                                                const std::uint32_t restart_decay_interval) noexcept
        {
            evsids_maintenance_interval_ = conflict_maintenance_interval;
            search_coordinator_.set_decision_maintenance_intervals(conflict_maintenance_interval, chb_decay_interval,
                                                                   restart_decay_interval);
        }

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
            local_search_effort_percent_ = percent > 100u ? 100u : percent;
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
        std::array<std::uint32_t, 3> decision_maintenance_intervals() const noexcept
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
            search_coordinator_.set_glue_restart_threshold(percent == 0u ? 0.0 : static_cast<double>(percent) / 100.0);
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

        inprocess_telemetry_snapshot current_inprocess_telemetry_snapshot() const noexcept
        {
            return inprocess_telemetry_snapshot {
                inprocess_scheduler_.conflict_density_ema(),        inprocess_scheduler_.structural_gain_ema(),
                inprocess_scheduler_.restart_pressure_ema(),        inprocess_scheduler_.reduction_pressure_ema(),
                inprocess_scheduler_.learned_clause_pressure_ema(),
            };
        }

        const simplify::preprocessing_profile_selector::pass_plan& preprocess_current_pass_plan() const noexcept
        {
            return preprocess_scheduler_.profile_selector().current_pass_plan();
        }

    private:
        // ------------------------------------------------------------------------------------------------------
        // Wiring
        // ------------------------------------------------------------------------------------------------------

        void rebind_internal_views() noexcept
        {
            search_coordinator_.attach_database(clause_database_);
            flush_restore_manager_.attach_database(clause_database_);
            preprocess_scheduler_.attach_memory_governor(memory_governor_);
            preprocess_scheduler_.attach_clause_database(clause_database_);
            preprocess_scheduler_.attach_watch_list(watch_list_);
            preprocess_scheduler_.attach_variable_mapper(variable_mapper_);
            preprocess_scheduler_.attach_proof_manager(proof_manager_);
            preprocess_scheduler_.attach_extension_stack(extension_stack_);
            model_reconstructor_.attach_extension_stack(extension_stack_);
            // Simplification passes announce new or rewritten clauses through this sink. Watches are rebuilt
            // wholesale after every pipeline and every epoch, so there is nothing for the sink to do.
            preprocess_scheduler_.attach_clause_sink([](const clause::ref_t) noexcept {});
            inprocess_scheduler_.attach_memory_governor(memory_governor_);
            inprocess_scheduler_.attach_clause_database(clause_database_);
            inprocess_scheduler_.attach_watch_list(watch_list_);
            inprocess_scheduler_.attach_variable_mapper(variable_mapper_);
            inprocess_scheduler_.attach_proof_manager(proof_manager_);
        }

        void reset_episode_counters() noexcept
        {
            propagation_assignment_count_ = 0u;
            watch_entry_scan_count_ = 0u;
            binary_watch_scan_count_ = 0u;
            binary_watch_conflict_count_ = 0u;
            learned_clause_shrink_event_count_ = 0u;
            learned_clause_glue_total_ = 0u;
            learned_clause_glue_sample_count_ = 0u;
            minimized_clause_count_ = 0u;
            shrunk_clause_count_ = 0u;
            promoted_clause_count_ = 0u;
        }

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
        /// @brief Fixed seed: the probe feeds branching, so it must be reproducible across repeats.
        static constexpr std::uint64_t local_search_seed {0x5851f42d4c957f2dull};
        /// @brief The walk portfolio, cycled through by successive walks.
        /// @details probSAT's polynomial break rule is tuned for uniform 3-SAT and stalls on structured encodings,
        /// where low-noise WalkSAT succeeds; the two alternate, and the formula's longest clause decides which one
        /// opens (probSAT on pure 3-SAT, WalkSAT otherwise).
        static constexpr std::array<local_search::configuration, 4> local_search_portfolio {
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
        [[nodiscard]] bool normalize_clause(const std::span<const literal> literals) noexcept
        {
            normalized_clause_scratch_.clear();
            if (++literal_stamp_ == 0u)
            {
                std::fill(literal_stamps_.begin(), literal_stamps_.end(), 0u);
                literal_stamp_ = 1u;
            }
            for (const auto lit: literals)
            {
                const auto slot = static_cast<std::size_t>(lit.raw());
                if (slot + 1u >= literal_stamps_.size())
                    literal_stamps_.resize(slot + 2u, 0u);
                if (literal_stamps_[slot ^ 1u] == literal_stamp_)
                    return false;
                if (literal_stamps_[slot] == literal_stamp_)
                    continue;
                literal_stamps_[slot] = literal_stamp_;
                normalized_clause_scratch_.push_back(lit);
            }
            return true;
        }

        [[nodiscard]] std::uint32_t find_max_variable(const std::span<const literal> assumptions) const noexcept
        {
            // Seeded from what the problem was stated over, not from what survives simplification. Bounded
            // variable elimination removes a variable's clauses outright, so scanning only the current database
            // would shrink the variable range after preprocessing and the reported model would omit variables.
            std::uint32_t max_variable = max_problem_variable_;
            const auto& storage = clause_database_.storage_of();
            const auto process_clause = [&storage, &max_variable](const clause::ref_t ref) noexcept
            {
                for (const auto lit: storage.view_literals(ref))
                    if (lit.variable_of().index() > max_variable)
                        max_variable = lit.variable_of().index();
            };
            clause_database_.iterate_irredundant(process_clause);
            clause_database_.iterate_redundant(process_clause);
            for (const auto lit: assumptions)
                if (lit.variable_of().index() > max_variable)
                    max_variable = lit.variable_of().index();
            return max_variable;
        }

        void initialize_search_state(const solve_request& request) noexcept
        {
            const auto slots = static_cast<std::size_t>(variable_count_) + 1u;
            values_.assign(slots * 2u, 0);
            levels_.assign(slots, 0u);
            reasons_.assign(slots, clause::ref_t {});
            trail_positions_.assign(slots, 0u);
            flags_.assign(slots, 0u);
            level_stamps_.assign(slots + 1u, 0u);
            chain_stamps_.assign(slots, 0u);
            trail_.clear();
            trail_.reserve(slots);
            control_.clear();
            control_.reserve(slots + 1u);
            control_.push_back(control_frame {});
            level_ = 0u;
            propagated_ = 0u;
            assumption_count_ = static_cast<std::uint32_t>(request.assumptions.size());
            analyzed_.clear();
            minimized_.clear();
            levels_touched_.clear();
            glue_stamp_ = 0u;
            chain_stamp_ = 0u;

            // Saved phases persist across episodes, so a later solve resumes near the assignment the previous
            // one settled on; only variables new to this episode get the default polarity.
            if (saved_phase_.size() < slots)
                saved_phase_.resize(slots, std::int8_t {1});

            heap_.resize(variable_count_);
            heap_.clear();
            for (std::uint32_t index = 1u; index <= variable_count_; ++index)
                heap_.push(index);
            local_search_enabled_ = false;
            opening_walk_pending_ = false;
            // Focused first: frequent restarts while the search has no history worth keeping, then the
            // configured (stable) schedule, the periods doubling so both modes get equal time in the long run.
            focused_mode_ = mode_switching_enabled && mode_start_focused;
            mode_period_ = mode_initial_period;
            mode_switch_at_ = mode_switching_enabled ? search_coordinator_.conflict_event_count() + mode_period_
                                                     : std::numeric_limits<counter_t>::max();
            search_coordinator_.set_restart_interval(focused_mode_ ? focused_restart_interval : stable_restart_interval_);
            next_local_search_at_ = local_search_initial_interval;
            visits_at_last_walk_ = 0u;
            best_walk_unsatisfied_ = std::numeric_limits<std::size_t>::max();
            local_search_scale_ = 1.0;
        }

        /// @brief Decides whether the walk is worth running on this formula at all.
        [[nodiscard]] bool local_search_applicable(const std::uint32_t max_variable) const noexcept
        {
            if (max_variable == 0u || max_variable > local_search_variable_limit)
                return false;
            const auto clause_count = clause_database_.stats_snapshot().irredundant_count;
            return clause_count >= local_search_minimum_clause_count && clause_count <= local_search_clause_limit;
        }

        /// @brief Runs one walk of the portfolio from the current saved phases and folds the result back into them.
        /// @details The phases are overwritten only when the walk ends closer to a model than any walk before it.
        /// A walk that merely wanders off to a different local minimum would otherwise redirect a search that was
        /// making progress on its own; measured on random 3-SAT that perturbation cost more than the walk itself.
        /// @param max_flips Flip budget for this walk.
        /// @param force Adopt the result regardless, used for the opening walk that has nothing to compare with.
        /// @return True if the walk's assignment was adopted, which includes every walk that found a model.
        bool walk_and_seed_phases(const std::size_t max_flips, const bool force) noexcept
        {
            // Once a strategy has produced the best assignment seen, later walks keep using it: alternating with
            // a rule that stalls on this formula would only waste every second walk.
            const auto config_index = best_walk_config_ < local_search_portfolio.size() ? best_walk_config_
                                                                                        : local_search_round_ % local_search_portfolio.size();
            const auto config = local_search_portfolio[config_index];
            ++local_search_round_;
            phase_scratch_.assign(static_cast<std::size_t>(variable_count_) + 1u, 0u);
            for (std::uint32_t index = 1u; index <= variable_count_; ++index)
                phase_scratch_[index] = saved_phase_[index] > 0 ? 1u : 0u;
            const auto solved = local_search_.walk(config, max_flips, phase_scratch_);
            const auto unsatisfied = local_search_.best_unsatisfied_count();
            // Improvement means a near miss or a quarter fewer unsatisfied clauses: creeping down by one clause
            // per walk on a formula the walk cannot solve kept doubling the re-walk budget and reseeding the
            // phases of a search that was doing fine on its own.
            const bool improved = unsatisfied < best_walk_unsatisfied_ &&
                                  (unsatisfied <= local_search_near_miss_limit || unsatisfied * 4u <= best_walk_unsatisfied_ * 3u);
            if (!force && !solved && !improved)
                return false;
            best_walk_unsatisfied_ = unsatisfied;
            best_walk_config_ = config_index;
            const auto assignment = local_search_.best_assignment();
            for (std::uint32_t index = 1u; index <= variable_count_ && index < assignment.size(); ++index)
                saved_phase_[index] = assignment[index] != 0u ? std::int8_t {1} : std::int8_t {-1};
            complete_introduced_phases();
            return true;
        }

        /// @brief Gives every factoring-introduced variable the phase its definition implies under the current phases.
        /// @details The walk works on the formula as stated, so its assignment says nothing about the variables
        /// factoring introduced afterwards; left at a default phase they get decided against the model and the
        /// search pays thousands of conflicts to undo that (`gcp125_17`: 14,302 conflicts instead of 2,001). An
        /// introduced variable stands for "every literal of its group holds", and under a model of the original
        /// formula that phase satisfies both halves of its definition. Records are replayed in order, so a group
        /// that mentions an earlier introduced variable sees that variable's completed phase.
        void complete_introduced_phases() noexcept
        {
            const auto witnesses = extension_stack_.witness_literals();
            for (const auto& record: extension_stack_.records())
            {
                const auto* factor = std::get_if<factor_transformation>(&record.payload);
                if (factor == nullptr)
                    continue;
                const auto introduced = factor->introduced_variable.index();
                if (introduced == 0u || introduced > variable_count_)
                    continue;
                bool all_true = true;
                for (auto position = factor->witness_begin; position < factor->witness_end && position < witnesses.size(); ++position)
                {
                    const auto lit = witnesses[position];
                    if (lit.raw() == 0u)
                        break;
                    const auto phase = saved_phase_[lit.variable_of().index()];
                    if (lit.is_negated() ? phase >= 0 : phase <= 0)
                    {
                        all_true = false;
                        break;
                    }
                }
                saved_phase_[introduced] = all_true ? std::int8_t {1} : std::int8_t {-1};
            }
        }

        /// @brief Runs one opening strategy in rounds, stopping as soon as the walk stops closing in on a model.
        /// @details Walk-friendly formulas (random k-SAT, colouring) improve steadily until they are solved;
        /// structured ones (bounded model checking, scheduling) stall after the first few hundred flips per
        /// variable, and on those the rest of a per-variable budget is pure overhead. Each round restarts from the
        /// best assignment so far; two rounds in a row without a new best end the strategy.
        /// @param budget Total flip budget of this strategy.
        /// @param force Adopt the first round regardless, for the very first walk of the episode.
        /// @return True if a model was found.
        bool walk_in_rounds(const std::size_t budget, const bool force) noexcept
        {
            const auto rounds = local_search_opening_rounds;
            const auto round_flips = std::max<std::size_t>(1u, budget / rounds);
            const auto config_index = local_search_round_ % local_search_portfolio.size();
            std::size_t stalled {};
            for (std::size_t round = 0u; round < rounds; ++round)
            {
                const auto before = best_walk_unsatisfied_;
                local_search_round_ = config_index;
                if (walk_and_seed_phases(round_flips, force && round == 0u) && best_walk_unsatisfied_ == 0u)
                    return true;
                stalled = best_walk_unsatisfied_ < before ? 0u : stalled + 1u;
                if (stalled >= 2u)
                    break;
                // Walk-friendly formulas are within two unsatisfied clauses after a few rounds (`lran_f2000`
                // after one, `gcp125_17` from the start); the ones that creep from seven to one over ten rounds
                // never finish, and on a formula the search solves in a few thousand conflicts those rounds were
                // most of the run (`hanoi4`: 79% of its instructions). Past the third round only a near miss
                // earns the rest of the budget.
                if (round >= 1u && best_walk_unsatisfied_ > local_search_near_miss_limit)
                    break;
            }
            local_search_round_ = config_index + 1u;
            return false;
        }

        /// @brief Failed-literal probing with the search's own propagator, plus lifting.
        /// @details Every unassigned variable with a binary occurrence is probed in both polarities at level 1. A
        /// conflict makes the probe's negation a root unit, learned as a unit clause (a RUP step for the proof)
        /// and propagated at the root; a literal implied by both polarities of a variable is a unit too
        /// (lifting). Kissat's probing phase derives units for 40% of the variables of `hanoi4` and `par16` and a
        /// quarter of `bmc-ibm-13`, and its ablation put that phase at 3x on `bmc-ibm-13`; the preprocessing
        /// pipeline's own `probing` pass only closes the formula under the units it already has. Effort is
        /// bounded by trail growth so the pass stays a small share of the formula.
        void probe_failed_literals() noexcept
        {
            // A probed unit is a RUP step without antecedents; the LRAT consumers need chains, so with a proof
            // consumer attached the search derives its units with the analysis that produces them.
            if (variable_count_ == 0u || level_ != 0u || proof_manager_.has_active_consumers())
                return;
            if (!assign_root_units())
                return;
            if (propagate().valid())
            {
                root_refuted_ = true;
                return;
            }
            const auto has_binary = [this](const literal lit) noexcept
            {
                for (const auto& entry: watch_list_.list_at(lit.raw()))
                    if (entry.is_binary())
                        return true;
                return false;
            };
            // The first round is cheap; each round after a productive one gets four times the budget, so a formula
            // where probing yields nothing (random 3-SAT, the scheduling instances) pays a few thousand
            // assignments and one where it pays (`bmc-ibm-13`: 131 units) gets the full pass.
            const auto base_effort = static_cast<std::size_t>(clause_database_.stats_snapshot().irredundant_count) * probe_effort_per_clause +
                                     probe_minimum_effort;
            std::size_t effort = base_effort;
            probe_marks_.assign(static_cast<std::size_t>(variable_count_) * 2u + 2u, 0u);
            const auto clear_probe_marks = [this]() noexcept
            {
                for (const auto marked: marked_scratch_)
                    probe_marks_[marked.raw()] = 0u;
                marked_scratch_.clear();
            };
            const auto learn_root_unit = [this](const literal unit) noexcept
            {
                const std::array<literal, 1> unit_clause {unit};
                const auto ref = clause_database_.add_clause(unit_clause, true);
                if (!ref.valid())
                    return false;
                unit_clause_refs_.push_back(ref);
                if (proof_manager_.has_active_consumers())
                    proof_manager_.on_add_derived(ref, unit_clause);
                // A probed unit is a learned clause in every sense, including the episode's retention accounting.
                search_coordinator_.note_learned_clause();
                incremental_context_.retain_learned_clause();
                assign(unit, ref);
                ++probe_unit_count_;
                if (propagate().valid())
                {
                    root_refuted_ = true;
                    return false;
                }
                return true;
            };
            // Rounds: a unit derived in one round falsifies further probes in the next; three rounds cover what
            // the effort bound leaves.
            for (std::size_t round = 0u; round < probe_rounds; ++round)
            {
            if (round != 0u)
                effort = base_effort * probe_round_growth;
            const auto units_before = probe_unit_count_ + probe_lifted_count_;
            for (std::uint32_t var = 1u; var <= variable_count_ && effort != 0u; ++var)
            {
                if (is_assigned(var))
                    continue;
                const literal positive {variable {var}, false};
                if (!has_binary(positive) && !has_binary(positive.negated()))
                    continue;
                // First polarity: remember what it implies for lifting against the second.
                lifted_scratch_.clear();
                clear_probe_marks();
                bool positive_failed = false;
                {
                    ++probe_count_;
                    const auto trail_before = trail_.size();
                    new_level(positive);
                    assign(positive, clause::ref_t {});
                    const auto conflict = propagate();
                    const auto grown = trail_.size() - trail_before;
                    effort = grown >= effort ? 0u : effort - grown;
                    if (conflict.valid())
                        positive_failed = true;
                    else
                        for (auto index = trail_before + 1u; index < trail_.size(); ++index)
                        {
                            probe_marks_[trail_[index].raw()] = 1u;
                            marked_scratch_.push_back(trail_[index]);
                        }
                    backtrack(0u);
                }
                if (positive_failed)
                {
                    clear_probe_marks();
                    if (!learn_root_unit(positive.negated()))
                        return;
                    continue;
                }
                if (effort == 0u)
                {
                    // Leaving with the positive probe's marks set would let a later variable's negative probe
                    // lift a literal only this probe implied: a wrong unit, and a wrong UNSAT verdict on
                    // `bmc-ibm-1/12/13` before this was caught.
                    clear_probe_marks();
                    break;
                }
                // Second polarity: a conflict makes the first a unit; otherwise the intersection of the two
                // implied sets lifts to units.
                {
                    ++probe_count_;
                    const literal negative = positive.negated();
                    const auto trail_before = trail_.size();
                    new_level(negative);
                    assign(negative, clause::ref_t {});
                    const auto conflict = propagate();
                    const auto grown = trail_.size() - trail_before;
                    effort = grown >= effort ? 0u : effort - grown;
                    if (!conflict.valid())
                        for (auto index = trail_before + 1u; index < trail_.size(); ++index)
                            if (probe_marks_[trail_[index].raw()] != 0u)
                                lifted_scratch_.push_back(trail_[index]);
                    backtrack(0u);
                    clear_probe_marks();
                    if (conflict.valid())
                    {
                        if (!learn_root_unit(positive))
                            return;
                        continue;
                    }
                }
                for (const auto lifted: lifted_scratch_)
                {
                    if (is_assigned(var_of(lifted)))
                        continue;
                    ++probe_lifted_count_;
                    if (!learn_root_unit(lifted))
                        return;
                }
            }
            if (probe_unit_count_ + probe_lifted_count_ == units_before)
                break;
            }
        }

        /// @brief Tries the trivial assignments before any search: each polarity in forward and backward variable
        /// order, every variable a decision followed by propagation (Kissat's "lucky" phases).
        /// @details Structured encodings are often satisfied by one of these (`ii16a1`: solved outright where the
        /// search needed 2,000 conflicts), and each attempt costs at most one propagation of the whole formula.
        /// Phases are restored afterwards so a failed attempt leaves no trace; assumptions and a root conflict
        /// hand straight over to the search.
        /// @return True if an attempt assigned every variable without a conflict; the trail then holds the model.
        [[nodiscard]] bool try_lucky_assignments(const solve_request& request) noexcept
        {
            if (!request.assumptions.empty() || variable_count_ == 0u || level_ != 0u || root_refuted_)
                return false;
            // Unit clauses live outside the watch lists; without them assigned first an attempt can satisfy
            // every watched clause and still contradict a unit, which is what the model verifier caught.
            if (!assign_root_units())
                return false;
            if (propagate().valid())
            {
                root_refuted_ = true;
                return false;
            }
            const auto root_trail = trail_.size();
            for (std::uint32_t strategy = 0u; strategy < 4u; ++strategy)
            {
                const bool negative = (strategy & 1u) != 0u;
                const bool backward = strategy >= 2u;
                bool failed = false;
                for (std::uint32_t step = 1u; step <= variable_count_ && !failed; ++step)
                {
                    const auto var = backward ? variable_count_ + 1u - step : step;
                    if (is_assigned(var))
                        continue;
                    // Each attempt's decisions count against the request's decision limit like any other, so a
                    // limited request that would have stopped in the search stops here too.
                    search_coordinator_.note_decision();
                    if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
                    {
                        backtrack(0u);
                        if (!opening_phase_snapshot_.empty())
                            std::copy(opening_phase_snapshot_.begin(), opening_phase_snapshot_.end(), saved_phase_.begin());
                        return false;
                    }
                    const literal decision {variable {var}, negative};
                    new_level(decision);
                    assign(decision, clause::ref_t {});
                    failed = propagate().valid();
                }
                if (!failed && trail_.size() == static_cast<std::size_t>(variable_count_))
                {
                    ++lucky_success_count_;
                    return true;
                }
                backtrack(0u);
                propagated_ = root_trail;
            }
            if (!opening_phase_snapshot_.empty())
                std::copy(opening_phase_snapshot_.begin(), opening_phase_snapshot_.end(), saved_phase_.begin());
            return false;
        }

        /// @brief Arms the opening walk for this episode, on the walker prepared before preprocessing.
        /// @details Branching polarity otherwise starts from nothing, which is what makes runtime heavy-tailed on
        /// satisfiable instances near the ratio where they stop being easy. The opening walk fixes that, but it
        /// runs only after the search has spent `local_search_opening_probe_conflicts` (formulas the search
        /// finishes first never walk) and only when its budget stays under `local_search_opening_flip_cap`
        /// (larger formulas leave walking to the effort-proportional re-walk schedule).
        /// @return True if `run_opening_walk` should run once the probe budget is spent.
        [[nodiscard]] bool prepare_opening_walk() noexcept
        {
            local_search_round_ = 0u;
            best_walk_config_ = local_search_portfolio.size();
            local_search_enabled_ = local_search_prepared_;
            if (!local_search_enabled_)
                return false;
            if (local_search_.maximum_clause_size() > 3u)
                local_search_round_ = 1u;

            local_search_.set_seed(local_search_seed);
            const auto budget = static_cast<std::size_t>(local_search_variable_count_) * local_search_flips_per_variable_;
            if (budget == 0u || budget > local_search_opening_flip_cap)
                return false;
            // The walk starts from the phases as they are now, not from what the probe leaves behind: the probe's
            // phase-saved assignment is a local minimum the walk climbs out of slowly, and starting there turned
            // a 0.2 s solve of `lran_f2000` into a timeout. Keeping the snapshot makes the walk's trajectory
            // independent of the probe.
            opening_phase_snapshot_.assign(saved_phase_.begin(), saved_phase_.end());
            return true;
        }

        /// @brief The opening walk: both strategies in rounds, phases seeded from the best assignment found.
        /// @details The first walk starts from the saved phases (all positive on a fresh solver, the previous
        /// model's neighbourhood in an incremental session); the second strategy gets the same budget whatever
        /// the first achieved; both stop early when they stall. The walk cannot affect soundness -- it only
        /// chooses which way each variable is tried first.
        /// @return True if a walk found a model, in which case the phases are that model.
        [[nodiscard]] bool run_opening_walk() noexcept
        {
            // The probe's phase-saved assignment is kept aside: a walk that ends far from a model has nothing
            // better to offer than what the search already learned (`ais12`: 12,004 conflicts after adopting
            // such a walk, 7,580 without), so only a near miss replaces it.
            probe_phase_scratch_.assign(saved_phase_.begin(), saved_phase_.end());
            std::copy(opening_phase_snapshot_.begin(), opening_phase_snapshot_.end(), saved_phase_.begin());
            const auto budget = static_cast<std::size_t>(local_search_variable_count_) * local_search_flips_per_variable_;
            if (walk_in_rounds(budget / 2u, true))
                return true;
            // The second rule gets its turn only after a near miss: every formula the opening walk has solved fell
            // to the first rule, and on the ones it cannot solve the second rule doubled the cost (`hanoi4`,
            // `ais12`: 50-60 ms of walking against a search that finishes in under 10 ms).
            if (best_walk_unsatisfied_ > local_search_near_miss_limit)
            {
                std::copy(probe_phase_scratch_.begin(), probe_phase_scratch_.end(), saved_phase_.begin());
                return false;
            }
            const auto first_best = best_walk_config_;
            best_walk_config_ = local_search_portfolio.size();
            const auto second_best_before = best_walk_unsatisfied_;
            const auto solved = walk_in_rounds(budget / 2u, false);
            if (!solved && best_walk_unsatisfied_ >= second_best_before)
                best_walk_config_ = first_best;
            if (!solved && best_walk_unsatisfied_ > local_search_near_miss_limit)
                std::copy(probe_phase_scratch_.begin(), probe_phase_scratch_.end(), saved_phase_.begin());
            return solved;
        }

        /// @brief Re-walks from the current phases once enough search effort has accrued since the last walk.
        /// @details The budget is a fixed share of the watch visits spent since the previous walk, so on an
        /// unsatisfiable formula the walks stay a small constant fraction of the run while on a satisfiable one
        /// they keep growing with the search until one of them lands on a model.
        void rewalk_if_due() noexcept
        {
            if (!local_search_enabled_ || local_search_effort_percent_ == 0u || opening_walk_pending_)
                return;
            const auto conflicts = search_coordinator_.conflict_event_count();
            if (conflicts < next_local_search_at_)
                return;
            const auto visits = watch_entry_scan_count_ - visits_at_last_walk_;
            visits_at_last_walk_ = watch_entry_scan_count_;
            const auto interval = next_local_search_at_ == 0u ? local_search_initial_interval : next_local_search_at_ * 2u;
            next_local_search_at_ = conflicts + interval;
            const auto minimum = std::min(static_cast<std::size_t>(local_search_variable_count_) * local_search_rewalk_floor_per_variable,
                                          local_search_rewalk_floor_cap);
            const auto share = static_cast<double>(visits) * static_cast<double>(local_search_effort_percent_) / (100.0 * visits_per_flip);
            const auto budget = std::max(minimum, static_cast<std::size_t>(share * local_search_scale_));
            const auto improved = walk_and_seed_phases(budget, false);
            local_search_scale_ = improved ? std::min(local_search_scale_ * 2.0, local_search_scale_ceiling)
                                           : std::max(local_search_scale_ * 0.5, local_search_scale_floor);
        }

        /// @brief Rebuilds every watch list and the unit list from the clause database.
        void rebuild_propagation_state() noexcept
        {
            watch_list_.reserve(static_cast<std::size_t>(variable_count_) * 2u + 2u);
            watch_list_.clear_entries();
            unit_clause_refs_.clear();
            const auto attach = [this](const clause::ref_t ref) noexcept { attach_clause_for_propagation(ref); };
            clause_database_.iterate_irredundant(attach);
            clause_database_.iterate_redundant(attach);
        }

        /// @brief Registers a clause for propagation: two watches for size >= 2, the unit list otherwise.
        void attach_clause_for_propagation(const clause::ref_t ref) noexcept
        {
            const auto& storage = clause_database_.storage_of();
            const auto literals = storage.view_literals(ref);
            if (literals.size() <= 1u)
            {
                unit_clause_refs_.push_back(ref);
                return;
            }
            const auto is_binary = literals.size() == 2u;
            watch_list_.push_watch(literals[0], watch {literals[1], ref, is_binary});
            watch_list_.push_watch(literals[1], watch {literals[0], ref, is_binary});
        }

        /// @brief Assigns every root-level unit; returns false if one is already falsified or a clause is empty.
        [[nodiscard]] bool assign_root_units() noexcept
        {
            const auto& storage = clause_database_.storage_of();
            for (const auto ref: unit_clause_refs_)
            {
                if (!storage.is_alive(ref))
                    continue;
                const auto literals = storage.view_literals(ref);
                if (literals.empty())
                    return false;
                const auto lit = literals.front();
                const auto value = value_of(lit);
                if (value < 0)
                    return false;
                if (value == 0)
                {
                    assign(lit, ref);
                    ++propagation_assignment_count_;
                }
            }
            return true;
        }

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
        void backtrack(const std::uint32_t target_level) noexcept
        {
            if (target_level >= level_)
                return;
            const auto begin = control_[target_level + 1u].trail_begin;
            for (auto index = trail_.size(); index-- > begin;)
            {
                const auto lit = trail_[index];
                const auto var = var_of(lit);
                values_[lit.raw()] = 0;
                values_[lit.raw() ^ 1u] = 0;
                if (!heap_.contains(var))
                    heap_.push(var);
            }
            trail_.resize(begin);
            if (propagated_ > begin)
                propagated_ = begin;
            control_.resize(target_level + 1u);
            level_ = target_level;
        }

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
        [[nodiscard]] clause::ref_t propagate() noexcept
        {
            (void) propagator_.propagate();
            auto& storage = clause_database_.storage_of();
            std::int8_t* const values = values_.data();
            std::size_t visits {};
            std::size_t binary_visits {};
            std::size_t assigned {};
            std::size_t partitions {};
            clause::ref_t conflict {};

            while (propagated_ < trail_.size())
            {
                const literal lit = trail_[propagated_++];
                const literal not_lit = lit.negated();
                auto& list = watch_list_.list_at(not_lit.raw());
                ++partitions;
                watch* const begin = list.data();
                watch* const end = begin + list.size();
                watch* read = begin;
                watch* write = begin;

                while (read != end)
                {
                    const watch entry = *read++;
                    ++visits;
                    const literal blocking = entry.blocking_literal();
                    const auto blocking_value = values[blocking.raw()];
                    if (blocking_value > 0)
                    {
                        *write++ = entry;
                        continue;
                    }

                    if (entry.is_binary())
                    {
                        ++binary_visits;
                        *write++ = entry;
                        if (blocking_value < 0)
                        {
                            ++binary_watch_conflict_count_;
                            conflict = entry.clause_ref();
                            break;
                        }
                        assign(blocking, entry.clause_ref());
                        ++assigned;
                        continue;
                    }

                    const clause::ref_t ref {entry.raw_offset()};
                    literal* const lits = storage.literal_data(ref);
                    const literal other {lits[0].raw() ^ lits[1].raw() ^ not_lit.raw()};
                    const auto other_value = values[other.raw()];
                    if (other_value > 0)
                    {
                        *write++ = watch {other, ref, false};
                        continue;
                    }

                    literal* const lits_end = lits + storage.header_at(ref).size;
                    literal* candidate = lits + 2;
                    while (candidate != lits_end && values[candidate->raw()] < 0)
                        ++candidate;

                    if (candidate != lits_end)
                    {
                        const literal replacement = *candidate;
                        lits[0] = other;
                        lits[1] = replacement;
                        *candidate = not_lit;
                        watch_list_.list_at(replacement.raw()).push_back(watch {other, ref, false});
                        continue;
                    }

                    *write++ = watch {other, ref, false};
                    if (other_value < 0)
                    {
                        conflict = ref;
                        break;
                    }
                    lits[0] = other;
                    lits[1] = not_lit;
                    assign(other, ref);
                    ++assigned;
                }

                while (read != end)
                    *write++ = *read++;
                list.resize(static_cast<std::size_t>(write - begin));
                if (conflict.valid())
                    break;
            }

            watch_entry_scan_count_ += visits;
            binary_watch_scan_count_ += binary_visits;
            propagation_assignment_count_ += assigned;
            watch_list_.add_iterate_count(partitions);
            return conflict;
        }

        // ------------------------------------------------------------------------------------------------------
        // Conflict analysis, minimization and learning
        // ------------------------------------------------------------------------------------------------------

        void bump_clause(const clause::ref_t ref) noexcept
        {
            auto& header = clause_database_.storage_of().header_at(ref);
            // Two counts per use: the reduction pass treats two or more as "used since the last pass" and leaves
            // one behind as a pass of grace for mid-glue clauses, which it then clears.
            header.used = static_cast<std::uint8_t>(std::min<unsigned>(255u, static_cast<unsigned>(header.used) + 2u));
            header.activity += 1.0f;
        }

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
        void collect_root_chain(const std::uint32_t var) noexcept
        {
            if (chain_stamps_[var] == chain_stamp_)
                return;
            chain_stamps_[var] = chain_stamp_;
            const auto reason = reasons_[var];
            if (!reason.valid())
                return;
            const auto& storage = clause_database_.storage_of();
            for (const auto other: storage.view_literals(reason))
                if (var_of(other) != var)
                    collect_root_chain(var_of(other));
            root_chain_refs_.push_back(reason);
        }

        /// @brief Records the clauses that justify removing a minimized literal (proof chains only).
        void collect_minimize_chain(const std::uint32_t var) noexcept
        {
            if (chain_stamps_[var] == chain_stamp_)
                return;
            chain_stamps_[var] = chain_stamp_;
            const auto reason = reasons_[var];
            const auto& storage = clause_database_.storage_of();
            for (const auto other: storage.view_literals(reason))
            {
                const auto other_var = var_of(other);
                if (other_var == var)
                    continue;
                if (levels_[other_var] == 0u)
                    collect_root_chain(other_var);
                else if ((flags_[other_var] & keep_flag) == 0u && (flags_[other_var] & removable_flag) != 0u)
                    collect_minimize_chain(other_var);
            }
            minimize_chain_refs_.push_back(reason);
        }

        /// @brief Recursive removability test of a learned-clause literal through its implication ancestry.
        /// @details A literal is redundant when every literal of its reason is itself in the clause, fixed at the
        /// root, or recursively redundant. Two early rejections from CaDiCaL prune the search: a literal that is
        /// the only one of its level in the clause, or the earliest one of its level on the trail, cannot be
        /// implied by the others. `poison` caches failures and `removable` successes for the rest of this clause.
        [[nodiscard]] bool minimize_literal(const std::uint32_t var, const std::uint32_t depth) noexcept
        {
            const auto var_flags = flags_[var];
            const auto lvl = levels_[var];
            if (lvl == 0u || (var_flags & (removable_flag | keep_flag)) != 0u)
                return true;
            const auto reason = reasons_[var];
            if (!reason.valid() || (var_flags & poison_flag) != 0u || lvl == level_)
                return false;
            const auto& frame = control_[lvl];
            if (depth == 0u && frame.seen_count < 2u)
                return false;
            if (trail_positions_[var] <= frame.seen_min_trail)
                return false;
            if (depth > minimize_depth_limit)
                return false;

            bool removable = true;
            const auto& storage = clause_database_.storage_of();
            const literal* const lits = storage.literal_data(reason);
            const literal* const lits_end = lits + storage.header_at(reason).size;
            for (const literal* current = lits; current != lits_end; ++current)
            {
                const auto other_var = var_of(*current);
                if (other_var == var)
                    continue;
                if (!minimize_literal(other_var, depth + 1u))
                {
                    removable = false;
                    break;
                }
            }
            flags_[var] |= removable ? removable_flag : poison_flag;
            minimized_.push_back(var);
            return removable;
        }

        /// @brief Runs first-UIP analysis from `conflict`, minimizes, bumps, and leaves the result in `learned_`.
        /// @details Resolution walks the trail backwards: literals of the current level are counted as `open`
        /// and their reasons resolved in trail order until one remains, the first UIP; lower-level literals are
        /// collected into the clause. Level-zero literals are dropped as they can never be falsified again.
        /// @return The glue (number of distinct decision levels) of the learned clause.
        [[nodiscard]] std::uint32_t analyze_conflict(const clause::ref_t conflict) noexcept
        {
            auto& storage = clause_database_.storage_of();
            const bool want_chain = proof_manager_.has_active_consumers();
            learned_.clear();
            learned_.push_back(literal {});
            analyzed_.clear();
            minimized_.clear();
            levels_touched_.clear();
            resolution_chain_refs_.clear();
            root_chain_refs_.clear();
            minimize_chain_refs_.clear();
            if (want_chain && ++chain_stamp_ == 0u)
            {
                std::fill(chain_stamps_.begin(), chain_stamps_.end(), 0u);
                chain_stamp_ = 1u;
            }

            std::uint32_t open {};
            auto trail_index = trail_.size();
            literal uip {};
            clause::ref_t reason = conflict;
            for (;;)
            {
                bump_clause(reason);
                if (want_chain)
                    resolution_chain_refs_.push_back(reason);
                const literal* const lits = storage.literal_data(reason);
                const literal* const lits_end = lits + storage.header_at(reason).size;
                for (const literal* current = lits; current != lits_end; ++current)
                    if (*current != uip)
                        analyze_literal(*current, open, want_chain);

                for (;;)
                {
                    const literal candidate = trail_[--trail_index];
                    const auto var = var_of(candidate);
                    if ((flags_[var] & seen_flag) != 0u && levels_[var] == level_)
                    {
                        uip = candidate;
                        break;
                    }
                }
                if (--open == 0u)
                    break;
                reason = reasons_[var_of(uip)];
            }
            learned_[0] = uip.negated();

            // Minimize: literals earlier on the trail are decided first, so that their keep/removable marks are
            // available when later, deeper literals are examined. A unit has nothing to minimize.
            if (learned_.size() > 1u)
                ++minimized_clause_count_;
            if (learned_.size() > 2u)
                std::sort(learned_.begin() + 1, learned_.end(), [this](const literal left, const literal right) noexcept
                          { return trail_positions_[var_of(left)] < trail_positions_[var_of(right)]; });
            std::size_t write {1u};
            for (std::size_t index = 1u; index < learned_.size(); ++index)
            {
                const auto lit = learned_[index];
                const auto var = var_of(lit);
                if (minimize_literal(var, 0u))
                {
                    if (want_chain)
                        collect_minimize_chain(var);
                    continue;
                }
                flags_[var] |= keep_flag;
                learned_[write++] = lit;
            }
            if (write < learned_.size())
            {
                ++shrunk_clause_count_;
                learned_.resize(write);
            }

            // The literal of highest level after the UIP goes second: it is the backjump level's watch partner.
            std::uint32_t glue {1u};
            if (learned_.size() > 1u)
            {
                std::size_t best_index {1u};
                auto best_level = levels_[var_of(learned_[1])];
                ++glue_stamp_;
                if (glue_stamp_ == 0u)
                {
                    std::fill(level_stamps_.begin(), level_stamps_.end(), 0u);
                    glue_stamp_ = 1u;
                }
                level_stamps_[level_] = glue_stamp_;
                for (std::size_t index = 1u; index < learned_.size(); ++index)
                {
                    const auto lvl = levels_[var_of(learned_[index])];
                    if (level_stamps_[lvl] != glue_stamp_)
                    {
                        level_stamps_[lvl] = glue_stamp_;
                        ++glue;
                    }
                    if (lvl > best_level)
                    {
                        best_level = lvl;
                        best_index = index;
                    }
                }
                if (best_index != 1u)
                    std::swap(learned_[1], learned_[best_index]);
            }

            // Reason-side bumping: the variables that forced the learned clause's literals are as much a part of
            // the conflict as the literals themselves; bumping them focuses the search on the structure behind
            // the clause rather than only its surface. Kissat and CaDiCaL both do this for short clauses.
            if (learned_.size() <= reason_bump_size_limit)
            {
                for (std::size_t index = 1u; index < learned_.size(); ++index)
                {
                    const auto reason_ref = reasons_[var_of(learned_[index])];
                    if (!reason_ref.valid())
                        continue;
                    const literal* const lits = storage.literal_data(reason_ref);
                    const literal* const lits_end = lits + storage.header_at(reason_ref).size;
                    for (const literal* current = lits; current != lits_end; ++current)
                    {
                        const auto var = var_of(*current);
                        if ((flags_[var] & seen_flag) != 0u || levels_[var] == 0u)
                            continue;
                        flags_[var] |= seen_flag;
                        analyzed_.push_back(var);
                    }
                }
            }

            for (const auto var: analyzed_)
                heap_.bump(var);
            heap_.decay();

            for (const auto var: analyzed_)
                flags_[var] = 0u;
            for (const auto var: minimized_)
                flags_[var] = 0u;
            for (const auto lvl: levels_touched_)
                control_[lvl].seen_count = 0u;
            return glue;
        }

        /// @brief Stores the learned clause, backjumps, asserts its first literal, and reports the proof step.
        void learn_clause(const std::uint32_t glue) noexcept
        {
            const auto backjump_level = learned_.size() > 1u ? levels_[var_of(learned_[1])] : 0u;
            backtrack(backjump_level);

            const auto ref = clause_database_.add_clause(learned_, true);
            auto& header = clause_database_.storage_of().header_at(ref);
            header.glue = glue;
            header.tier = static_cast<std::uint8_t>(glue <= core_glue_limit ? 0u : glue <= retained_glue_limit ? 1u : 2u);
            if (header.tier == 0u)
                ++promoted_clause_count_;

            if (learned_.size() == 1u)
                unit_clause_refs_.push_back(ref);
            else
                attach_clause_for_propagation(ref);
            assign(learned_[0], ref);

            if (proof_manager_.has_active_consumers())
            {
                antecedents_scratch_.clear();
                const auto append = [this](const clause::ref_t chain_ref) noexcept
                {
                    const auto id = proof_manager_.stable_id_for_clause(chain_ref);
                    if (id.valid())
                        antecedents_scratch_.push_back(id);
                };
                for (const auto chain_ref: root_chain_refs_)
                    append(chain_ref);
                for (const auto chain_ref: minimize_chain_refs_)
                    append(chain_ref);
                for (auto it = resolution_chain_refs_.rbegin(); it != resolution_chain_refs_.rend(); ++it)
                    append(*it);
                proof_manager_.on_add_derived(ref, learned_, antecedents_scratch_);
            }
            else
                proof_manager_.on_add_derived(ref, learned_);

            learned_clause_glue_total_ += glue;
            ++learned_clause_glue_sample_count_;
            search_coordinator_.note_learned_clause();
            incremental_context_.retain_learned_clause();
        }

        // ------------------------------------------------------------------------------------------------------
        // Assumptions
        // ------------------------------------------------------------------------------------------------------

        /// @brief Explains a falsified assumption by the assumptions that imply its negation.
        void analyze_final(const literal failed) noexcept
        {
            failed_core_.clear();
            failed_core_.push_back(failed);
            analyzed_.clear();
            const auto failed_var = var_of(failed);
            flags_[failed_var] |= seen_flag;
            analyzed_.push_back(failed_var);

            const auto& storage = clause_database_.storage_of();
            for (auto index = trail_.size(); index-- > 0;)
            {
                const auto lit = trail_[index];
                const auto var = var_of(lit);
                if ((flags_[var] & seen_flag) == 0u || levels_[var] == 0u)
                    continue;
                const auto reason = reasons_[var];
                if (!reason.valid())
                {
                    // A decision below the search levels is an assumption; the failed literal's own negation
                    // reaches here only when the caller assumed both polarities.
                    failed_core_.push_back(lit);
                    continue;
                }
                for (const auto other: storage.view_literals(reason))
                {
                    const auto other_var = var_of(other);
                    if ((flags_[other_var] & seen_flag) == 0u)
                    {
                        flags_[other_var] |= seen_flag;
                        analyzed_.push_back(other_var);
                    }
                }
            }
            for (const auto var: analyzed_)
                flags_[var] = 0u;
            analyzed_.clear();
        }

        // ------------------------------------------------------------------------------------------------------
        // Restarts, reduction, inprocessing
        // ------------------------------------------------------------------------------------------------------

        /// @brief Restarts, keeping the trail prefix whose decisions the heap would repeat anyway.
        void restart() noexcept
        {
            std::uint32_t reuse = assumption_count_;
            if (reuse < level_)
            {
                while (!heap_.empty() && is_assigned(heap_.top()))
                    (void) heap_.pop();
                if (!heap_.empty())
                {
                    const auto next_score = heap_.score(heap_.top());
                    while (reuse < level_)
                    {
                        const auto decision = control_[reuse + 1u].decision;
                        if (decision.raw() == 0u || heap_.score(var_of(decision)) < next_score)
                            break;
                        ++reuse;
                    }
                }
            }
            backtrack(reuse);
            search_coordinator_.note_restart();
        }

        void protect_trail_reasons(const bool protect) noexcept
        {
            for (const auto lit: trail_)
            {
                const auto reason = reasons_[var_of(lit)];
                if (!reason.valid())
                    continue;
                if (protect)
                    clause_database_.mark_reason_clause(reason);
                else
                    clause_database_.unmark_reason_clause(reason);
            }
        }

        /// @brief Reduces the learned-clause database and compacts the arena.
        void reduce() noexcept
        {
            protect_trail_reasons(true);
            auto& controller = search_coordinator_.reduce_controller();
            controller.select_reduction_candidates(clause_database_);
            controller.reduce_clauses(clause_database_);
            controller.flush_redundant(clause_database_);
            controller.update_tiers(clause_database_);
            clause_database_.decay_quality();
            protect_trail_reasons(false);
            collect_garbage();
        }

        /// @brief Slides live clauses down the arena and re-targets every watch, reason and unit reference.
        void collect_garbage() noexcept
        {
            const auto slots = clause_database_.storage_of().arena_bytes() / sizeof(std::uint32_t) + 1u;
            forwarding_.assign(slots, clause::ref_t::invalid_offset);
            clause_database_.compact([this](const clause::ref_t old_ref, const clause::ref_t new_ref) noexcept
                                     { forwarding_[old_ref.offset() / sizeof(std::uint32_t)] = new_ref.offset(); });
            const auto forward = [this](const clause::ref_t ref) noexcept
            {
                return clause::ref_t {forwarding_[ref.offset() / sizeof(std::uint32_t)]};
            };
            watch_list_.rewrite_refs(forward);
            for (const auto lit: trail_)
            {
                auto& reason = reasons_[var_of(lit)];
                if (reason.valid())
                    reason = forward(reason);
            }
            std::size_t write {};
            for (const auto ref: unit_clause_refs_)
            {
                const auto moved = forward(ref);
                if (moved.valid())
                    unit_clause_refs_[write++] = moved;
            }
            unit_clause_refs_.resize(write);
            forwarding_.clear();
        }

        /// @brief Runs an inprocessing epoch at the root when the scheduler says one is due.
        /// @return `unsatisfiable` if the simplified formula is refuted at the root, `unknown` otherwise.
        [[nodiscard]] status run_inprocess_if_due() noexcept
        {
            inprocess_scheduler_.set_conflicts_seen(search_coordinator_.conflict_event_count());
            inprocess_scheduler_.set_restart_count(search_coordinator_.restart_count());
            inprocess_scheduler_.set_decisions_seen(search_coordinator_.decision_event_count());
            inprocess_scheduler_.set_reduction_passes_seen(search_coordinator_.reduction_pass_count());
            inprocess_scheduler_.set_learned_clauses_seen(static_cast<counter_t>(search_coordinator_.learned_clause_count()));
            if (!inprocess_scheduler_.should_run())
                return status::unknown;

            backtrack(0u);
            protect_trail_reasons(true);
            const auto epochs_before = inprocess_scheduler_.epoch_count();
            inprocess_scheduler_.run_epoch();
            protect_trail_reasons(false);
            if (inprocess_scheduler_.epoch_count() == epochs_before)
                return status::unknown;

            inprocess_scheduler_.report_epoch_summary();
            search_coordinator_.notify_inprocess_epoch_completed(inprocess_scheduler_.last_structural_gain());
            rebuild_propagation_state();
            propagated_ = 0u;
            if (!assign_root_units())
                return root_conflict() == status::unsatisfiable ? status::unsatisfiable : status::unknown;
            return status::unknown;
        }

        // ------------------------------------------------------------------------------------------------------
        // The search loop
        // ------------------------------------------------------------------------------------------------------

        /// @brief Per-conflict bookkeeping shared by search conflicts and root conflicts.
        void note_conflict(const std::uint32_t glue) noexcept
        {
            search_coordinator_.note_conflict(glue);
            if (evsids_maintenance_interval_ != 0u && search_coordinator_.conflict_event_count() % evsids_maintenance_interval_ == 0u)
                heap_.rescale_by(0.5);
        }

        /// @brief Accounts for a root-level conflict: the formula is refuted, unless the conflict budget ran out on
        /// this very conflict, in which case the episode reports the limit like any other conflict would.
        [[nodiscard]] status root_conflict() noexcept
        {
            note_conflict(0u);
            if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
                return status::unknown;
            return status::unsatisfiable;
        }

        /// @brief Runs the CDCL search to a terminal outcome for one episode.
        /// @details Iterative by construction: a conflict is resolved by analysis, learning and a backjump applied
        /// in place, so the decision level is data rather than C++ stack depth. Assumptions occupy the first
        /// decision levels; a falsified assumption ends the episode with its core, a root-level conflict with the
        /// empty core.
        [[nodiscard]] status run_search(const solve_request& request) noexcept
        {
            if (!assign_root_units())
                return root_conflict();

            for (;;)
            {
                const auto conflict = propagate();
                if (conflict.valid())
                {
                    if (level_ == 0u)
                        return root_conflict();
                    const auto glue = analyze_conflict(conflict);
                    learn_clause(glue);
                    note_conflict(glue);
                    if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
                        return status::unknown;
                    continue;
                }

                if (level_ < assumption_count_)
                {
                    const auto assumption = request.assumptions[level_];
                    const auto value = value_of(assumption);
                    if (value < 0)
                    {
                        // A falsified assumption is the episode's conflict: it is counted like one, so limits and
                        // statistics treat an assumption-refuted episode the same way as a root-refuted one.
                        analyze_final(assumption);
                        return root_conflict();
                    }
                    if (value > 0)
                    {
                        new_level(literal {});
                        continue;
                    }
                    new_level(assumption);
                    assign(assumption, clause::ref_t {});
                    continue;
                }

                if (opening_walk_pending_ && search_coordinator_.conflict_event_count() >= opening_walk_due_at_)
                {
                    // The probe is over: a full restart, not the reusing kind, because the trail prefix a restart
                    // keeps follows the phases the walk is about to replace.
                    opening_walk_pending_ = false;
                    backtrack(assumption_count_);
                    search_coordinator_.note_restart();
                    (void) run_opening_walk();
                    continue;
                }

                if (search_coordinator_.conflict_event_count() >= mode_switch_at_)
                {
                    // Stable/focused alternation, as CaDiCaL and Kissat do it: a satisfiable structured formula
                    // (`hanoi5`: 30k conflicts at a 50-conflict interval, 109k at 4,096) wants frequent restarts
                    // while random 3-SAT wants rare ones, and neither can be told apart in advance.
                    focused_mode_ = !focused_mode_;
                    if (focused_mode_)
                        mode_period_ *= 2u;
                    // Stable periods are four times the focused ones: random 3-SAT wants the stable schedule for
                    // most of the run, and the structured formulas need only a few focused periods.
                    mode_switch_at_ = search_coordinator_.conflict_event_count() + (focused_mode_ ? mode_period_ : mode_period_ * stable_period_factor);
                    search_coordinator_.set_restart_interval(focused_mode_ ? focused_restart_interval : stable_restart_interval_);
                }

                if (search_coordinator_.should_restart())
                {
                    if (search_coordinator_.has_progress_since_restart())
                    {
                        restart();
                        rewalk_if_due();
                        if (run_inprocess_if_due() == status::unsatisfiable)
                            return status::unsatisfiable;
                    }
                    else
                        search_coordinator_.note_restart();
                    continue;
                }

                if (search_coordinator_.should_reduce())
                    reduce();

                std::uint32_t decision_var {};
                while (!heap_.empty())
                {
                    const auto candidate = heap_.pop();
                    if (!is_assigned(candidate))
                    {
                        decision_var = candidate;
                        break;
                    }
                }
                if (decision_var == 0u)
                    return status::satisfiable;

                const literal decision {variable {decision_var}, saved_phase_[decision_var] < 0};
                new_level(decision);
                assign(decision, clause::ref_t {});
                search_coordinator_.note_decision();
                if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
                    return status::unknown;
            }
        }

        // ------------------------------------------------------------------------------------------------------
        // Results
        // ------------------------------------------------------------------------------------------------------

        void build_internal_model() noexcept
        {
            internal_model_.clear();
            internal_model_.reserve(variable_count_);
            for (std::uint32_t index = 1u; index <= variable_count_; ++index)
                internal_model_.push_back(literal {variable {index}, values_[static_cast<std::size_t>(index) << 1u] < 0});

            // The search only ever sees the formula that preprocessing left behind, so any variable a pass
            // eliminated still has to have its value re-derived from the clauses that were removed. With an empty
            // extension stack this is an identity pass.
            if (extension_stack_.size() != 0u)
            {
                model_reconstructor_.set_initial_model(internal_model_);
                const auto reconstructed = model_reconstructor_.reconstruct_full_model();
                internal_model_.assign(reconstructed.values().begin(), reconstructed.values().end());
            }
        }

        void synchronize_search_outcome(const status solve_status) noexcept
        {
            switch (solve_status)
            {
                case status::satisfiable:
                    search_coordinator_.handle_sat();
                    break;
                case status::unsatisfiable:
                    search_coordinator_.handle_unsat();
                    break;
                case status::unknown:
                default:
                    // Only stamp an external cause if the coordinator has not already recorded a specific one
                    // (decision_limit/conflict_limit); otherwise this would clobber that more precise cause.
                    if (search_coordinator_.current_termination_cause() == search_coordinator::termination_cause::none)
                        search_coordinator_.handle_termination();
                    break;
            }
        }

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
        std::vector<literal> trail_ {};
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
