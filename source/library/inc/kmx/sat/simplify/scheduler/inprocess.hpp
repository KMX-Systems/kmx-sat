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
    #include <string_view>
    #include <vector>
#endif

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/memory_governor.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/engine/congruence.hpp>
#include <kmx/sat/simplify/forward_subsumer.hpp>
#include <kmx/sat/simplify/vivifier.hpp>

namespace kmx::sat::simplify::scheduler
{
    /// @brief All periodic simplifications that run during search.
    ///
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
        struct pass_effectiveness final
        {
            std::uint64_t observed_runs {};
            std::uint64_t effective_runs {};
        };

        struct pass_summary final
        {
            std::string_view pass_name {};
            bool executed {};
            bool skipped_by_memory_policy {};
            bool skipped_by_proof_format {};
            std::optional<std::size_t> clause_count_before {};
            std::optional<std::size_t> clause_count_after {};
            std::optional<bool> was_effective {};
        };

        static constexpr std::array<std::string_view, 3> baseline_passes {"forward_subsumer", "vivifier", "congruence"};

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
        void attach_clause_database(cdcl::clause::database& database) noexcept
        {
            clause_database_ = &database;
            forward_subsumer_.attach_database(database);
            vivifier_.attach_database(database);
            congruence_.attach_clause_database(database);
        }

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

        /// @brief Enables one inprocessing pass by name.
        /// @param pass_name Pass identifier to enable.
        /// @throws None (noexcept).
        void enable_pass(const std::string_view pass_name) noexcept
        {
            if (!is_known_pass(pass_name) || is_enabled(pass_name))
            {
                return;
            }
            enabled_passes_.push_back(pass_name);
        }

        /// @brief Disables one inprocessing pass by name.
        /// @param pass_name Pass identifier to disable.
        /// @throws None (noexcept).
        void disable_pass(const std::string_view pass_name) noexcept
        {
            enabled_passes_.erase(std::remove(enabled_passes_.begin(), enabled_passes_.end(), pass_name), enabled_passes_.end());
        }

        /// @brief Requests that the next inprocessing epoch be skipped.
        /// @throws None (noexcept).
        void request_abort() noexcept { abort_requested_ = true; }

        /// @brief Clears any explicit skip request.
        /// @throws None (noexcept).
        void clear_abort() noexcept { abort_requested_ = false; }

        /// @brief Sets the number of conflicts already seen by the search engine.
        void set_conflicts_seen(const std::uint64_t conflicts) noexcept
        {
            if (conflicts < conflicts_seen_)
            {
                next_conflict_trigger_ = conflict_trigger_window_;
            }
            conflicts_seen_ = conflicts;
        }

        /// @brief Sets how many restarts have already occurred.
        void set_restart_count(const std::uint64_t restarts) noexcept
        {
            if (restarts < restart_count_)
            {
                next_restart_trigger_ = restart_trigger_window_;
                last_restart_snapshot_ = restarts;
            }
            restart_count_ = restarts;
        }

        /// @brief Sets how many decisions have already been produced by the search engine.
        void set_decisions_seen(const std::uint64_t decisions) noexcept
        {
            if (decisions < decisions_seen_)
            {
                last_decisions_snapshot_ = decisions;
            }
            decisions_seen_ = decisions;
        }

        /// @brief Sets how many reduction passes have already been executed by CDCL maintenance.
        void set_reduction_passes_seen(const std::uint64_t reduction_passes) noexcept
        {
            if (reduction_passes < reduction_passes_seen_)
            {
                last_reduction_snapshot_ = reduction_passes;
            }
            reduction_passes_seen_ = reduction_passes;
        }

        /// @brief Sets how many learned clauses have already been produced by the current episode.
        void set_learned_clauses_seen(const std::uint64_t learned_clause_count) noexcept
        {
            if (learned_clause_count < learned_clauses_seen_)
            {
                last_learned_clause_snapshot_ = learned_clause_count;
            }
            learned_clauses_seen_ = learned_clause_count;
        }

        /// @brief Checks whether an inprocessing epoch is due now.
        /// @return True if an epoch should run.
        /// @throws None (noexcept).
        bool should_run() const noexcept
        {
            if (abort_requested_)
            {
                return false;
            }

            if (memory_governor_ != nullptr && memory_governor_->hard_limit_breached())
            {
                return false;
            }

            return conflicts_seen_ >= next_conflict_trigger_ || restart_count_ >= next_restart_trigger_;
        }

        /// @brief Runs one inprocessing epoch within its computed budget.
        /// @throws None (noexcept).
        void run_epoch() noexcept
        {
            if (!should_run())
            {
                last_budget_ = 0u;
                last_effectiveness_ = 0u;
                last_passes_executed_ = 0u;
                last_run_summaries_.clear();
                return;
            }

            ++epoch_count_;
            update_search_telemetry();
            last_budget_ = compute_budget();
            last_run_summaries_.clear();
            const auto before_counts = clause_counts_snapshot();
            run_pass_sequence();
            const auto after_counts = clause_counts_snapshot();
            clause_delta_last_epoch_ = before_counts > after_counts ? before_counts - after_counts : 0u;
            record_effectiveness();
            update_gain_telemetry();
            update_budget_bonus();
            update_adaptive_pass_cap();
            update_cooldown_windows();

            if (conflicts_seen_ >= next_conflict_trigger_)
            {
                next_conflict_trigger_ = conflicts_seen_ + conflict_trigger_window_;
            }
            if (restart_count_ >= next_restart_trigger_)
            {
                next_restart_trigger_ = restart_count_ + restart_trigger_window_;
            }
        }

        /// @brief Computes the work budget available for the next inprocessing epoch.
        /// @return Budget value, in an implementation-defined conflict-equivalent unit.
        /// @throws None (noexcept).
        std::uint64_t compute_budget() const noexcept { return 1u + (conflicts_seen_ / 32u) + restart_count_ + telemetry_budget_bonus_; }

        /// @brief Executes the currently enabled sequence of inprocessing passes for one epoch.
        /// @throws None (noexcept).
        void run_pass_sequence() noexcept
        {
            last_passes_executed_ = 0u;
            const bool skip_memory_heavy_passes = memory_governor_ != nullptr && memory_governor_->soft_limit_breached();

            auto ordered_passes = enabled_passes_;
            std::stable_sort(ordered_passes.begin(), ordered_passes.end(),
                             [&](const std::string_view left, const std::string_view right) noexcept
                             { return pass_priority(left) > pass_priority(right); });
            last_execution_order_ = ordered_passes;

            const auto max_passes =
                std::min<std::size_t>(ordered_passes.size(), std::min<std::size_t>(static_cast<std::size_t>(last_budget_),
                                                                                   static_cast<std::size_t>(adaptive_pass_cap_)));

            for (std::size_t index {}; index < max_passes; ++index)
            {
                const auto pass_name = ordered_passes[index];
                if (skip_memory_heavy_passes && (pass_name == "vivifier" || pass_name == "congruence"))
                {
                    last_run_summaries_.push_back(pass_summary {
                        pass_name,
                        false,
                        true,
                        false,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                    });
                    continue;
                }

                if (!is_proof_format_compatible(pass_name))
                {
                    last_run_summaries_.push_back(pass_summary {
                        pass_name,
                        false,
                        false,
                        true,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                    });
                    continue;
                }

                const auto clause_count_before = clause_counts_snapshot();

                if (pass_name == "forward_subsumer")
                {
                    forward_subsumer_.run();
                }
                else if (pass_name == "vivifier")
                {
                    vivifier_.set_budget(static_cast<std::size_t>(last_budget_));
                    vivifier_.run();
                }
                else if (pass_name == "congruence")
                {
                    congruence_.run();
                }

                const auto clause_count_after = clause_counts_snapshot();
                const bool observed_effectiveness = clause_database_ != nullptr;
                const bool was_effective = clause_count_after < clause_count_before;
                record_pass_effectiveness(pass_name, observed_effectiveness, was_effective);
                last_run_summaries_.push_back(pass_summary {
                    pass_name,
                    true,
                    false,
                    false,
                    observed_effectiveness ? std::optional<std::size_t> {clause_count_before} : std::nullopt,
                    observed_effectiveness ? std::optional<std::size_t> {clause_count_after} : std::nullopt,
                    observed_effectiveness ? std::optional<bool> {was_effective} : std::nullopt,
                });

                ++pass_count_;
                ++last_passes_executed_;
            }
        }
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
        void record_effectiveness() noexcept
        {
            if (clause_delta_last_epoch_ > 0u)
            {
                last_effectiveness_ = clause_delta_last_epoch_;
                return;
            }

            last_effectiveness_ = last_passes_executed_ > 0u ? 1u : 0u;
        }

        void update_adaptive_pass_cap() noexcept
        {
            const auto enabled_count = static_cast<std::uint64_t>(enabled_passes_.size());
            if (enabled_count == 0u)
            {
                adaptive_pass_cap_ = 0u;
                low_yield_streak_ = 0u;
                high_yield_streak_ = 0u;
                return;
            }

            if (adaptive_pass_cap_ == 0u || adaptive_pass_cap_ > enabled_count)
            {
                adaptive_pass_cap_ = enabled_count;
            }

            if (clause_delta_last_epoch_ == 0u)
            {
                ++low_yield_streak_;
                high_yield_streak_ = 0u;
                if (low_yield_streak_ >= 2u && adaptive_pass_cap_ > 1u)
                {
                    --adaptive_pass_cap_;
                    low_yield_streak_ = 0u;
                }
                return;
            }

            ++high_yield_streak_;
            low_yield_streak_ = 0u;
            if (high_yield_streak_ >= 1u && adaptive_pass_cap_ < enabled_count)
            {
                ++adaptive_pass_cap_;
            }
        }

        void update_cooldown_windows() noexcept
        {
            if (clause_delta_last_epoch_ == 0u)
            {
                ++cooldown_low_yield_streak_;
                cooldown_high_yield_streak_ = 0u;
                if (cooldown_low_yield_streak_ >= 2u)
                {
                    conflict_trigger_window_ = std::min(max_conflict_trigger_window, conflict_trigger_window_ * 2u);
                    restart_trigger_window_ = std::min(max_restart_trigger_window, restart_trigger_window_ + 1u);
                    cooldown_low_yield_streak_ = 0u;
                }
                return;
            }

            ++cooldown_high_yield_streak_;
            cooldown_low_yield_streak_ = 0u;
            if (cooldown_high_yield_streak_ >= 1u)
            {
                conflict_trigger_window_ = std::max(default_conflict_trigger_window, conflict_trigger_window_ / 2u);
                restart_trigger_window_ = std::max(default_restart_trigger_window, restart_trigger_window_ - 1u);
            }
        }

        void update_search_telemetry() noexcept
        {
            const auto conflict_delta = conflicts_seen_ >= last_conflicts_snapshot_ ? conflicts_seen_ - last_conflicts_snapshot_ : 0u;
            const auto decision_delta = decisions_seen_ >= last_decisions_snapshot_ ? decisions_seen_ - last_decisions_snapshot_ : 0u;
            const auto restart_delta = restart_count_ >= last_restart_snapshot_ ? restart_count_ - last_restart_snapshot_ : 0u;
            const auto reduction_delta =
                reduction_passes_seen_ >= last_reduction_snapshot_ ? reduction_passes_seen_ - last_reduction_snapshot_ : 0u;
            const auto learned_clause_delta = learned_clauses_seen_ >= last_learned_clause_snapshot_ ?
                                                  learned_clauses_seen_ - last_learned_clause_snapshot_ :
                                                  0u;

            last_conflicts_snapshot_ = conflicts_seen_;
            last_decisions_snapshot_ = decisions_seen_;
            last_restart_snapshot_ = restart_count_;
            last_reduction_snapshot_ = reduction_passes_seen_;
            last_learned_clause_snapshot_ = learned_clauses_seen_;

            const auto density_denominator = decision_delta == 0u ? 1.0 : static_cast<double>(decision_delta);
            const auto conflict_density = static_cast<double>(conflict_delta) / density_denominator;
            const auto pressure_denominator = decision_delta == 0u ? 1.0 : static_cast<double>(decision_delta);
            const auto restart_pressure = static_cast<double>(restart_delta) / pressure_denominator;
            const auto reduction_pressure = static_cast<double>(reduction_delta) / pressure_denominator;
            const auto learned_clause_denominator = conflict_delta == 0u ? 1.0 : static_cast<double>(conflict_delta);
            const auto learned_clause_pressure = static_cast<double>(learned_clause_delta) / learned_clause_denominator;

            if (telemetry_samples_ == 0u)
            {
                conflict_density_ema_ = conflict_density;
                restart_pressure_ema_ = restart_pressure;
                reduction_pressure_ema_ = reduction_pressure;
                learned_clause_pressure_ema_ = learned_clause_pressure;
            }
            else
            {
                conflict_density_ema_ = (1.0 - telemetry_alpha) * conflict_density_ema_ + telemetry_alpha * conflict_density;
                restart_pressure_ema_ = (1.0 - telemetry_alpha) * restart_pressure_ema_ + telemetry_alpha * restart_pressure;
                reduction_pressure_ema_ = (1.0 - telemetry_alpha) * reduction_pressure_ema_ + telemetry_alpha * reduction_pressure;
                learned_clause_pressure_ema_ =
                    (1.0 - telemetry_alpha) * learned_clause_pressure_ema_ + telemetry_alpha * learned_clause_pressure;
            }
        }

        void update_gain_telemetry() noexcept
        {
            const auto gain_denominator = last_passes_executed_ == 0u ? 1.0 : static_cast<double>(last_passes_executed_);
            const auto observed_gain = static_cast<double>(clause_delta_last_epoch_) / gain_denominator;

            if (telemetry_samples_ == 0u)
            {
                structural_gain_ema_ = observed_gain;
            }
            else
            {
                structural_gain_ema_ = (1.0 - telemetry_alpha) * structural_gain_ema_ + telemetry_alpha * observed_gain;
            }

            ++telemetry_samples_;
        }

        void update_budget_bonus() noexcept
        {
            if (conflict_density_ema_ >= high_conflict_density_threshold && structural_gain_ema_ >= high_structural_gain_threshold)
            {
                telemetry_budget_bonus_ = 2u;
                return;
            }

            if (restart_pressure_ema_ >= high_restart_pressure_threshold || reduction_pressure_ema_ >= high_reduction_pressure_threshold)
            {
                telemetry_budget_bonus_ = 2u;
                return;
            }

            if (learned_clause_pressure_ema_ >= high_learned_clause_pressure_threshold)
            {
                telemetry_budget_bonus_ = 2u;
                return;
            }

            if (conflict_density_ema_ >= medium_conflict_density_threshold || structural_gain_ema_ >= medium_structural_gain_threshold ||
                restart_pressure_ema_ >= medium_restart_pressure_threshold ||
                reduction_pressure_ema_ >= medium_reduction_pressure_threshold ||
                learned_clause_pressure_ema_ >= medium_learned_clause_pressure_threshold)
            {
                telemetry_budget_bonus_ = 1u;
                return;
            }

            telemetry_budget_bonus_ = 0u;
        }

        std::uint64_t pass_count() const noexcept { return pass_count_; }

        std::uint64_t last_effectiveness() const noexcept { return last_effectiveness_; }

        std::size_t enabled_pass_count() const noexcept { return enabled_passes_.size(); }

        std::uint64_t last_passes_executed() const noexcept { return last_passes_executed_; }

        std::uint64_t adaptive_pass_cap() const noexcept { return adaptive_pass_cap_; }

        std::uint64_t last_structural_gain() const noexcept { return clause_delta_last_epoch_; }

        std::uint64_t conflict_trigger_window() const noexcept { return conflict_trigger_window_; }

        std::uint64_t restart_trigger_window() const noexcept { return restart_trigger_window_; }

        std::span<const std::string_view> last_execution_order() const noexcept { return last_execution_order_; }

        const pass_effectiveness& pass_effectiveness_of(const std::string_view pass_name) const noexcept
        {
            if (pass_name == "forward_subsumer")
            {
                return forward_subsumer_effectiveness_;
            }
            if (pass_name == "vivifier")
            {
                return vivifier_effectiveness_;
            }
            return congruence_effectiveness_;
        }

        /// @brief Returns how many epochs have been run.
        std::uint64_t epoch_count() const noexcept { return epoch_count_; }

        /// @brief Returns the budget used by the last epoch.
        std::uint64_t last_budget() const noexcept { return last_budget_; }

        bool abort_requested() const noexcept { return abort_requested_; }

        std::size_t reported_summary_count() const noexcept { return reported_summary_count_; }

        const std::vector<pass_summary>& last_reported_summaries() const noexcept { return last_reported_summaries_; }

    private:
        static constexpr std::uint64_t default_conflict_trigger_window {32u};
        static constexpr std::uint64_t default_restart_trigger_window {1u};
        static constexpr std::uint64_t max_conflict_trigger_window {128u};
        static constexpr std::uint64_t max_restart_trigger_window {4u};
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

        bool is_known_pass(const std::string_view pass_name) const noexcept
        {
            return std::find(baseline_passes.begin(), baseline_passes.end(), pass_name) != baseline_passes.end();
        }

        bool is_enabled(const std::string_view pass_name) const noexcept
        {
            return std::find(enabled_passes_.begin(), enabled_passes_.end(), pass_name) != enabled_passes_.end();
        }

        bool is_proof_format_compatible(const std::string_view pass_name) const noexcept
        {
            if (proof_manager_ == nullptr || !proof_manager_->has_enabled_formats())
            {
                return true;
            }

            if (pass_name != "congruence")
            {
                return true;
            }

            return proof_manager_->has_enabled_format("veripb");
        }

        std::size_t clause_counts_snapshot() const noexcept
        {
            if (clause_database_ == nullptr)
            {
                return 0u;
            }

            const auto stats = clause_database_->stats_snapshot();
            return stats.irredundant_count + stats.redundant_count;
        }

        pass_effectiveness& pass_effectiveness_entry(const std::string_view pass_name) noexcept
        {
            if (pass_name == "forward_subsumer")
            {
                return forward_subsumer_effectiveness_;
            }
            if (pass_name == "vivifier")
            {
                return vivifier_effectiveness_;
            }
            return congruence_effectiveness_;
        }

        void record_pass_effectiveness(const std::string_view pass_name, const bool observed_effectiveness,
                                       const bool was_effective) noexcept
        {
            if (!observed_effectiveness)
            {
                return;
            }

            auto& entry = pass_effectiveness_entry(pass_name);
            ++entry.observed_runs;
            if (was_effective)
            {
                ++entry.effective_runs;
            }
        }

        double pass_priority(const std::string_view pass_name) const noexcept
        {
            const auto& entry = pass_effectiveness_of(pass_name);
            if (entry.observed_runs == 0u)
            {
                return 0.5;
            }

            return static_cast<double>(entry.effective_runs) / static_cast<double>(entry.observed_runs);
        }

        cdcl::memory_governor* memory_governor_ {};
        cdcl::clause::database* clause_database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::vector<std::string_view> enabled_passes_ {};
        std::vector<std::string_view> last_execution_order_ {};
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
        double conflict_density_ema_ {0.0};
        double structural_gain_ema_ {0.0};
        double restart_pressure_ema_ {0.0};
        double reduction_pressure_ema_ {0.0};
        double learned_clause_pressure_ema_ {0.0};
        mutable std::size_t reported_summary_count_ {};
    };
}
