/// @file library/src/kmx/sat/simplify/scheduler/inprocess.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/scheduler/inprocess.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/scheduler/inprocess.hpp>

namespace kmx::sat::simplify::scheduler
{
    void inprocess::attach_clause_database(cdcl::clause::database& database) noexcept
    {
        clause_database_ = &database;
        forward_subsumer_.attach_database(database);
        vivifier_.attach_database(database);
        congruence_.attach_clause_database(database);
    }

    void inprocess::enable_pass(const pass_id_t id) noexcept
    {
        if (!is_known_pass(id) || is_enabled(id))
            return;
        enabled_passes_.push_back(id);
        // The adaptive cap is expressed in passes of the enabled set; changing the set resets it so that a
        // newly enabled pass is not silently throttled out of every epoch.
        adaptive_pass_cap_ = enabled_passes_.size();
    }

    void inprocess::set_conflicts_seen(const std::uint64_t conflicts) noexcept
    {
        if (conflicts < conflicts_seen_)
            next_conflict_trigger_ = conflict_trigger_window_;
        conflicts_seen_ = conflicts;
    }

    void inprocess::set_trigger_windows(const std::uint64_t conflict_window, const std::uint64_t restart_window) noexcept
    {
        if (conflict_window != 0u)
        {
            base_conflict_trigger_window_ = conflict_window;
            conflict_trigger_window_ = conflict_window;
            next_conflict_trigger_ = conflicts_seen_ + conflict_window;
        }
        if (restart_window != 0u)
        {
            base_restart_trigger_window_ = restart_window;
            restart_trigger_window_ = restart_window;
            next_restart_trigger_ = restart_count_ + restart_window;
        }
    }

    void inprocess::set_restart_count(const std::uint64_t restarts) noexcept
    {
        if (restarts < restart_count_)
        {
            next_restart_trigger_ = restart_trigger_window_;
            last_restart_snapshot_ = restarts;
        }
        restart_count_ = restarts;
    }

    void inprocess::set_decisions_seen(const std::uint64_t decisions) noexcept
    {
        if (decisions < decisions_seen_)
            last_decisions_snapshot_ = decisions;
        decisions_seen_ = decisions;
    }

    void inprocess::set_reduction_passes_seen(const std::uint64_t reduction_passes) noexcept
    {
        if (reduction_passes < reduction_passes_seen_)
            last_reduction_snapshot_ = reduction_passes;
        reduction_passes_seen_ = reduction_passes;
    }

    void inprocess::set_learned_clauses_seen(const std::uint64_t learned_clause_count) noexcept
    {
        if (learned_clause_count < learned_clauses_seen_)
            last_learned_clause_snapshot_ = learned_clause_count;
        learned_clauses_seen_ = learned_clause_count;
    }

    bool inprocess::should_run() const noexcept
    {
        if (abort_requested_)
            return false;

        if ((memory_governor_ != nullptr) && memory_governor_->hard_limit_breached())
            return false;

        return (conflicts_seen_ >= next_conflict_trigger_) || (restart_count_ >= next_restart_trigger_);
    }

    void inprocess::run_epoch() noexcept
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
        clause_delta_last_epoch_ = (before_counts > after_counts) ? before_counts - after_counts : 0u;
        record_effectiveness();
        update_gain_telemetry();
        update_budget_bonus();
        update_adaptive_pass_cap();
        update_cooldown_windows();

        if (conflicts_seen_ >= next_conflict_trigger_)
            next_conflict_trigger_ = conflicts_seen_ + conflict_trigger_window_;
        if (restart_count_ >= next_restart_trigger_)
            next_restart_trigger_ = restart_count_ + restart_trigger_window_;
    }

    void inprocess::run_pass_sequence() noexcept
    {
        last_passes_executed_ = 0u;
        const bool skip_memory_heavy_passes = (memory_governor_ != nullptr) && memory_governor_->soft_limit_breached();

        auto ordered_passes = enabled_passes_;
        std::stable_sort(ordered_passes.begin(), ordered_passes.end(),
                         [&](const pass_id_t left, const pass_id_t right) noexcept { return pass_priority(left) > pass_priority(right); });
        last_execution_order_ = ordered_passes;

        const auto max_passes =
            std::min<std::size_t>(ordered_passes.size(), std::min<std::size_t>(static_cast<std::size_t>(last_budget_),
                                                                               static_cast<std::size_t>(adaptive_pass_cap_)));

        for (std::size_t index {}; index < max_passes; ++index)
        {
            const auto pass = ordered_passes[index];
            if (skip_memory_heavy_passes && ((pass == pass_id_t::vivifier) || (pass == pass_id_t::congruence)))
            {
                last_run_summaries_.push_back(pass_summary {
                    pass,
                    false,
                    true,
                    false,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                });
                continue;
            }

            if (!is_proof_format_compatible(pass))
            {
                last_run_summaries_.push_back(pass_summary {
                    pass,
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

            switch (pass)
            {
                case pass_id_t::forward_subsumer:
                    forward_subsumer_.run();
                    break;
                case pass_id_t::vivifier:
                    vivifier_.set_budget(static_cast<std::size_t>(last_budget_));
                    vivifier_.run();
                    break;
                default:
                    congruence_.run();
                    break;
            }

            const auto clause_count_after = clause_counts_snapshot();
            const bool observed_effectiveness = clause_database_ != nullptr;
            const bool was_effective = clause_count_after < clause_count_before;
            record_pass_effectiveness(pass, observed_effectiveness, was_effective);
            last_run_summaries_.push_back(pass_summary {
                pass,
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

    void inprocess::record_effectiveness() noexcept
    {
        if (clause_delta_last_epoch_ > 0u)
        {
            last_effectiveness_ = clause_delta_last_epoch_;
            return;
        }

        last_effectiveness_ = (last_passes_executed_ > 0u) ? 1u : 0u;
    }

    void inprocess::update_adaptive_pass_cap() noexcept
    {
        const auto enabled_count = static_cast<std::uint64_t>(enabled_passes_.size());
        if (enabled_count == 0u)
        {
            adaptive_pass_cap_ = 0u;
            low_yield_streak_ = 0u;
            high_yield_streak_ = 0u;
            return;
        }

        if ((adaptive_pass_cap_ == 0u) || (adaptive_pass_cap_ > enabled_count))
            adaptive_pass_cap_ = enabled_count;

        if (clause_delta_last_epoch_ == 0u)
        {
            ++low_yield_streak_;
            high_yield_streak_ = 0u;
            if ((low_yield_streak_ >= 2u) && (adaptive_pass_cap_ > 1u))
            {
                --adaptive_pass_cap_;
                low_yield_streak_ = 0u;
            }
            return;
        }

        ++high_yield_streak_;
        low_yield_streak_ = 0u;
        if ((high_yield_streak_ >= 1u) && (adaptive_pass_cap_ < enabled_count))
            ++adaptive_pass_cap_;
    }

    void inprocess::update_cooldown_windows() noexcept
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
            // The floor is the cadence this scheduler was configured with, not the shipped default. A caller
            // that asked for a short cadence must get it back after a productive epoch; recovering to the
            // default instead would silently override the configuration.
            conflict_trigger_window_ = std::max(base_conflict_trigger_window_, conflict_trigger_window_ / 2u);
            restart_trigger_window_ = std::max(base_restart_trigger_window_, restart_trigger_window_ - 1u);
        }
    }

    void inprocess::update_search_telemetry() noexcept
    {
        const auto conflict_delta = (conflicts_seen_ >= last_conflicts_snapshot_) ? conflicts_seen_ - last_conflicts_snapshot_ : 0u;
        const auto decision_delta = (decisions_seen_ >= last_decisions_snapshot_) ? decisions_seen_ - last_decisions_snapshot_ : 0u;
        const auto restart_delta = (restart_count_ >= last_restart_snapshot_) ? restart_count_ - last_restart_snapshot_ : 0u;
        const auto reduction_delta =
            (reduction_passes_seen_ >= last_reduction_snapshot_) ? reduction_passes_seen_ - last_reduction_snapshot_ : 0u;
        const auto learned_clause_delta =
            (learned_clauses_seen_ >= last_learned_clause_snapshot_) ? learned_clauses_seen_ - last_learned_clause_snapshot_ : 0u;

        last_conflicts_snapshot_ = conflicts_seen_;
        last_decisions_snapshot_ = decisions_seen_;
        last_restart_snapshot_ = restart_count_;
        last_reduction_snapshot_ = reduction_passes_seen_;
        last_learned_clause_snapshot_ = learned_clauses_seen_;

        const auto density_denominator = (decision_delta == 0u) ? 1.0 : static_cast<double>(decision_delta);
        const auto conflict_density = static_cast<double>(conflict_delta) / density_denominator;
        const auto pressure_denominator = (decision_delta == 0u) ? 1.0 : static_cast<double>(decision_delta);
        const auto restart_pressure = static_cast<double>(restart_delta) / pressure_denominator;
        const auto reduction_pressure = static_cast<double>(reduction_delta) / pressure_denominator;
        const auto learned_clause_denominator = (conflict_delta == 0u) ? 1.0 : static_cast<double>(conflict_delta);
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

    void inprocess::update_gain_telemetry() noexcept
    {
        const auto gain_denominator = (last_passes_executed_ == 0u) ? 1.0 : static_cast<double>(last_passes_executed_);
        const auto observed_gain = static_cast<double>(clause_delta_last_epoch_) / gain_denominator;

        if (telemetry_samples_ == 0u)
            structural_gain_ema_ = observed_gain;
        else
            structural_gain_ema_ = (1.0 - telemetry_alpha) * structural_gain_ema_ + telemetry_alpha * observed_gain;

        ++telemetry_samples_;
    }

    void inprocess::update_budget_bonus() noexcept
    {
        if ((conflict_density_ema_ >= high_conflict_density_threshold) && (structural_gain_ema_ >= high_structural_gain_threshold))
        {
            telemetry_budget_bonus_ = 2u;
            return;
        }

        if ((restart_pressure_ema_ >= high_restart_pressure_threshold) || (reduction_pressure_ema_ >= high_reduction_pressure_threshold))
        {
            telemetry_budget_bonus_ = 2u;
            return;
        }

        if (learned_clause_pressure_ema_ >= high_learned_clause_pressure_threshold)
        {
            telemetry_budget_bonus_ = 2u;
            return;
        }

        if ((conflict_density_ema_ >= medium_conflict_density_threshold) || (structural_gain_ema_ >= medium_structural_gain_threshold) ||
            (restart_pressure_ema_ >= medium_restart_pressure_threshold) ||
            (reduction_pressure_ema_ >= medium_reduction_pressure_threshold) ||
            (learned_clause_pressure_ema_ >= medium_learned_clause_pressure_threshold))
        {
            telemetry_budget_bonus_ = 1u;
            return;
        }

        telemetry_budget_bonus_ = 0u;
    }

    const inprocess::pass_effectiveness& inprocess::pass_effectiveness_of(const pass_id_t id) const noexcept
    {
        switch (id)
        {
            case pass_id_t::forward_subsumer:
                return forward_subsumer_effectiveness_;
            case pass_id_t::vivifier:
                return vivifier_effectiveness_;
            default:
                return congruence_effectiveness_;
        }
    }

    bool inprocess::is_proof_format_compatible(const pass_id_t id) const noexcept
    {
        if ((proof_manager_ == nullptr) || !proof_manager_->has_enabled_formats())
            return true;

        if (id != pass_id_t::congruence)
            return true;

        return proof_manager_->has_enabled_format(proof::format_id::veripb);
    }

    std::size_t inprocess::clause_counts_snapshot() const noexcept
    {
        if (clause_database_ == nullptr)
            return 0u;

        const auto stats = clause_database_->stats_snapshot();
        return stats.irredundant_count + stats.redundant_count;
    }

    inprocess::pass_effectiveness& inprocess::pass_effectiveness_entry(const pass_id_t id) noexcept
    {
        switch (id)
        {
            case pass_id_t::forward_subsumer:
                return forward_subsumer_effectiveness_;
            case pass_id_t::vivifier:
                return vivifier_effectiveness_;
            default:
                return congruence_effectiveness_;
        }
    }

    void inprocess::record_pass_effectiveness(const pass_id_t id, const bool observed_effectiveness, const bool was_effective) noexcept
    {
        if (!observed_effectiveness)
            return;

        auto& entry = pass_effectiveness_entry(id);
        ++entry.observed_runs;
        if (was_effective)
            ++entry.effective_runs;
    }

    double inprocess::pass_priority(const pass_id_t id) const noexcept
    {
        const auto& entry = pass_effectiveness_of(id);
        if (entry.observed_runs == 0u)
            return 0.5;

        return static_cast<double>(entry.effective_runs) / static_cast<double>(entry.observed_runs);
    }
}
