/// @file inc/kmx/sat/simplify/preprocessing_profile_selector.hpp
/// @brief Instance-aware selection and ordering of preprocessing passes from cheap structural fingerprints, avoiding
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <optional>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/simplify/pass_id.hpp>

namespace kmx::sat::simplify
{
    /// @brief Instance-aware selection and ordering of preprocessing passes from cheap structural fingerprints, avoiding
    /// fixed-pipeline time wasted on passes unlikely to help. Research-track scheduler policy layered on top of
    /// scheduler::preprocess, not a replacement for it.
    /// @details
    /// A fixed preprocessing pipeline spends time on every enabled pass regardless of whether the current instance's
    /// structure makes that pass likely to help; `preprocessing_profile_selector` instead computes cheap structural
    /// fingerprints (variable/clause ratio, clause-length distribution, gate density from `extractor::gate`) via
    /// `fingerprint_formula`, uses `select_pass_plan` to choose which passes `scheduler::preprocess` should
    /// enable/disable and in what order for this specific instance, and refines that choice over time using
    /// `record_pass_effectiveness`/`update_selection_policy` fed by each pass's actual yield.
    /// @note Research-track: this component only advises `scheduler::preprocess`'s pass selection and never bypasses
    /// it; any change to its selection policy must be validated on the comparative benchmarking harness before
    /// becoming a default.
    class preprocessing_profile_selector final
    {
    public:
        struct formula_fingerprint final
        {
            std::size_t clause_count {};
            std::size_t binary_clause_count {};
            std::size_t long_clause_count {};
            std::size_t total_literal_count {};
            double average_clause_length {};
            double binary_clause_ratio {};
            double long_clause_ratio {};
        };

        struct pass_plan final
        {
            std::uint64_t skip_pass_mask {};
            bool skip_memory_heavy_passes {};
            bool skip_probing_for_long_clause_heavy {};
            bool skip_memory_heavy_from_inprocess_pressure {};
            bool skip_memory_heavy_from_learned_clause_pressure {};
        };

        struct pass_effectiveness final
        {
            std::size_t observed_runs {};
            std::size_t effective_runs {};
        };

        /// @brief Constructs a profile selector with a default (fixed-order) selection policy.
        /// @throws None (noexcept).
        preprocessing_profile_selector() noexcept = default;

        /// @brief Attaches the clause database used to compute structural fingerprints.
        /// @param database Clause database scanned by `fingerprint_formula`.
        /// @throws None (noexcept).
        void attach_clause_database(cdcl::clause::database& database) noexcept { clause_database_ = &database; }

        /// @brief Computes cheap structural fingerprints of the current formula.
        /// @throws None (noexcept).
        void fingerprint_formula() noexcept;

        /// @brief Selects and orders which preprocessing passes should run for this formula's fingerprint.
        /// @throws None (noexcept).
        void select_pass_plan() noexcept;

        /// @brief Records the actual yield of a pass to refine future selection decisions.
        /// @throws None (noexcept).
        void record_pass_effectiveness() noexcept { ++effectiveness_count_; }

        /// @brief Records observed effectiveness for one pass.
        /// @param id Pass whose effect was measured.
        /// @param was_effective `true` if pass produced direct benefit, `false` if not, `nullopt` if unavailable.
        /// @throws None (noexcept).
        void record_pass_effectiveness(const pass_id id, const std::optional<bool> was_effective) noexcept;

        /// @brief Records whether formula memory usage is above the soft ceiling for this planning cycle.
        /// @param has_soft_pressure True when the attached memory governor is above soft budget.
        /// @throws None (noexcept).
        void set_soft_memory_pressure(const bool has_soft_pressure) noexcept { soft_memory_pressure_ = has_soft_pressure; }

        /// @brief Injects inprocess telemetry so preprocess and inprocess share pressure-model inputs.
        /// @param conflict_density_ema Inprocess conflict-density EMA.
        /// @param structural_gain_ema Inprocess structural-gain EMA.
        /// @param restart_pressure_ema Inprocess restart-pressure EMA.
        /// @param reduction_pressure_ema Inprocess reduction-pressure EMA.
        /// @param learned_clause_pressure_ema Inprocess learned-clause-pressure EMA.
        void set_inprocess_telemetry(const double conflict_density_ema, const double structural_gain_ema, const double restart_pressure_ema,
                                     const double reduction_pressure_ema, const double learned_clause_pressure_ema) noexcept;

        /// @brief Checks whether a pass should execute under the current plan.
        /// @param id Candidate pass.
        /// @return True when the pass is allowed in the active plan.
        /// @throws None (noexcept).
        bool should_run_pass(const pass_id id) const noexcept { return (current_plan_.skip_pass_mask & pass_bit(id)) == 0u; }

        const pass_plan& current_pass_plan() const noexcept { return current_plan_; }

        const formula_fingerprint& current_formula_fingerprint() const noexcept { return current_fingerprint_; }

        /// @brief Updates the underlying selection policy from accumulated pass-effectiveness history.
        /// @throws None (noexcept).
        void update_selection_policy() noexcept;

        std::size_t fingerprint_count() const noexcept { return fingerprint_count_; }

        std::size_t pass_plan_count() const noexcept { return pass_plan_count_; }

        std::size_t effectiveness_count() const noexcept { return effectiveness_count_; }

        std::size_t policy_update_count() const noexcept { return policy_update_count_; }

        const pass_effectiveness& factorizer_effectiveness() const noexcept { return factorizer_effectiveness_; }

        const pass_effectiveness& probing_effectiveness() const noexcept { return probing_effectiveness_; }

    private:
        static constexpr std::uint64_t pass_bit(const pass_id id) noexcept { return std::uint64_t {1u} << static_cast<std::size_t>(id); }

        static constexpr double default_probing_long_clause_threshold {0.75};
        static constexpr double low_effectiveness_hit_rate_threshold {0.10};
        static constexpr std::size_t minimum_effectiveness_sample_size {4u};
        static constexpr double elevated_conflict_density_threshold {0.25};
        static constexpr double elevated_restart_pressure_threshold {0.03};
        static constexpr double elevated_reduction_pressure_threshold {0.03};
        static constexpr double elevated_learned_clause_pressure_threshold {0.50};
        static constexpr double low_structural_gain_threshold {0.05};

        bool is_elevated_learned_clause_pressure() const noexcept
        {
            return inprocess_learned_clause_pressure_ema_ >= elevated_learned_clause_pressure_threshold;
        }

        bool should_skip_memory_heavy_from_inprocess_pressure() const noexcept;

        cdcl::clause::database* clause_database_ {};
        formula_fingerprint current_fingerprint_ {};
        pass_plan current_plan_ {};
        pass_effectiveness factorizer_effectiveness_ {};
        pass_effectiveness probing_effectiveness_ {};
        bool soft_memory_pressure_ {};
        bool has_inprocess_telemetry_ {};
        double inprocess_conflict_density_ema_ {};
        double inprocess_structural_gain_ema_ {};
        double inprocess_restart_pressure_ema_ {};
        double inprocess_reduction_pressure_ema_ {};
        double inprocess_learned_clause_pressure_ema_ {};
        double probing_long_clause_threshold_ {default_probing_long_clause_threshold};
        std::size_t fingerprint_count_ {};
        std::size_t pass_plan_count_ {};
        std::size_t effectiveness_count_ {};
        std::size_t policy_update_count_ {};
    };
}
