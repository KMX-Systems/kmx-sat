/// @file inc/kmx/sat/cdcl/solver_core.hpp
/// @brief The main internal solver container, but without degenerating back into an opaque monolith.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstdint>
    #include <initializer_list>
    #include <optional>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/clause/minimizer.hpp>
#include <kmx/sat/cdcl/incremental_context.hpp>
#include <kmx/sat/cdcl/memory_governor.hpp>
#include <kmx/sat/cdcl/propagator.hpp>
#include <kmx/sat/cdcl/search_coordinator.hpp>
#include <kmx/sat/cdcl/store/clause_cold.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/flush_restore_manager.hpp>
#include <kmx/sat/simplify/forward_subsumer.hpp>
#include <kmx/sat/simplify/scheduler/inprocess.hpp>
#include <kmx/sat/simplify/scheduler/preprocess.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief The main internal solver container, but without degenerating back into an opaque monolith.
    ///
    /// @details
    /// `solver_core` plays the same coordinating role as CaDiCaL's `Internal` struct, but as a composition root over
    /// separately testable objects (`clause::database`, `search_coordinator`, and transitively every CDCL component)
    /// rather than one large struct holding all state and behavior as fields and methods. `solve`/
    /// `solve_under_assumptions` are the entry points `external_frontend` calls into for one episode;
    /// `add_problem_clause` registers an original clause before or between episodes; `current_status` exposes the
    /// last terminal outcome; `extract_internal_model`/`extract_failed_core` hand raw internal-variable results to
    /// `model_reconstructor`/`failed_core_extractor` for translation back to external variables.
    /// @note The aggregate memory layout and call overhead of this composition-root design must remain within
    /// measurement noise of an equivalent flat-struct baseline, as validated by Phase 5 comparative benchmarking
    /// against pinned CaDiCaL/Kissat releases.
    class solver_core final
    {
    public:
        struct inprocess_telemetry_snapshot final
        {
            double conflict_density_ema {0.0};
            double structural_gain_ema {0.0};
            double restart_pressure_ema {0.0};
            double reduction_pressure_ema {0.0};
            double learned_clause_pressure_ema {0.0};
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
        solver_core() noexcept { rebind_internal_views(); }

        /// @brief Resets the core to an empty, freshly bound state without invalidating internal helper pointers.
        /// @throws None (noexcept).
        void reset() noexcept
        {
            clause_database_ = {};
            search_coordinator_ = {};
            propagator_ = {};
            proof_manager_ = {};
            incremental_context_ = {};
            memory_governor_ = {};
            watch_list_ = {};
            variable_mapper_ = {};
            clause_cold_ = {};
            flush_restore_manager_ = {};
            forward_subsumer_ = {};
            clause_minimizer_ = {};
            preprocess_scheduler_ = {};
            inprocess_scheduler_ = {};
            unit_clause_refs_.clear();
            original_clause_count_ = 0u;
            internal_model_.clear();
            failed_core_.clear();
            last_conflict_clause_.clear();
            propagation_queue_scratch_.clear();
            watch_snapshot_scratch_.clear();
            propagation_assignment_count_ = 0u;
            watch_entry_scan_count_ = 0u;
            binary_watch_scan_count_ = 0u;
            binary_watch_conflict_count_ = 0u;
            learned_clause_shrink_event_count_ = 0u;
                learned_clause_glue_total_ = 0u;
                learned_clause_glue_sample_count_ = 0u;
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
            last_conflict_clause_.clear();
            propagation_assignment_count_ = 0u;
            watch_entry_scan_count_ = 0u;
            binary_watch_scan_count_ = 0u;
            binary_watch_conflict_count_ = 0u;
            learned_clause_shrink_event_count_ = 0u;
            learned_clause_glue_total_ = 0u;
            learned_clause_glue_sample_count_ = 0u;
            clause_cold_.reset();

            incremental_context_.begin_solve_epoch();
            memory_governor_.reset_epoch_usage();
            clause_database_.clear_reason_clauses();
            forward_subsumer_.run();
            preprocess_scheduler_.clear_abort();
            preprocess_scheduler_.set_inprocess_telemetry_snapshot(
                inprocess_scheduler_.conflict_density_ema(), inprocess_scheduler_.structural_gain_ema(),
                inprocess_scheduler_.restart_pressure_ema(), inprocess_scheduler_.reduction_pressure_ema(),
                inprocess_scheduler_.learned_clause_pressure_ema());
            preprocess_scheduler_.run_initial_pipeline();
            preprocess_scheduler_.report_pass_summary();
            inprocess_scheduler_.clear_abort();

            search_coordinator_.apply_assumptions(request);

            const auto finalize_epoch = [this](const status result) noexcept
            {
                status_ = result;
                incremental_context_.end_solve_epoch();
                incremental_context_.reset_transient_state();
                search_coordinator_.clear_variable_selectability_filter();
                synchronize_search_outcome(status_);
                return status_;
            };

            const auto max_variable = find_max_variable(request.assumptions);
            assignment_vector assignment(static_cast<std::size_t>(max_variable + 1u), unassigned_value);
            decision_level_vector decision_levels(static_cast<std::size_t>(max_variable + 1u), 0u);
            reason_vector reasons(static_cast<std::size_t>(max_variable + 1u), clause::ref_t {});
            trail_vector trail {};
            std::uint64_t conflicts = 0;

            for (std::uint32_t index = 1u; index <= max_variable; ++index)
            {
                search_coordinator_.activate_variable(variable {index});
            }

            for (const auto assumption: request.assumptions)
            {
                if (!assign_literal(assignment, decision_levels, reasons, assumption, 0u))
                {
                    failed_core_.clear();
                    failed_core_.push_back(assumption);
                    for (const auto prior_assumption: request.assumptions)
                    {
                        if (prior_assumption.variable_of().index() != assumption.variable_of().index())
                        {
                            continue;
                        }
                        if (prior_assumption.raw() == assumption.raw())
                        {
                            continue;
                        }
                        failed_core_.push_back(prior_assumption);
                        break;
                    }
                    if (failed_core_.size() < 2u)
                    {
                        failed_core_ = request.assumptions;
                    }
                    return finalize_epoch(status::unsatisfiable);
                }

                search_coordinator_.notify_assignment_literal(assumption);
                trail.push_back(assumption);
            }

            propagator_.set_pending_assumption_count(request.assumptions.size());
            const auto assumption_conflict = propagator_.propagate_assumptions();
            if (assumption_conflict.valid())
            {
                const auto assumption_clause = clause_database_.storage_of().literals_of(assumption_conflict);
                const auto assumption_status = handle_clause_conflict(assumption_conflict, assumption_clause, decision_levels,
                                                                      reasons, request, conflicts, trail, 0u);
                if (assumption_status != status::satisfiable)
                {
                    failed_core_ = build_failed_core_from_reasons(request.assumptions, reasons);
                    if (failed_core_.empty())
                    {
                        failed_core_ = build_failed_core_from_conflict(request.assumptions);
                    }
                    if (failed_core_.empty() && !request.assumptions.empty())
                    {
                        failed_core_ = request.assumptions;
                    }
                    return finalize_epoch(assumption_status);
                }
            }

            status_ = solve_recursive(assignment, decision_levels, reasons, request, conflicts, 0u, trail, request.assumptions);

            if (status_ == status::satisfiable)
            {
                build_internal_model(assignment);
            }
            else if (status_ == status::unsatisfiable)
            {
                failed_core_ = build_failed_core_from_reasons(request.assumptions, reasons);
                if (failed_core_.empty())
                {
                    failed_core_ = build_failed_core_from_conflict(request.assumptions);
                }
                if (failed_core_.empty() && !request.assumptions.empty())
                {
                    failed_core_ = request.assumptions;
                }
            }

            run_inprocess_if_due();

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

        /// @brief Registers an original (non-redundant) problem clause with the internal clause database.
        /// @param literals Internal literals composing the clause.
        /// @throws None (noexcept).
        void add_problem_clause(const std::span<const literal> literals) noexcept
        {
            const auto ref = clause_database_.add_clause(literals, false);
            attach_clause_for_propagation(ref);
            proof_manager_.on_add_original(ref, literals);
            ++original_clause_count_;
        }

        /// @brief Convenience overload for adding a clause from a braced literal list.
        /// @param literals Internal literals composing the clause.
        /// @throws None (noexcept).
        void add_problem_clause(const std::initializer_list<literal> literals) noexcept
        {
            add_problem_clause(std::span<const literal> {literals.begin(), literals.size()});
        }

        /// @brief Returns the terminal status of the most recently completed solve episode.
        /// @return Current internal status value.
        /// @throws None (noexcept).
        status current_status() const noexcept { return status_; }

        /// @brief Returns how many original problem clauses have been registered.
        /// @return Number of clauses added via `add_problem_clause`.
        /// @throws None (noexcept).
        std::size_t original_clause_count() const noexcept { return original_clause_count_; }

        /// @brief Returns how many learned clauses have been registered in the clause database.
        /// @return Number of redundant clauses currently owned by the database.
        /// @throws None (noexcept).
        std::size_t learned_clause_count() const noexcept { return clause_database_.stats_snapshot().redundant_count; }

        /// @brief Returns how many learned clauses have been processed by the clause minimizer.
        std::uint32_t minimized_learned_clause_count() const noexcept { return clause_minimizer_.minimized_clause_count(); }

        /// @brief Returns how many learned clauses have been shrunk in storage by the clause minimizer.
        std::uint32_t shrunk_learned_clause_count() const noexcept { return clause_minimizer_.shrunk_clause_count(); }

        /// @brief Returns how many learned clauses were promoted after glue recomputation.
        std::uint32_t promoted_learned_clause_count() const noexcept { return clause_minimizer_.promoted_clause_count(); }

        /// @brief Extracts the internal-variable model after a satisfiable episode.
        /// @return Read-only span of internal model literals, to be translated by `model_reconstructor`.
        /// @throws None (noexcept).
        std::span<const literal> extract_internal_model() const noexcept { return internal_model_; }

        /// @brief Extracts the internal-variable failed core after an unsatisfiable episode under assumptions.
        /// @return Read-only span of internal failed-assumption literals, to be translated by `failed_core_extractor`.
        /// @throws None (noexcept).
        std::span<const literal> extract_failed_core() const noexcept { return failed_core_; }

        std::size_t proof_buffered_event_count() const noexcept { return proof_manager_.buffered_event_count(); }

        const proof::proof_event& last_proof_event() const noexcept { return proof_manager_.last_event(); }

        std::span<const proof::proof_event> buffered_proof_events() const noexcept { return proof_manager_.buffered_events(); }

        /// @brief Emits the final proof conclusion for a completed solve episode.
        /// @throws None (noexcept).
        void finalize_proof() noexcept
        {
            if (proof_enabled())
            {
                proof_manager_.on_conclusion();
            }
        }

        /// @brief Returns the latest outcome produced by the coordinator-backed search episode.
        /// @return Coordinator outcome for the most recent solve episode.
        /// @throws None (noexcept).
        search_coordinator::outcome current_search_outcome() const noexcept { return search_coordinator_.current_outcome(); }

        /// @brief Returns why the latest coordinator-backed episode terminated.
        /// @return Coordinator termination cause for the most recent solve episode.
        /// @throws None (noexcept).
        search_coordinator::termination_cause current_search_termination_cause() const noexcept
        {
            return search_coordinator_.current_termination_cause();
        }

        /// @brief Returns how many learned clauses were retained across completed epochs.
        /// @return Retained learned-clause count tracked by the incremental context.
        std::uint32_t retained_learned_clause_count() const noexcept { return incremental_context_.retained_learned_clauses(); }

        /// @brief Returns whether the latest solve call completed a transient-state reset.
        /// @return True if transient per-epoch state has been reset.
        bool transient_state_was_reset() const noexcept { return incremental_context_.transient_state_reset(); }

        /// @brief Returns whether a persistent option subset has been recorded.
        /// @return True if persistent option state was marked.
        bool persisted_option_subset() const noexcept { return incremental_context_.persisted_option_subset(); }

        /// @brief Marks the currently configured options as persistent across solve epochs.
        /// @throws None (noexcept).
        void persist_option_subset() noexcept { incremental_context_.persist_option_subset(); }

        /// @brief Returns how many clauses the subsumption pass removed.
        /// @return Subsumed clause count accumulated by the attached forward subsumer.
        std::size_t subsumed_clause_count() const noexcept { return forward_subsumer_.subsumed_count(); }

        /// @brief Returns how many preprocess pipeline runs have been executed.
        /// @return Number of preprocess runs.
        std::size_t preprocess_run_count() const noexcept { return preprocess_scheduler_.pipeline_run_count(); }

        /// @brief Returns how many inprocess epochs have been executed.
        /// @return Number of inprocess epochs.
        std::uint64_t inprocess_epoch_count() const noexcept { return inprocess_scheduler_.epoch_count(); }

        /// @brief Returns conflicts handled during the latest solve episode.
        std::uint64_t conflict_event_count() const noexcept { return search_coordinator_.conflict_event_count(); }

        /// @brief Returns decisions produced during the latest solve episode.
        std::uint64_t decision_event_count() const noexcept { return search_coordinator_.decision_event_count(); }

        /// @brief Returns restarts performed during the latest solve episode.
        std::uint64_t restart_count() const noexcept { return search_coordinator_.restart_count(); }

        /// @brief Returns learned clauses registered during the latest solve episode.
        std::size_t episode_learned_clause_count() const noexcept { return search_coordinator_.learned_clause_count(); }

        /// @brief Returns how many main propagation calls were observed by the core-owned propagator.
        std::size_t propagator_call_count() const noexcept { return propagator_.propagation_call_count(); }

        /// @brief Returns the number of assignments enqueued by watched-literal propagation in the current core state.
        std::size_t propagation_assignment_count() const noexcept { return propagation_assignment_count_; }

        /// @brief Returns the number of watch entries examined by propagation in the current core state.
        std::size_t watch_entry_scan_count() const noexcept { return watch_entry_scan_count_; }

        /// @brief Returns how many watch-list partitions propagation queried in the current episode.
        std::size_t watch_partition_iterate_count() const noexcept { return watch_list_.iterate_call_count(); }

        /// @brief Returns how many binary watch entries were processed in the current core state.
        std::size_t binary_watch_scan_count() const noexcept { return binary_watch_scan_count_; }

        /// @brief Returns how many binary watch entries produced conflicts in the current core state.
        std::size_t binary_watch_conflict_count() const noexcept { return binary_watch_conflict_count_; }

        /// @brief Returns how many finalized learned clauses emitted proof shrink events in the current episode.
        std::size_t learned_clause_shrink_event_count() const noexcept { return learned_clause_shrink_event_count_; }

        std::uint64_t learned_clause_glue_total() const noexcept { return learned_clause_glue_total_; }

        std::uint64_t learned_clause_glue_sample_count() const noexcept { return learned_clause_glue_sample_count_; }

        std::uint64_t reduction_pass_count() const noexcept { return search_coordinator_.reduction_pass_count(); }

        std::uint64_t reduced_clause_count() const noexcept { return search_coordinator_.reduced_clause_count(); }

        std::uint64_t deleted_clause_count() const noexcept { return search_coordinator_.deleted_clause_count(); }

        /// @brief Returns how many assumption propagation calls were observed by the core-owned propagator.
        std::size_t propagator_assumption_call_count() const noexcept { return propagator_.assumption_propagation_call_count(); }

        /// @brief Exposes the flush/restore policy manager for focused integration tests.
        /// @return Reference to the clause flush/restore manager.
        simplify::flush_restore_manager& flush_restore_manager() noexcept { return flush_restore_manager_; }

        /// @brief Attaches an external proof tracer to the core-owned proof manager.
        /// @param sink External proof tracer sink.
        void attach_proof_tracer(const proof::tracer::view& sink) noexcept { proof_manager_.register_tracer(sink); }

        /// @brief Returns whether any proof format is active through the core-owned proof manager.
        bool proof_enabled() const noexcept { return proof_manager_.has_enabled_formats(); }

        bool proof_checkers_valid() const noexcept { return proof_manager_.validate_checkers(); }

        /// @brief Configures decision-heuristic maintenance intervals.
        /// @param conflict_maintenance_interval EVSIDS rescale interval in conflicts (0 disables).
        /// @param chb_decay_interval CHB decay interval in conflicts (0 disables).
        /// @param restart_decay_interval CHB decay interval in restarts (0 disables).
        void set_decision_maintenance_intervals(const std::uint32_t conflict_maintenance_interval,
                                                const std::uint32_t chb_decay_interval,
                                                const std::uint32_t restart_decay_interval) noexcept
        {
            search_coordinator_.set_decision_maintenance_intervals(conflict_maintenance_interval,
                                                                    chb_decay_interval,
                                                                    restart_decay_interval);
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
        std::uint32_t glue_restart_threshold_percent() const noexcept
        {
            return search_coordinator_.glue_restart_threshold_percent();
        }

        /// @brief Enables or disables research-track cold clause storage.
        void set_cold_storage_enabled(const bool enabled) noexcept { clause_cold_.set_enabled(enabled); }

        /// @brief Returns whether research-track cold clause storage is enabled.
        bool cold_storage_enabled() const noexcept { return clause_cold_.enabled(); }

        /// @brief Returns the current cold storage footprint in bytes.
        std::size_t cold_footprint_bytes() const noexcept { return clause_cold_.cold_footprint_bytes(); }

        /// @brief Returns the number of EVSIDS rescale maintenance steps observed in decision heuristics.
        std::uint32_t decision_evsids_rescale_count() const noexcept
        {
            return search_coordinator_.decision_evsids_rescale_count();
        }

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
                inprocess_scheduler_.conflict_density_ema(),
                inprocess_scheduler_.structural_gain_ema(),
                inprocess_scheduler_.restart_pressure_ema(),
                inprocess_scheduler_.reduction_pressure_ema(),
                inprocess_scheduler_.learned_clause_pressure_ema(),
            };
        }

        const simplify::preprocessing_profile_selector::pass_plan& preprocess_current_pass_plan() const noexcept
        {
            return preprocess_scheduler_.profile_selector().current_pass_plan();
        }

    private:
        void rebind_internal_views() noexcept
        {
            search_coordinator_.attach_database(clause_database_);
            flush_restore_manager_.attach_database(clause_database_);
            forward_subsumer_.attach_database(clause_database_);
            forward_subsumer_.attach_proof_manager(proof_manager_);
            clause_minimizer_.attach_storage(clause_database_.storage_of());
            clause_minimizer_.attach_database(clause_database_);
            preprocess_scheduler_.attach_memory_governor(memory_governor_);
            preprocess_scheduler_.attach_clause_database(clause_database_);
            preprocess_scheduler_.attach_watch_list(watch_list_);
            preprocess_scheduler_.attach_variable_mapper(variable_mapper_);
            preprocess_scheduler_.attach_proof_manager(proof_manager_);
            preprocess_scheduler_.attach_clause_sink([this](const clause::ref_t ref) noexcept { attach_clause_for_propagation(ref); });
            inprocess_scheduler_.attach_memory_governor(memory_governor_);
            inprocess_scheduler_.attach_clause_database(clause_database_);
            inprocess_scheduler_.attach_watch_list(watch_list_);
            inprocess_scheduler_.attach_variable_mapper(variable_mapper_);
            inprocess_scheduler_.attach_proof_manager(proof_manager_);
        }
        store::clause_cold clause_cold_ {};

        using assignment_vector = std::vector<std::int8_t>;
        using decision_level_vector = std::vector<std::uint32_t>;
        using reason_vector = std::vector<clause::ref_t>;
        using trail_vector = std::vector<literal>;

        /// @brief Non-owning context bundle passed to the conflict-analysis lookup callbacks below.
        struct conflict_resolution_context final
        {
            const clause::database* database;
            const decision_level_vector* levels;
            const reason_vector* reasons;
            mutable std::vector<literal> reason_scratch {};
        };

        /// @brief Callback bound to `conflict_resolution_context`, returning a variable's current decision level.
        static std::uint32_t conflict_level_lookup(const void* context, const variable var) noexcept
        {
            const auto* ctx = static_cast<const conflict_resolution_context*>(context);
            if (ctx == nullptr)
            {
                return 0u;
            }
            const auto index = static_cast<std::size_t>(var.index());
            return index < ctx->levels->size() ? (*ctx->levels)[index] : 0u;
        }

        /// @brief Callback bound to `conflict_resolution_context`, returning a variable's reason-clause literals.
        static std::span<const literal> conflict_reason_lookup(const void* context, const variable var) noexcept
        {
            const auto* ctx = static_cast<const conflict_resolution_context*>(context);
            if (ctx == nullptr)
            {
                return {};
            }
            const auto index = static_cast<std::size_t>(var.index());
            if (index >= ctx->reasons->size())
            {
                return {};
            }
            const auto ref = (*ctx->reasons)[index];
            if (!ref.valid())
            {
                return {};
            }
            const auto reason_literals = ctx->database->storage_of().literals_of(ref);
            ctx->reason_scratch.assign(reason_literals.begin(), reason_literals.end());
            return ctx->reason_scratch;
        }

        static constexpr std::int8_t unassigned_value = -1;
        static constexpr std::int8_t false_value = 0;
        static constexpr std::int8_t true_value = 1;

        /// @brief Registers a clause for real propagation: two-watched-literal indexing for size >= 2, a direct
        /// unit-clause bypass list for size <= 1 (unit clauses are checked every fixpoint pass instead of being
        /// watched, since a solitary literal has no second slot to watch).
        /// @param ref Reference to the clause to attach.
        /// @throws None (noexcept).
        void attach_clause_for_propagation(const clause::ref_t ref) noexcept
        {
            if (!ref.valid())
            {
                return;
            }

            const auto literals = clause_database_.storage_of().literals_of(ref);
            if (literals.size() <= 1u)
            {
                unit_clause_refs_.push_back(ref);
                return;
            }

            const auto is_binary = literals.size() == 2u;
            watch watch_on_first {literals[1], ref, is_binary};
            watch watch_on_second {literals[0], ref, is_binary};
            if (is_binary)
            {
                watch_on_first.set_binary_literal(literals[1]);
                watch_on_second.set_binary_literal(literals[0]);
            }
            watch_list_.watch_literal(literals[0], watch_on_first);
            watch_list_.watch_literal(literals[1], watch_on_second);
        }

        std::vector<clause::ref_t> active_clause_refs() const noexcept
        {
            const auto stats = clause_database_.stats_snapshot();
            std::vector<clause::ref_t> refs {};
            refs.reserve(stats.irredundant_count + stats.redundant_count);

            clause_database_.iterate_irredundant([&](const clause::ref_t ref) noexcept { refs.push_back(ref); });
            clause_database_.iterate_redundant([&](const clause::ref_t ref) noexcept { refs.push_back(ref); });

            return refs;
        }

        static bool contains_clause_id(const std::vector<proof::clause::id>& ids, const proof::clause::id id) noexcept
        {
            for (const auto existing: ids)
            {
                if (existing.equals(id))
                {
                    return true;
                }
            }
            return false;
        }

        static bool contains_clause_ref(const std::vector<clause::ref_t>& refs, const clause::ref_t ref) noexcept
        {
            for (const auto existing: refs)
            {
                if (existing.offset() == ref.offset())
                {
                    return true;
                }
            }
            return false;
        }

        static bool is_tautological_clause(const std::span<const literal> literals) noexcept
        {
            for (std::size_t index = 0u; index < literals.size(); ++index)
            {
                const auto lhs = literals[index];
                for (std::size_t inner = index + 1u; inner < literals.size(); ++inner)
                {
                    const auto rhs = literals[inner];
                    if (lhs.variable_of().index() != rhs.variable_of().index())
                    {
                        continue;
                    }
                    if (lhs.is_negated() != rhs.is_negated())
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        std::vector<clause::ref_t> build_ordered_reason_chain_refs(const std::span<const literal> chain_literals,
                                                                   const reason_vector& reasons) const noexcept
        {
            std::vector<clause::ref_t> ordered_reason_refs {};

            for (const auto lit: chain_literals)
            {
                const auto variable_index = static_cast<std::size_t>(lit.variable_of().index());
                if (variable_index >= reasons.size())
                {
                    continue;
                }

                const auto reason_ref = reasons[variable_index];
                if (!reason_ref.valid() || contains_clause_ref(ordered_reason_refs, reason_ref))
                {
                    continue;
                }

                ordered_reason_refs.push_back(reason_ref);
            }

            return ordered_reason_refs;
        }

        std::vector<proof::clause::id> build_conflict_antecedents(const clause::ref_t conflict_ref,
                                                                  const std::span<const clause::ref_t> ordered_reason_refs) const noexcept
        {
            std::vector<proof::clause::id> antecedents {};

            const auto conflict_id = proof_manager_.stable_id_for_clause(conflict_ref);
            if (conflict_id.valid())
            {
                antecedents.push_back(conflict_id);
            }

            for (const auto reason_ref: ordered_reason_refs)
            {
                const auto reason_id = proof_manager_.stable_id_for_clause(reason_ref);
                if (!reason_id.valid() || contains_clause_id(antecedents, reason_id))
                {
                    continue;
                }
                antecedents.push_back(reason_id);
            }

            return antecedents;
        }

        std::uint32_t find_max_variable(const std::span<const literal> assumptions) const noexcept
        {
            std::uint32_t max_variable = 0;

            for (const auto ref: active_clause_refs())
            {
                for (const auto lit: clause_database_.storage_of().literals_of(ref))
                {
                    if (lit.variable_of().index() > max_variable)
                    {
                        max_variable = lit.variable_of().index();
                    }
                }
            }

            for (const auto lit: assumptions)
            {
                if (lit.variable_of().index() > max_variable)
                {
                    max_variable = lit.variable_of().index();
                }
            }

            return max_variable;
        }

        static bool assign_literal(assignment_vector& assignment, decision_level_vector& decision_levels, reason_vector& reasons,
                                   const literal lit, const std::uint32_t decision_level, const clause::ref_t reason_ref = {}) noexcept
        {
            const auto index = lit.variable_of().index();
            if (index >= assignment.size() || index >= reasons.size())
            {
                return false;
            }

            const std::int8_t required_value = lit.is_negated() ? false_value : true_value;
            const auto current_value = assignment[index];
            if (current_value == unassigned_value)
            {
                assignment[index] = required_value;
                decision_levels[index] = decision_level;
                reasons[index] = reason_ref;
                return true;
            }
            return current_value == required_value;
        }

        static bool literal_is_satisfied(const literal lit, const std::int8_t variable_value) noexcept
        {
            if (variable_value == unassigned_value)
            {
                return false;
            }
            if (lit.is_negated())
            {
                return variable_value == false_value;
            }
            return variable_value == true_value;
        }

        static bool conflict_limit_reached(const solve_request& request, const std::uint64_t conflicts) noexcept
        {
            return request.conflict_limit != 0 && conflicts >= request.conflict_limit;
        }

        static bool decision_limit_reached(const solve_request& request, const std::uint64_t decisions) noexcept
        {
            return request.decision_limit != 0 && decisions >= request.decision_limit;
        }

        void record_last_conflict_clause(const std::span<const literal> conflict_clause) noexcept
        {
            last_conflict_clause_.assign(conflict_clause.begin(), conflict_clause.end());
        }

        std::vector<literal> build_failed_core_from_conflict(const std::span<const literal> assumptions) const noexcept
        {
            std::vector<literal> core {};
            if (assumptions.empty() || last_conflict_clause_.empty())
            {
                return core;
            }

            core.reserve(assumptions.size());
            for (const auto assumption: assumptions)
            {
                const auto negated_assumption = assumption.negated();
                const auto conflict_it = std::find_if(last_conflict_clause_.begin(), last_conflict_clause_.end(),
                                                      [negated_assumption](const literal lit) noexcept
                                                      {
                                                          return lit.raw() == negated_assumption.raw();
                                                      });
                if (conflict_it != last_conflict_clause_.end())
                {
                    core.push_back(assumption);
                }
            }

            return core;
        }

        std::vector<literal> build_failed_core_from_reasons(const std::span<const literal> assumptions,
                                                            const reason_vector& reasons) const noexcept
        {
            std::vector<literal> core {};
            if (assumptions.empty() || last_conflict_clause_.empty() || reasons.empty())
            {
                return core;
            }

            std::vector<literal> pending_literals {last_conflict_clause_.begin(), last_conflict_clause_.end()};
            std::vector<bool> visited_variable(reasons.size(), false);

            while (!pending_literals.empty())
            {
                const auto lit = pending_literals.back();
                pending_literals.pop_back();

                const auto variable_index = static_cast<std::size_t>(lit.variable_of().index());
                if (variable_index == 0u || variable_index >= reasons.size() || variable_index >= visited_variable.size())
                {
                    continue;
                }
                if (visited_variable[variable_index])
                {
                    continue;
                }
                visited_variable[variable_index] = true;

                const auto assumption_it = std::find_if(assumptions.begin(), assumptions.end(),
                                                        [lit](const literal assumption) noexcept
                                                        {
                                                            return assumption.variable_of().index() == lit.variable_of().index();
                                                        });
                if (assumption_it != assumptions.end())
                {
                    const auto duplicate_it = std::find_if(core.begin(), core.end(),
                                                           [assumption_it](const literal existing) noexcept
                                                           {
                                                               return existing.raw() == assumption_it->raw();
                                                           });
                    if (duplicate_it == core.end())
                    {
                        core.push_back(*assumption_it);
                    }
                    continue;
                }

                const auto reason_ref = reasons[variable_index];
                if (!reason_ref.valid())
                {
                    continue;
                }

                const auto reason_clause = clause_database_.storage_of().literals_of(reason_ref);
                for (const auto reason_literal: reason_clause)
                {
                    const auto reason_variable_index = static_cast<std::size_t>(reason_literal.variable_of().index());
                    if (reason_variable_index == variable_index)
                    {
                        continue;
                    }
                    pending_literals.push_back(reason_literal);
                }
            }

            return core;
        }

        status consume_staged_conflict(const decision_level_vector& decision_levels, const reason_vector& reasons,
                                       const solve_request& request, std::uint64_t& conflicts, const trail_vector& trail,
                                       const std::uint32_t current_level) noexcept
        {
            const auto staged_conflict = propagator_.propagate();
            if (!staged_conflict.valid())
            {
                return status::satisfiable;
            }

            const auto conflict_clause = clause_database_.storage_of().literals_of(staged_conflict);
            if (conflict_clause.empty())
            {
                ++conflicts;
                return conflict_limit_reached(request, conflicts) ? status::unknown : status::unsatisfiable;
            }

            return handle_clause_conflict(staged_conflict, conflict_clause, decision_levels, reasons, request, conflicts, trail,
                                          current_level);
        }

        status handle_clause_conflict(const clause::ref_t ref, const std::span<const literal> conflict_clause,
                                      const decision_level_vector& decision_levels, const reason_vector& reasons,
                                      const solve_request& request, std::uint64_t& conflicts, const trail_vector& trail,
                                      const std::uint32_t current_level) noexcept
        {
            record_last_conflict_clause(conflict_clause);
            search_coordinator_.notify_conflict_clause(conflict_clause);

            const auto learned_clause_count_before = search_coordinator_.learned_clause_count();

            search_coordinator_.seed_conflict_clause(conflict_clause);
            const conflict_resolution_context resolution_context {&clause_database_, &decision_levels, &reasons};
            search_coordinator_.handle_conflict_via_resolution(trail, current_level, &conflict_level_lookup, &conflict_reason_lookup,
                                                               &resolution_context);

            if (search_coordinator_.learned_clause_count() != learned_clause_count_before)
            {
                const auto learned_clause = search_coordinator_.last_learned_clause();
                if (!learned_clause.empty())
                {
                    if (is_tautological_clause(learned_clause))
                    {
                        ++conflicts;
                        run_inprocess_if_due();
                        if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
                        {
                            return status::unknown;
                        }
                        return conflict_limit_reached(request, conflicts) ? status::unknown : status::unsatisfiable;
                    }

                    const auto learned_ref = clause_database_.add_clause(learned_clause, true);
                    clause_minimizer_.minimize_learned_clause(learned_ref, &conflict_level_lookup, &conflict_reason_lookup,
                                                              &resolution_context);
                    clause_minimizer_.shrink_clause(learned_ref);
                    clause_minimizer_.recompute_glue(learned_ref, &conflict_level_lookup, &resolution_context);
                    clause_minimizer_.promote_if_needed(learned_ref);
                    search_coordinator_.notify_learned_glue(clause_minimizer_.last_glue());
                    learned_clause_glue_total_ += clause_minimizer_.last_glue();
                    ++learned_clause_glue_sample_count_;

                    const auto finalized_learned_clause = clause_database_.storage_of().literals_of(learned_ref);
                    attach_clause_for_propagation(learned_ref);
                    if (clause_minimizer_.was_shrunk(learned_ref))
                    {
                        proof_manager_.on_shrink_clause(learned_ref, finalized_learned_clause);
                        ++learned_clause_shrink_event_count_;
                    }
                    const auto ordered_reason_refs =
                        build_ordered_reason_chain_refs(search_coordinator_.last_resolution_chain_literals(), reasons);
                    clause_database_.increment_activity(ref);
                    for (const auto reason_ref: ordered_reason_refs)
                    {
                        clause_database_.increment_activity(reason_ref);
                        clause_database_.increment_used_count(reason_ref);
                    }
                    const auto antecedents = build_conflict_antecedents(ref, ordered_reason_refs);
                    proof_manager_.on_add_derived(learned_ref, finalized_learned_clause, antecedents);
                    incremental_context_.retain_learned_clause();
                }
            }

            ++conflicts;
            run_inprocess_if_due();

            if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
            {
                return status::unknown;
            }
            return conflict_limit_reached(request, conflicts) ? status::unknown : status::unsatisfiable;
        }

        void run_inprocess_if_due() noexcept
        {
            const auto inprocess_epochs_before = inprocess_scheduler_.epoch_count();
            inprocess_scheduler_.set_conflicts_seen(search_coordinator_.conflict_event_count());
            inprocess_scheduler_.set_restart_count(search_coordinator_.restart_count());
            inprocess_scheduler_.set_decisions_seen(search_coordinator_.decision_event_count());
            inprocess_scheduler_.set_reduction_passes_seen(search_coordinator_.reduction_pass_count());
            inprocess_scheduler_.set_learned_clauses_seen(static_cast<std::uint64_t>(search_coordinator_.learned_clause_count()));
            inprocess_scheduler_.run_epoch();
            if (inprocess_scheduler_.epoch_count() > inprocess_epochs_before)
            {
                inprocess_scheduler_.report_epoch_summary();
                search_coordinator_.notify_inprocess_epoch_completed(inprocess_scheduler_.last_structural_gain());
            }
        }

        /// @brief Runs Boolean Constraint Propagation to fixpoint using real two-watched-literal indexing.
        ///
        /// @details
        /// Unit clauses (size <= 1) bypass the watch scheme entirely and are checked directly against the current
        /// assignment on every call, since a single literal has no second slot to watch. Every other active clause is
        /// indexed by exactly two watched literals in `watch_list_`; only an assignment that falsifies one of those
        /// two literals ever triggers a re-check of that clause (the Chaff/MiniSat two-watched-literal scheme), so
        /// propagation cost here is driven by the number of watch entries touched, not by the total clause count.
        /// @param seed_literals Externally-assigned literals (a decision, or the episode's assumptions) that were
        /// assigned by the caller before this call and must still have their watch entries checked here; without
        /// this seed, a decision/assumption literal that immediately falsifies a watched clause would never be
        /// detected, since the watch-scan below only follows literals it discovers itself.
        /// @throws None (noexcept).
        status propagate_units(assignment_vector& assignment, decision_level_vector& decision_levels, reason_vector& reasons,
                               const solve_request& request, std::uint64_t& conflicts, const std::uint32_t current_level,
                               trail_vector& trail, const std::span<const literal> seed_literals = {}) noexcept
        {
            {
                const auto staged_status = consume_staged_conflict(decision_levels, reasons, request, conflicts, trail, current_level);
                if (staged_status != status::satisfiable)
                {
                    return staged_status;
                }
            }

            propagation_queue_scratch_.clear();
            propagation_queue_scratch_.insert(propagation_queue_scratch_.end(), seed_literals.begin(), seed_literals.end());
            auto& propagation_queue = propagation_queue_scratch_;

            const auto enqueue_assignment = [&](const literal lit, const clause::ref_t reason_ref) noexcept -> bool
            {
                if (!assign_literal(assignment, decision_levels, reasons, lit, current_level, reason_ref))
                {
                    return false;
                }
                if (reason_ref.valid())
                {
                    clause_database_.mark_reason_clause(reason_ref);
                    clause_database_.increment_used_count(reason_ref);
                }
                ++propagation_assignment_count_;
                search_coordinator_.notify_assignment_literal(lit);
                propagation_queue.push_back(lit);
                trail.push_back(lit);
                return true;
            };

            for (const auto ref: unit_clause_refs_)
            {
                if (!clause_database_.storage_of().is_alive(ref))
                {
                    continue;
                }

                const auto literals = clause_database_.storage_of().literals_of(ref);
                if (literals.empty())
                {
                    propagator_.stage_conflict(ref);
                    continue;
                }

                const auto lit = literals.front();
                const auto index = lit.variable_of().index();
                if (index >= assignment.size())
                {
                    continue;
                }

                const auto value = assignment[index];
                if (value == unassigned_value)
                {
                    if (!enqueue_assignment(lit, ref))
                    {
                        propagator_.stage_conflict(ref);
                    }
                }
                else if (!literal_is_satisfied(lit, value))
                {
                    propagator_.stage_conflict(ref);
                }
            }

            {
                const auto unit_conflict_status =
                    consume_staged_conflict(decision_levels, reasons, request, conflicts, trail, current_level);
                if (unit_conflict_status != status::satisfiable)
                {
                    return unit_conflict_status;
                }
            }

            watch_snapshot_scratch_.clear();
            auto& watch_snapshot = watch_snapshot_scratch_;
            std::size_t queue_head = 0u;

            while (queue_head < propagation_queue.size())
            {
                const auto assigned_literal = propagation_queue[queue_head++];
                const auto false_literal = assigned_literal.negated();

                watch_snapshot.clear();
                watch_snapshot.reserve(watch_list_.size_of(false_literal));
                watch_list_.iterate(false_literal, [&](const watch& entry) noexcept { watch_snapshot.push_back(entry); });

                for (const auto& entry: watch_snapshot)
                {
                    ++watch_entry_scan_count_;
                    const auto ref = entry.clause_ref();
                    if (!clause_database_.storage_of().is_alive(ref))
                    {
                        watch_list_.unwatch_literal(false_literal, entry);
                        continue;
                    }

                    const auto blocking = entry.blocking_literal();
                    const auto blocking_index = blocking.variable_of().index();
                    const auto blocking_value = blocking_index < assignment.size() ? assignment[blocking_index] : unassigned_value;
                    if (literal_is_satisfied(blocking, blocking_value))
                    {
                        continue;
                    }

                    if (entry.is_binary())
                    {
                        ++binary_watch_scan_count_;
                        if (blocking_value == unassigned_value)
                        {
                            if (enqueue_assignment(blocking, ref))
                            {
                                continue;
                            }
                        }
                        ++binary_watch_conflict_count_;
                        propagator_.stage_conflict(ref);
                        return consume_staged_conflict(decision_levels, reasons, request, conflicts, trail, current_level);
                    }

                    const auto literals = clause_database_.storage_of().literals_of(ref);
                    literal replacement_watch {};
                    bool replacement_found = false;

                    for (const auto candidate: literals)
                    {
                        if (candidate.raw() == false_literal.raw() || candidate.raw() == blocking.raw())
                        {
                            continue;
                        }

                        const auto candidate_index = candidate.variable_of().index();
                        const auto candidate_value = candidate_index < assignment.size() ? assignment[candidate_index] : unassigned_value;
                        const auto candidate_is_false = candidate_value != unassigned_value && !literal_is_satisfied(candidate, candidate_value);
                        if (candidate_is_false)
                        {
                            continue;
                        }

                        replacement_watch = candidate;
                        replacement_found = true;
                        if (candidate_value != unassigned_value)
                        {
                            break;
                        }
                    }

                    if (replacement_found)
                    {
                        watch_list_.unwatch_literal(false_literal, entry);
                        watch_list_.watch_literal(replacement_watch, watch {blocking, ref, false});
                        continue;
                    }

                    if (blocking_value == unassigned_value)
                    {
                        if (enqueue_assignment(blocking, ref))
                        {
                            continue;
                        }
                    }

                    propagator_.stage_conflict(ref);
                    return consume_staged_conflict(decision_levels, reasons, request, conflicts, trail, current_level);
                }
            }

            if (!propagation_queue.empty())
            {
                std::vector<variable> propagated_variables {};
                propagated_variables.reserve(propagation_queue.size());
                for (const auto lit: propagation_queue)
                {
                    propagated_variables.push_back(lit.variable_of());
                }
                search_coordinator_.notify_propagated_variables(propagated_variables);
            }

            return status::satisfiable;
        }

        static std::uint32_t pick_unassigned_variable(const assignment_vector& assignment) noexcept
        {
            for (std::uint32_t index = 1; index < assignment.size(); ++index)
            {
                if (assignment[index] == unassigned_value)
                {
                    return index;
                }
            }
            return 0;
        }

        status solve_recursive(assignment_vector& assignment, decision_level_vector& decision_levels, reason_vector& reasons,
                               const solve_request& request, std::uint64_t& conflicts, const std::uint32_t current_level,
                               trail_vector& trail, const std::span<const literal> seed_literals = {}) noexcept
        {
            const auto propagation_status = propagate_units(assignment, decision_levels, reasons, request, conflicts, current_level,
                                                             trail, seed_literals);
            if (propagation_status != status::satisfiable)
            {
                return propagation_status;
            }

            bool has_unassigned_variable = false;
            for (std::size_t index = 1u; index < assignment.size(); ++index)
            {
                if (assignment[index] == unassigned_value)
                {
                    has_unassigned_variable = true;
                    break;
                }
            }
            if (!has_unassigned_variable)
            {
                return status::satisfiable;
            }

            search_coordinator_.set_variable_selectability_filter(&solver_core::is_unassigned_candidate, &assignment);

            const auto branch_literal = search_coordinator_.take_branch_literal(0u);
            if (decision_limit_reached(request, search_coordinator_.decision_event_count()))
            {
                return status::unknown;
            }
            if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
            {
                return status::unknown;
            }
            if (!branch_literal.has_value())
            {
                return search_coordinator_.current_outcome() == search_coordinator::outcome::satisfiable ? status::satisfiable :
                                                                                                           status::unknown;
            }

            auto selected_branch_literal = branch_literal.value();
            const auto selected_index = selected_branch_literal.variable_of().index();
            if (selected_index >= assignment.size() || assignment[selected_index] != unassigned_value)
            {
                const auto fallback_variable = pick_unassigned_variable(assignment);
                if (fallback_variable == 0u)
                {
                    return status::satisfiable;
                }
                selected_branch_literal = literal {variable {fallback_variable}, selected_branch_literal.is_negated()};
            }

            for (const auto candidate_literal: {selected_branch_literal, selected_branch_literal.negated()})
            {
                auto branch_assignment = assignment;
                auto branch_levels = decision_levels;
                auto branch_reasons = reasons;
                auto branch_trail = trail;

                if (!assign_literal(branch_assignment, branch_levels, branch_reasons, candidate_literal, current_level + 1u))
                {
                    continue;
                }

                search_coordinator_.notify_assignment_literal(candidate_literal);
                branch_trail.push_back(candidate_literal);

                const std::array<literal, 1> branch_seed {candidate_literal};
                const auto branch_status = solve_recursive(branch_assignment, branch_levels, branch_reasons, request, conflicts,
                                                            current_level + 1u, branch_trail, std::span<const literal> {branch_seed});
                if (branch_status == status::satisfiable)
                {
                    assignment = std::move(branch_assignment);
                    decision_levels = std::move(branch_levels);
                    reasons = std::move(branch_reasons);
                    trail = std::move(branch_trail);
                    return status::satisfiable;
                }
                if (branch_status == status::unknown)
                {
                    return status::unknown;
                }
            }

            return status::unsatisfiable;
        }

        static bool is_unassigned_candidate(const variable var, const void* context) noexcept
        {
            if (context == nullptr)
            {
                return true;
            }

            const auto* assignment = static_cast<const assignment_vector*>(context);
            const auto index = static_cast<std::size_t>(var.index());
            if (index >= assignment->size())
            {
                return false;
            }
            return (*assignment)[index] == unassigned_value;
        }

        void build_internal_model(const assignment_vector& assignment) noexcept
        {
            internal_model_.clear();
            if (assignment.size() <= 1)
            {
                return;
            }

            internal_model_.reserve(assignment.size() - 1);
            for (std::uint32_t index = 1; index < assignment.size(); ++index)
            {
                const auto value = assignment[index];
                const bool negated = value == false_value;
                internal_model_.push_back(literal {variable {index}, negated});
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
                    {
                        search_coordinator_.handle_termination();
                    }
                    break;
            }
        }

        clause::database clause_database_ {};
        search_coordinator search_coordinator_ {};
        propagator propagator_ {};
        incremental_context incremental_context_ {};
        memory_governor memory_governor_ {};
        bank::watch_list watch_list_ {};
        variable_mapper variable_mapper_ {};
        proof_manager proof_manager_ {};
        simplify::flush_restore_manager flush_restore_manager_ {};
        simplify::forward_subsumer forward_subsumer_ {};
        clause::minimizer clause_minimizer_ {};
        simplify::scheduler::preprocess preprocess_scheduler_ {};
        simplify::scheduler::inprocess inprocess_scheduler_ {};
        std::vector<clause::ref_t> unit_clause_refs_ {};
        std::size_t original_clause_count_ {};
        std::vector<literal> internal_model_ {};
        std::vector<literal> failed_core_ {};
        std::vector<literal> last_conflict_clause_ {};
        std::vector<literal> propagation_queue_scratch_ {};
        std::vector<watch> watch_snapshot_scratch_ {};
        std::size_t propagation_assignment_count_ {};
        std::size_t watch_entry_scan_count_ {};
        std::size_t binary_watch_scan_count_ {};
        std::size_t binary_watch_conflict_count_ {};
        std::size_t learned_clause_shrink_event_count_ {};
        std::uint64_t learned_clause_glue_total_ {};
        std::uint64_t learned_clause_glue_sample_count_ {};
        status status_ {status::unknown};
    };
}
