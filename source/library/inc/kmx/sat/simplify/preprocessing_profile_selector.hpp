/// @file inc/kmx/sat/simplify/preprocessing_profile_selector.hpp
/// @brief Instance-aware selection and ordering of preprocessing passes from cheap structural fingerprints, avoiding
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <optional>
    #include <string_view>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>

namespace kmx::sat::simplify
{
    enum class pass_id : std::uint8_t
    {
        transitive_reducer,
        decomposition,
        probing,
        forward_subsumer,
        blocked,
        covered,
        bounded,
        fast,
        instantiation,
        factorizer,
        gate,
        congruence,
        vivifier,
        sweep,
    };

    inline constexpr std::array<std::string_view, 14> pass_names {
        "transitive_reducer", "decomposition", "probing", "forward_subsumer", "blocked",  "covered", "bounded", "fast",
        "instantiation",      "factorizer",    "gate",    "congruence",       "vivifier", "sweep"};

    inline constexpr std::optional<pass_id> parse_pass(const std::string_view name) noexcept
    {
        std::uint32_t hash {2166136261u};
        for (const auto character: name)
        {
            hash ^= static_cast<std::uint8_t>(character);
            hash *= 16777619u;
        }

        constexpr std::array<std::uint32_t, 14> pass_hashes {0xb70710c1u, 0xda4c4018u, 0xd6a79702u, 0x5ef40fb1u, 0x5a5d6eb3u,
                                                             0xf6358681u, 0xff54b66au, 0x029402afu, 0x9a6bf24au, 0x587fb7f6u,
                                                             0x1660eb12u, 0x60fe7efau, 0x1171d8a3u, 0x518432e3u};
        for (std::size_t index {}; index < pass_hashes.size(); ++index)
            if (pass_hashes[index] == hash && pass_names[index] == name)
                return static_cast<pass_id>(index);
        return {};
    }

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
            bool skip_factorizer_for_binary_dense {};
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
        void fingerprint_formula() noexcept
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

        /// @brief Selects and orders which preprocessing passes should run for this formula's fingerprint.
        /// @throws None (noexcept).
        void select_pass_plan() noexcept
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
                    is_elevated_learned_clause_pressure() && inprocess_structural_gain_ema_ <= low_structural_gain_threshold;
                current_plan_.skip_memory_heavy_passes = true;
            }
            current_plan_.skip_factorizer_for_binary_dense =
                current_fingerprint_.clause_count >= 16u && current_fingerprint_.binary_clause_ratio >= factorizer_binary_dense_threshold_;
            current_plan_.skip_probing_for_long_clause_heavy = current_fingerprint_.clause_count >= 16u &&
                                                               current_fingerprint_.long_clause_ratio >= probing_long_clause_threshold_ &&
                                                               current_fingerprint_.binary_clause_ratio <= 0.10;
            if (current_plan_.skip_memory_heavy_passes)
                current_plan_.skip_pass_mask |= pass_bit(pass_id::congruence) | pass_bit(pass_id::vivifier);
            if (current_plan_.skip_factorizer_for_binary_dense)
                current_plan_.skip_pass_mask |= pass_bit(pass_id::factorizer);
            if (current_plan_.skip_probing_for_long_clause_heavy)
                current_plan_.skip_pass_mask |= pass_bit(pass_id::probing);
        }

        /// @brief Records the actual yield of a pass to refine future selection decisions.
        /// @throws None (noexcept).
        void record_pass_effectiveness() noexcept { ++effectiveness_count_; }

        /// @brief Records observed effectiveness for one named pass.
        /// @param pass_name Identifier of the pass whose effect was measured.
        /// @param was_effective `true` if pass produced direct benefit, `false` if not, `nullopt` if unavailable.
        /// @throws None (noexcept).
        void record_pass_effectiveness(const pass_id id, const std::optional<bool> was_effective) noexcept
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

        /// @brief Compatibility adapter for callers that configure passes by their external names.
        void record_pass_effectiveness(const std::string_view name, const std::optional<bool> was_effective) noexcept
        {
            const auto id = parse_pass(name);
            if (!id.has_value())
            {
                record_pass_effectiveness();
                return;
            }

            switch (id.value())
            {
                case pass_id::factorizer:
                case pass_id::probing:
                    record_pass_effectiveness(id.value(), was_effective);
                    return;
                default:
                    record_pass_effectiveness();
                    return;
            }
        }

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
                                     const double reduction_pressure_ema, const double learned_clause_pressure_ema) noexcept
        {
            inprocess_conflict_density_ema_ = conflict_density_ema;
            inprocess_structural_gain_ema_ = structural_gain_ema;
            inprocess_restart_pressure_ema_ = restart_pressure_ema;
            inprocess_reduction_pressure_ema_ = reduction_pressure_ema;
            inprocess_learned_clause_pressure_ema_ = learned_clause_pressure_ema;
            has_inprocess_telemetry_ = true;
        }

        /// @brief Checks whether a pass should execute under the current plan.
        /// @param pass_name Identifier of the candidate pass.
        /// @return True when the pass is allowed in the active plan.
        /// @throws None (noexcept).
        bool should_run_pass(const pass_id id) const noexcept { return (current_plan_.skip_pass_mask & pass_bit(id)) == 0u; }

        const pass_plan& current_pass_plan() const noexcept { return current_plan_; }

        const formula_fingerprint& current_formula_fingerprint() const noexcept { return current_fingerprint_; }

        /// @brief Updates the underlying selection policy from accumulated pass-effectiveness history.
        /// @throws None (noexcept).
        void update_selection_policy() noexcept
        {
            ++policy_update_count_;

            factorizer_binary_dense_threshold_ = default_factorizer_binary_dense_threshold;
            if (factorizer_effectiveness_.observed_runs >= minimum_effectiveness_sample_size)
            {
                const auto factorizer_hit_rate = static_cast<double>(factorizer_effectiveness_.effective_runs) /
                                                 static_cast<double>(factorizer_effectiveness_.observed_runs);
                if (factorizer_hit_rate <= low_effectiveness_hit_rate_threshold)
                    factorizer_binary_dense_threshold_ = 0.70;
            }

            probing_long_clause_threshold_ = default_probing_long_clause_threshold;
            if (probing_effectiveness_.observed_runs >= minimum_effectiveness_sample_size)
            {
                const auto probing_hit_rate =
                    static_cast<double>(probing_effectiveness_.effective_runs) / static_cast<double>(probing_effectiveness_.observed_runs);
                if (probing_hit_rate <= low_effectiveness_hit_rate_threshold)
                    probing_long_clause_threshold_ = 0.65;
            }
        }

        std::size_t fingerprint_count() const noexcept { return fingerprint_count_; }

        std::size_t pass_plan_count() const noexcept { return pass_plan_count_; }

        std::size_t effectiveness_count() const noexcept { return effectiveness_count_; }

        std::size_t policy_update_count() const noexcept { return policy_update_count_; }

        const pass_effectiveness& factorizer_effectiveness() const noexcept { return factorizer_effectiveness_; }

        const pass_effectiveness& probing_effectiveness() const noexcept { return probing_effectiveness_; }

    private:
        static constexpr std::uint64_t pass_bit(const pass_id id) noexcept { return std::uint64_t {1u} << static_cast<std::size_t>(id); }

        static constexpr double default_factorizer_binary_dense_threshold {0.80};
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

        bool should_skip_memory_heavy_from_inprocess_pressure() const noexcept
        {
            const bool elevated_search_pressure = inprocess_conflict_density_ema_ >= elevated_conflict_density_threshold ||
                                                  inprocess_restart_pressure_ema_ >= elevated_restart_pressure_threshold ||
                                                  inprocess_reduction_pressure_ema_ >= elevated_reduction_pressure_threshold ||
                                                  is_elevated_learned_clause_pressure();
            const bool low_structural_yield = inprocess_structural_gain_ema_ <= low_structural_gain_threshold;
            return elevated_search_pressure && low_structural_yield;
        }

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
        double factorizer_binary_dense_threshold_ {default_factorizer_binary_dense_threshold};
        double probing_long_clause_threshold_ {default_probing_long_clause_threshold};
        std::size_t fingerprint_count_ {};
        std::size_t pass_plan_count_ {};
        std::size_t effectiveness_count_ {};
        std::size_t policy_update_count_ {};
    };
}
