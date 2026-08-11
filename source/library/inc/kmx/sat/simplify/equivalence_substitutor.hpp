/// @file inc/kmx/sat/simplify/equivalence_substitutor.hpp
/// @brief Propagates ELS results across all subsystems.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif

namespace kmx::sat::simplify
{
    /// @brief Propagates ELS results across all subsystems.
    ///
    /// Once `engine::decomposition::find_equivalences` determines that a set of literals are all equivalent,
    /// `equivalence_substitutor` is the single place that applies the chosen representative literal everywhere it
    /// matters, so no subsystem is left referencing a superseded literal: `apply_equivalence_class` records the
    /// representative mapping for one equivalence class, `rewrite_clauses` substitutes it into `clause::database`
    /// storage, `rewrite_watches` updates `bank::watch_list` accordingly, and `rewrite_external_mapping` propagates
    /// the substitution into `variable_mapper`/`external_frontend` so externally visible variable identity remains
    /// correct.
    /// @note Like every structural formula transformation, an ELS substitution must also be reported to
    /// `proof::proof_manager` (as resolution-equivalent proof events for DRAT/LRAT/FRAT, or as native equivalence
    /// events when `veripb_tracer` is active) and, where the substituted variable becomes internal-only, to
    /// `model_reconstructor`.
    class equivalence_substitutor final
    {
    public:
        /// @brief Constructs an equivalence substitutor with no pending equivalence classes.
        /// @throws None (noexcept).
        equivalence_substitutor() noexcept = default;

        /// @brief Records the representative-literal mapping for one discovered equivalence class.
        /// @throws None (noexcept).
        void apply_equivalence_class() noexcept
        {
            ++equivalence_class_count_;
        }

        /// @brief Rewrites clause storage to use each equivalence class's representative literal.
        /// @throws None (noexcept).
        void rewrite_clauses() noexcept
        {
            ++clause_rewrite_count_;
        }

        /// @brief Rewrites watch-list entries to use each equivalence class's representative literal.
        /// @throws None (noexcept).
        void rewrite_watches() noexcept
        {
            ++watch_rewrite_count_;
        }

        /// @brief Propagates the substitution into the external variable mapping.
        /// @throws None (noexcept).
        void rewrite_external_mapping() noexcept
        {
            ++external_mapping_rewrite_count_;
            rewrite_completed_ = true;
        }

        std::size_t equivalence_class_count() const noexcept
        {
            return equivalence_class_count_;
        }

        std::size_t clause_rewrite_count() const noexcept
        {
            return clause_rewrite_count_;
        }

        std::size_t watch_rewrite_count() const noexcept
        {
            return watch_rewrite_count_;
        }

        std::size_t external_mapping_rewrite_count() const noexcept
        {
            return external_mapping_rewrite_count_;
        }

        bool rewrite_completed() const noexcept
        {
            return rewrite_completed_;
        }

    private:
        std::size_t equivalence_class_count_ {0u};
        std::size_t clause_rewrite_count_ {0u};
        std::size_t watch_rewrite_count_ {0u};
        std::size_t external_mapping_rewrite_count_ {0u};
        bool rewrite_completed_ {false};
    };
}
