/// @file library/src/kmx/sat/simplify/preprocessing_profile_selector.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/preprocessing_profile_selector.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/preprocessing_profile_selector.hpp>

namespace kmx::sat::simplify
{
    void preprocessing_profile_selector::fingerprint_formula() noexcept
    {
        ++fingerprint_count_;

        current_fingerprint_ = {};
        if (clause_database_ == nullptr)
            return;

        const auto collect_clause = [&](const cdcl::clause::ref_t ref) noexcept
        {
            if (clause_database_->is_garbage(ref))
                return;

            const auto literals = clause_database_->storage_of().view_literals(ref);
            current_fingerprint_.clause_count += 1u;
            current_fingerprint_.total_literal_count += literals.size();
            if (literals.size() == 2u)
                current_fingerprint_.binary_clause_count += 1u;
            if (literals.size() >= 5u)
                current_fingerprint_.long_clause_count += 1u;
        };

        clause_database_->iterate_irredundant(collect_clause);
        clause_database_->iterate_redundant(collect_clause);

        if (current_fingerprint_.clause_count == 0u)
            return;

        current_fingerprint_.average_clause_length =
            static_cast<double>(current_fingerprint_.total_literal_count) / static_cast<double>(current_fingerprint_.clause_count);
        current_fingerprint_.binary_clause_ratio =
            static_cast<double>(current_fingerprint_.binary_clause_count) / static_cast<double>(current_fingerprint_.clause_count);
        current_fingerprint_.long_clause_ratio =
            static_cast<double>(current_fingerprint_.long_clause_count) / static_cast<double>(current_fingerprint_.clause_count);
    }

    void preprocessing_profile_selector::select_pass_plan() noexcept
    {
        ++pass_plan_count_;
        current_plan_.skip_pass_mask = 0u;
        current_plan_.skip_memory_heavy_from_inprocess_pressure = false;
        current_plan_.skip_memory_heavy_from_learned_clause_pressure = false;
        current_plan_.skip_memory_heavy_passes = soft_memory_pressure_;
        if (has_inprocess_telemetry_ && should_skip_memory_heavy_from_inprocess_pressure())
        {
            current_plan_.skip_memory_heavy_from_inprocess_pressure = true;
            current_plan_.skip_memory_heavy_from_learned_clause_pressure =
                is_elevated_learned_clause_pressure() && (inprocess_structural_gain_ema_ <= low_structural_gain_threshold);
            current_plan_.skip_memory_heavy_passes = true;
        }
        current_plan_.skip_probing_for_long_clause_heavy = (current_fingerprint_.clause_count >= 16u) &&
                                                           (current_fingerprint_.long_clause_ratio >= probing_long_clause_threshold_) &&
                                                           (current_fingerprint_.binary_clause_ratio <= 0.10);
        if (current_plan_.skip_memory_heavy_passes)
            current_plan_.skip_pass_mask |= pass_bit(pass_id::congruence) | pass_bit(pass_id::vivifier);
        if (current_plan_.skip_probing_for_long_clause_heavy)
            current_plan_.skip_pass_mask |= pass_bit(pass_id::probing);
    }

    void preprocessing_profile_selector::record_pass_effectiveness(const pass_id id, const std::optional<bool> was_effective) noexcept
    {
        ++effectiveness_count_;
        if (was_effective.has_value())
            switch (id)
            {
                case pass_id::factorizer:
                    ++factorizer_effectiveness_.observed_runs;
                    if (*was_effective)
                        ++factorizer_effectiveness_.effective_runs;
                    break;
                case pass_id::probing:
                    ++probing_effectiveness_.observed_runs;
                    if (*was_effective)
                        ++probing_effectiveness_.effective_runs;
                    break;
                default:
                    break;
            }
    }

    void preprocessing_profile_selector::set_inprocess_telemetry(const double conflict_density_ema, const double structural_gain_ema,
                                                                 const double restart_pressure_ema, const double reduction_pressure_ema,
                                                                 const double learned_clause_pressure_ema) noexcept
    {
        inprocess_conflict_density_ema_ = conflict_density_ema;
        inprocess_structural_gain_ema_ = structural_gain_ema;
        inprocess_restart_pressure_ema_ = restart_pressure_ema;
        inprocess_reduction_pressure_ema_ = reduction_pressure_ema;
        inprocess_learned_clause_pressure_ema_ = learned_clause_pressure_ema;
        has_inprocess_telemetry_ = true;
    }

    void preprocessing_profile_selector::update_selection_policy() noexcept
    {
        ++policy_update_count_;

        probing_long_clause_threshold_ = default_probing_long_clause_threshold;
        if (probing_effectiveness_.observed_runs >= minimum_effectiveness_sample_size)
        {
            const auto probing_hit_rate =
                static_cast<double>(probing_effectiveness_.effective_runs) / static_cast<double>(probing_effectiveness_.observed_runs);
            if (probing_hit_rate <= low_effectiveness_hit_rate_threshold)
                probing_long_clause_threshold_ = 0.65;
        }
    }

    bool preprocessing_profile_selector::should_skip_memory_heavy_from_inprocess_pressure() const noexcept
    {
        const bool elevated_search_pressure = (inprocess_conflict_density_ema_ >= elevated_conflict_density_threshold) ||
                                              (inprocess_restart_pressure_ema_ >= elevated_restart_pressure_threshold) ||
                                              (inprocess_reduction_pressure_ema_ >= elevated_reduction_pressure_threshold) ||
                                              is_elevated_learned_clause_pressure();
        const bool low_structural_yield = inprocess_structural_gain_ema_ <= low_structural_gain_threshold;
        return elevated_search_pressure && low_structural_yield;
    }
}
