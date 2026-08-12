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
    /// @brief Instance-aware selection and ordering of preprocessing passes from cheap structural fingerprints, avoiding
    /// fixed-pipeline time wasted on passes unlikely to help. Research-track scheduler policy layered on top of
    /// scheduler::preprocess, not a replacement for it.
    ///
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
            std::size_t clause_count {0u};
            std::size_t binary_clause_count {0u};
            std::size_t long_clause_count {0u};
            std::size_t total_literal_count {0u};
            double average_clause_length {0.0};
            double binary_clause_ratio {0.0};
            double long_clause_ratio {0.0};
        };

        struct pass_plan final
        {
            bool skip_memory_heavy_passes {false};
            bool skip_factorizer_for_binary_dense {false};
            bool skip_probing_for_long_clause_heavy {false};
        };

        struct pass_effectiveness final
        {
            std::size_t observed_runs {0u};
            std::size_t effective_runs {0u};
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
            {
                return;
            }

            const auto collect_clause = [&](const cdcl::clause::ref_t ref) noexcept
            {
                if (clause_database_->is_garbage(ref))
                {
                    return;
                }

                const auto literals = clause_database_->storage_of().literals_of(ref);
                current_fingerprint_.clause_count += 1u;
                current_fingerprint_.total_literal_count += literals.size();
                if (literals.size() == 2u)
                {
                    current_fingerprint_.binary_clause_count += 1u;
                }
                if (literals.size() >= 5u)
                {
                    current_fingerprint_.long_clause_count += 1u;
                }
            };

            clause_database_->iterate_irredundant(collect_clause);
            clause_database_->iterate_redundant(collect_clause);

            if (current_fingerprint_.clause_count == 0u)
            {
                return;
            }

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
            current_plan_.skip_memory_heavy_passes = soft_memory_pressure_;
            current_plan_.skip_factorizer_for_binary_dense =
                current_fingerprint_.clause_count >= 16u && current_fingerprint_.binary_clause_ratio >= factorizer_binary_dense_threshold_;
            current_plan_.skip_probing_for_long_clause_heavy = current_fingerprint_.clause_count >= 16u &&
                                                               current_fingerprint_.long_clause_ratio >= probing_long_clause_threshold_ &&
                                                               current_fingerprint_.binary_clause_ratio <= 0.10;
        }

        /// @brief Records the actual yield of a pass to refine future selection decisions.
        /// @throws None (noexcept).
        void record_pass_effectiveness() noexcept { ++effectiveness_count_; }

        /// @brief Records observed effectiveness for one named pass.
        /// @param pass_name Identifier of the pass whose effect was measured.
        /// @param was_effective `true` if pass produced direct benefit, `false` if not, `nullopt` if unavailable.
        /// @throws None (noexcept).
        void record_pass_effectiveness(const std::string_view pass_name, const std::optional<bool> was_effective) noexcept
        {
            ++effectiveness_count_;
            if (!was_effective.has_value())
            {
                return;
            }

            if (pass_name == "factorizer")
            {
                ++factorizer_effectiveness_.observed_runs;
                if (*was_effective)
                {
                    ++factorizer_effectiveness_.effective_runs;
                }
                return;
            }

            if (pass_name == "probing")
            {
                ++probing_effectiveness_.observed_runs;
                if (*was_effective)
                {
                    ++probing_effectiveness_.effective_runs;
                }
            }
        }

        /// @brief Records whether formula memory usage is above the soft ceiling for this planning cycle.
        /// @param has_soft_pressure True when the attached memory governor is above soft budget.
        /// @throws None (noexcept).
        void set_soft_memory_pressure(const bool has_soft_pressure) noexcept { soft_memory_pressure_ = has_soft_pressure; }

        /// @brief Checks whether a pass should execute under the current plan.
        /// @param pass_name Identifier of the candidate pass.
        /// @return True when the pass is allowed in the active plan.
        /// @throws None (noexcept).
        bool should_run_pass(const std::string_view pass_name) const noexcept
        {
            if (current_plan_.skip_memory_heavy_passes)
            {
                for (const auto heavy_pass: memory_heavy_passes)
                {
                    if (pass_name == heavy_pass)
                    {
                        return false;
                    }
                }
            }

            if (current_plan_.skip_factorizer_for_binary_dense && pass_name == "factorizer")
            {
                return false;
            }

            if (current_plan_.skip_probing_for_long_clause_heavy && pass_name == "probing")
            {
                return false;
            }

            return true;
        }

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
                {
                    factorizer_binary_dense_threshold_ = 0.70;
                }
            }

            probing_long_clause_threshold_ = default_probing_long_clause_threshold;
            if (probing_effectiveness_.observed_runs >= minimum_effectiveness_sample_size)
            {
                const auto probing_hit_rate =
                    static_cast<double>(probing_effectiveness_.effective_runs) / static_cast<double>(probing_effectiveness_.observed_runs);
                if (probing_hit_rate <= low_effectiveness_hit_rate_threshold)
                {
                    probing_long_clause_threshold_ = 0.65;
                }
            }
        }

        std::size_t fingerprint_count() const noexcept { return fingerprint_count_; }

        std::size_t pass_plan_count() const noexcept { return pass_plan_count_; }

        std::size_t effectiveness_count() const noexcept { return effectiveness_count_; }

        std::size_t policy_update_count() const noexcept { return policy_update_count_; }

        const pass_effectiveness& factorizer_effectiveness() const noexcept { return factorizer_effectiveness_; }

        const pass_effectiveness& probing_effectiveness() const noexcept { return probing_effectiveness_; }

    private:
        static constexpr double default_factorizer_binary_dense_threshold {0.80};
        static constexpr double default_probing_long_clause_threshold {0.75};
        static constexpr double low_effectiveness_hit_rate_threshold {0.10};
        static constexpr std::size_t minimum_effectiveness_sample_size {4u};

        static constexpr std::array<std::string_view, 2> memory_heavy_passes {"congruence", "vivifier"};

        cdcl::clause::database* clause_database_ {nullptr};
        formula_fingerprint current_fingerprint_ {};
        pass_plan current_plan_ {};
        pass_effectiveness factorizer_effectiveness_ {};
        pass_effectiveness probing_effectiveness_ {};
        bool soft_memory_pressure_ {false};
        double factorizer_binary_dense_threshold_ {default_factorizer_binary_dense_threshold};
        double probing_long_clause_threshold_ {default_probing_long_clause_threshold};
        std::size_t fingerprint_count_ {0u};
        std::size_t pass_plan_count_ {0u};
        std::size_t effectiveness_count_ {0u};
        std::size_t policy_update_count_ {0u};
    };
}
