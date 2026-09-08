/// @file inc/kmx/sat/simplify/scheduler/inprocess.hpp
/// @brief All periodic simplifications that run during search.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <optional>
    #include <span>
    #include <vector>
#endif

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/memory_governor.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/engine/congruence.hpp>
#include <kmx/sat/simplify/forward_subsumer.hpp>
#include <kmx/sat/simplify/pass_id.hpp>
#include <kmx/sat/simplify/vivifier.hpp>

namespace kmx::sat::simplify::scheduler
{
    /// @brief All periodic simplifications that run during search.
    /// @details
    /// Unlike `scheduler::preprocess`, `scheduler::inprocess` runs repeatedly, interleaved with CDCL search epochs
    /// (the Kissat/CaDiCaL "inprocessing" style): `should_run` decides, based on conflict count and
    /// `telemetry::ema_tracker` signals, whether it is time for another simplification epoch;
    /// `compute_budget` bounds how much work (time/conflicts-equivalent) the epoch may spend, since inprocessing
    /// competes with search for the same conflict budget; `run_pass_sequence` executes the currently enabled subset
    /// of passes, orchestrated by `run_epoch`; `record_effectiveness` reports the epoch's yield (clauses removed,
    /// variables eliminated) to `preprocessing_profile_selector` and `telemetry::solver_statistics`.
    /// `controller::restart::reset_after_inprocess` must be called after an epoch changes the clause set, since
    /// restart interval counters accumulated before the epoch are no longer meaningful.
    /// @note Like `scheduler::preprocess`, this scheduler must consult `proof::proof_manager` for the active proof
    /// format(s) before running a format-sensitive pass, and `memory_governor`'s hard-ceiling escalation may suspend
    /// this scheduler entirely for the remainder of the current `solve()` call.
    class inprocess final
    {
    public:
        using pass_id_t = simplify::pass_id;

        struct pass_effectiveness final
        {
            std::uint64_t observed_runs {};
            std::uint64_t effective_runs {};
        };

        struct pass_summary final
        {
            pass_id_t id {};
            bool executed {};
            bool skipped_by_memory_policy {};
            bool skipped_by_proof_format {};
            std::optional<std::size_t> clause_count_before {};
            std::optional<std::size_t> clause_count_after {};
            std::optional<bool> was_effective {};
        };

        /// @brief Passes enabled by default. `congruence` is deliberately absent: its equivalence substitution
        /// merges variables without polarity and records no witness for model reconstruction, so an epoch could
        /// leave the search with a model that violates the original formula (observed on `bmc-ibm-13`). It stays
        /// available through `enable_pass` until that is fixed.
        static constexpr std::array<pass_id_t, 3u> known_passes {pass_id_t::forward_subsumer, pass_id_t::vivifier, pass_id_t::congruence};
        static constexpr std::array<pass_id_t, 2u> baseline_passes {pass_id_t::forward_subsumer, pass_id_t::vivifier};

        /// @brief Constructs an inprocess scheduler with a default epoch schedule.
        /// @throws None (noexcept).
        inprocess() noexcept: enabled_passes_ {baseline_passes.begin(), baseline_passes.end()}
        {
            adaptive_pass_cap_ = baseline_passes.size();
        }

        /// @brief Attaches a memory governor that can suspend inprocessing on hard pressure.
        /// @param governor Memory governor to consult before each epoch.
        /// @throws None (noexcept).
        void attach_memory_governor(cdcl::memory_governor& governor) noexcept { memory_governor_ = &governor; }

        /// @brief Attaches the clause database consumed by inprocessing passes.
        /// @param database Clause database to simplify in place.
        /// @throws None (noexcept).
        void attach_clause_database(cdcl::clause::database& database) noexcept;

        /// @brief Attaches watch-list storage used by congruence rewrite propagation.
        /// @param watch_list Watch list to rewrite.
        /// @throws None (noexcept).
        void attach_watch_list(cdcl::bank::watch_list& watch_list) noexcept { congruence_.attach_watch_list(watch_list); }

        /// @brief Attaches variable mapper used by congruence rewrite propagation.
        /// @param mapper Variable mapper to rewrite.
        /// @throws None (noexcept).
        void attach_variable_mapper(cdcl::variable_mapper& mapper) noexcept { congruence_.attach_variable_mapper(mapper); }

        /// @brief Attaches the proof manager used for format-aware pass gating.
        /// @param proof_manager Proof manager describing active proof formats.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept
        {
            proof_manager_ = &proof_manager;
            forward_subsumer_.attach_proof_manager(proof_manager);
        }

        /// @brief Enables one inprocessing pass.
        /// @param id Pass to enable.
        /// @throws None (noexcept).
        void enable_pass(const pass_id_t id) noexcept;

        /// @brief Disables one inprocessing pass.
        /// @param id Pass to disable.
        /// @throws None (noexcept).
        void disable_pass(const pass_id_t id) noexcept
        {
            enabled_passes_.erase(std::remove(enabled_passes_.begin(), enabled_passes_.end(), id), enabled_passes_.end());
            adaptive_pass_cap_ = enabled_passes_.size();
        }

        /// @brief Requests that the next inprocessing epoch be skipped.
        /// @throws None (noexcept).
        void request_abort() noexcept { abort_requested_ = true; }

        /// @brief Clears any explicit skip request.
        /// @throws None (noexcept).
        void clear_abort() noexcept { abort_requested_ = false; }

        /// @brief Sets the number of conflicts already seen by the search engine.
        void set_conflicts_seen(const std::uint64_t conflicts) noexcept;

        /// @brief Sets the conflict and restart cadences at which epochs become due.
        /// @details Exposed so a caller that knows its own workload -- or a test that wants a specific cadence --
        /// can state the schedule rather than inherit the default. Both counters are re-anchored to the values
        /// already seen, so the next epoch is one full window away rather than immediately due.
        /// @param conflict_window Conflicts between epochs (zero leaves the current window unchanged).
        /// @param restart_window Restarts between epochs (zero leaves the current window unchanged).
        void set_trigger_windows(const std::uint64_t conflict_window, const std::uint64_t restart_window) noexcept;

        /// @brief Sets how many restarts have already occurred.
        void set_restart_count(const std::uint64_t restarts) noexcept;

        /// @brief Sets how many decisions have already been produced by the search engine.
        void set_decisions_seen(const std::uint64_t decisions) noexcept;

        /// @brief Sets how many reduction passes have already been executed by CDCL maintenance.
        void set_reduction_passes_seen(const std::uint64_t reduction_passes) noexcept;

        /// @brief Sets how many learned clauses have already been produced by the current episode.
        void set_learned_clauses_seen(const std::uint64_t learned_clause_count) noexcept;

        /// @brief Checks whether an inprocessing epoch is due now.
        /// @return True if an epoch should run.
        /// @throws None (noexcept).
        bool should_run() const noexcept;

        /// @brief Runs one inprocessing epoch within its computed budget.
        /// @throws None (noexcept).
        void run_epoch() noexcept;

        /// @brief Computes the work budget available for the next inprocessing epoch.
        /// @return Budget value, in an implementation-defined conflict-equivalent unit.
        /// @throws None (noexcept).
        std::uint64_t compute_budget() const noexcept { return 1u + (conflicts_seen_ / 32u) + restart_count_ + telemetry_budget_bonus_; }

        /// @brief Executes the currently enabled sequence of inprocessing passes for one epoch.
        /// @throws None (noexcept).
        void run_pass_sequence() noexcept;
        double conflict_density_ema() const noexcept { return conflict_density_ema_; }

        double structural_gain_ema() const noexcept { return structural_gain_ema_; }

        double restart_pressure_ema() const noexcept { return restart_pressure_ema_; }

        double reduction_pressure_ema() const noexcept { return reduction_pressure_ema_; }

        double learned_clause_pressure_ema() const noexcept { return learned_clause_pressure_ema_; }

        std::uint64_t telemetry_budget_bonus() const noexcept { return telemetry_budget_bonus_; }

        /// @brief Captures the current epoch's per-pass summary for external reporting.
        void report_epoch_summary() const noexcept
        {
            last_reported_summaries_ = last_run_summaries_;
            ++reported_summary_count_;
        }

        /// @brief Records the effectiveness of the last epoch for future scheduling decisions.
        /// @throws None (noexcept).
        void record_effectiveness() noexcept;

        void update_adaptive_pass_cap() noexcept;

        void update_cooldown_windows() noexcept;

        void update_search_telemetry() noexcept;

        void update_gain_telemetry() noexcept;

        void update_budget_bonus() noexcept;

        std::uint64_t pass_count() const noexcept { return pass_count_; }

        std::uint64_t last_effectiveness() const noexcept { return last_effectiveness_; }

        std::size_t enabled_pass_count() const noexcept { return enabled_passes_.size(); }

        std::uint64_t last_passes_executed() const noexcept { return last_passes_executed_; }

        std::uint64_t adaptive_pass_cap() const noexcept { return adaptive_pass_cap_; }

        std::uint64_t last_structural_gain() const noexcept { return clause_delta_last_epoch_; }

        std::uint64_t conflict_trigger_window() const noexcept { return conflict_trigger_window_; }

        std::uint64_t restart_trigger_window() const noexcept { return restart_trigger_window_; }

        std::span<const pass_id_t> last_execution_order() const noexcept { return last_execution_order_; }

        const pass_effectiveness& pass_effectiveness_of(const pass_id_t id) const noexcept;

        /// @brief Returns how many epochs have been run.
        std::uint64_t epoch_count() const noexcept { return epoch_count_; }

        /// @brief Returns the budget used by the last epoch.
        std::uint64_t last_budget() const noexcept { return last_budget_; }

        bool abort_requested() const noexcept { return abort_requested_; }

        std::size_t reported_summary_count() const noexcept { return reported_summary_count_; }

        const std::vector<pass_summary>& last_reported_summaries() const noexcept { return last_reported_summaries_; }

    private:
        /// @brief Conflicts between inprocessing epochs, before yield-based adaptation.
        /// @details Every epoch runs subsumption, vivification and congruence over the whole clause database, so
        /// its cost is proportional to that database rather than to the window. At the previous value of 32 the
        /// sweeps cost about fifteen percent of total runtime and, worse, made a larger learned-clause database
        /// unaffordable: raising the retention limit from one thousand to ten thousand clauses took uuf250_01 from
        /// 7.5 s to 121 s, because the extra clauses were re-swept every 32 conflicts. Search is what closes an
        /// instance and simplification only assists it, so the window is set where an epoch is amortized over
        /// enough search to pay for itself.
        static constexpr std::uint64_t default_conflict_trigger_window {2000u};
        /// @brief Restarts between inprocessing epochs.
        /// @details Previously 1, which made every restart trigger a full simplification sweep. That coupling is
        /// what made restarts look like a regression and left them disabled: with restarts enabled on a Luby
        /// schedule the solver restarts thousands of times, and each one paid for a database-wide sweep.
        static constexpr std::uint64_t default_restart_trigger_window {512u};
        static constexpr std::uint64_t max_conflict_trigger_window {1u << 20u};
        static constexpr std::uint64_t max_restart_trigger_window {1u << 16u};
        static constexpr double telemetry_alpha {0.25};
        static constexpr double medium_conflict_density_threshold {0.10};
        static constexpr double high_conflict_density_threshold {0.25};
        static constexpr double medium_structural_gain_threshold {0.10};
        static constexpr double high_structural_gain_threshold {0.25};
        static constexpr double medium_restart_pressure_threshold {0.01};
        static constexpr double high_restart_pressure_threshold {0.03};
        static constexpr double medium_reduction_pressure_threshold {0.01};
        static constexpr double high_reduction_pressure_threshold {0.03};
        static constexpr double medium_learned_clause_pressure_threshold {0.20};
        static constexpr double high_learned_clause_pressure_threshold {0.50};

        bool is_known_pass(const pass_id_t id) const noexcept
        {
            return std::find(known_passes.begin(), known_passes.end(), id) != known_passes.end();
        }

        bool is_enabled(const pass_id_t id) const noexcept
        {
            return std::find(enabled_passes_.begin(), enabled_passes_.end(), id) != enabled_passes_.end();
        }

        bool is_proof_format_compatible(const pass_id_t id) const noexcept;

        std::size_t clause_counts_snapshot() const noexcept;

        pass_effectiveness& pass_effectiveness_entry(const pass_id_t id) noexcept;

        void record_pass_effectiveness(const pass_id_t id, const bool observed_effectiveness, const bool was_effective) noexcept;

        double pass_priority(const pass_id_t id) const noexcept;

        cdcl::memory_governor* memory_governor_ {};
        cdcl::clause::database* clause_database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::vector<pass_id_t> enabled_passes_ {};
        std::vector<pass_id_t> last_execution_order_ {};
        mutable std::vector<pass_summary> last_reported_summaries_ {};
        std::vector<pass_summary> last_run_summaries_ {};
        simplify::forward_subsumer forward_subsumer_ {};
        pass_effectiveness forward_subsumer_effectiveness_ {};
        simplify::vivifier vivifier_ {};
        pass_effectiveness vivifier_effectiveness_ {};
        simplify::engine::congruence congruence_ {};
        pass_effectiveness congruence_effectiveness_ {};
        bool abort_requested_ {};
        std::uint64_t conflicts_seen_ {};
        std::uint64_t restart_count_ {};
        std::uint64_t decisions_seen_ {};
        std::uint64_t reduction_passes_seen_ {};
        std::uint64_t learned_clauses_seen_ {};
        std::uint64_t last_conflicts_snapshot_ {};
        std::uint64_t last_decisions_snapshot_ {};
        std::uint64_t last_restart_snapshot_ {};
        std::uint64_t last_reduction_snapshot_ {};
        std::uint64_t last_learned_clause_snapshot_ {};
        std::uint64_t base_conflict_trigger_window_ {default_conflict_trigger_window};
        std::uint64_t base_restart_trigger_window_ {default_restart_trigger_window};
        std::uint64_t conflict_trigger_window_ {default_conflict_trigger_window};
        std::uint64_t restart_trigger_window_ {default_restart_trigger_window};
        std::uint64_t next_conflict_trigger_ {default_conflict_trigger_window};
        std::uint64_t next_restart_trigger_ {default_restart_trigger_window};
        std::uint64_t epoch_count_ {};
        std::uint64_t last_budget_ {};
        std::uint64_t pass_count_ {};
        std::uint64_t last_passes_executed_ {};
        std::uint64_t clause_delta_last_epoch_ {};
        std::uint64_t last_effectiveness_ {};
        std::uint64_t adaptive_pass_cap_ {};
        std::uint64_t low_yield_streak_ {};
        std::uint64_t high_yield_streak_ {};
        std::uint64_t cooldown_low_yield_streak_ {};
        std::uint64_t cooldown_high_yield_streak_ {};
        std::uint64_t telemetry_budget_bonus_ {};
        std::uint64_t telemetry_samples_ {};
        double conflict_density_ema_ {};
        double structural_gain_ema_ {};
        double restart_pressure_ema_ {};
        double reduction_pressure_ema_ {};
        double learned_clause_pressure_ema_ {};
        mutable std::size_t reported_summary_count_ {};
    };
}
