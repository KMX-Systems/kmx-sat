/// @file inc/kmx/sat/simplify/equivalence_substitutor.hpp
/// @brief Propagates ELS results across all subsystems.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <unordered_map>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{
    /// @brief Pairs of variable indices linked by a discovered equivalence.
    using index_pair_list_t = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

    /// @brief Propagates ELS results across all subsystems.
    /// @details
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

        /// @brief Attaches clause database storage to receive in-place literal substitutions.
        /// @param database Clause database to rewrite.
        /// @throws None (noexcept).
        void attach_clause_database(cdcl::clause::database& database) noexcept { clause_database_ = &database; }

        /// @brief Attaches watch-list storage to receive literal partition and metadata substitutions.
        /// @param watch_list Watch-list bank to rewrite.
        /// @throws None (noexcept).
        void attach_watch_list(cdcl::bank::watch_list& watch_list) noexcept { watch_list_ = &watch_list; }

        /// @brief Attaches variable mapper to propagate substitutions into external/internal mappings.
        /// @param mapper Variable mapper to rewrite.
        /// @throws None (noexcept).
        void attach_variable_mapper(cdcl::variable_mapper& mapper) noexcept { variable_mapper_ = &mapper; }

        /// @brief Records the representative-literal mapping for one discovered equivalence class.
        /// @throws None (noexcept).
        void apply_equivalence_class() noexcept { apply_equivalence_class(equivalence_class_count_ + 1u, equivalence_class_count_ + 2u); }

        /// @brief Records a representative mapping from one literal/raw id to another.
        /// @param from Raw identifier being replaced.
        /// @param to Raw identifier chosen as representative.
        /// @throws None (noexcept).
        void apply_equivalence_class(const std::uint32_t from, const std::uint32_t to) noexcept;

        /// @brief Rewrites clause storage to use each equivalence class's representative literal.
        /// @throws None (noexcept).
        void rewrite_clauses() noexcept;

        /// @brief Rewrites watch-list entries to use each equivalence class's representative literal.
        /// @throws None (noexcept).
        void rewrite_watches() noexcept;

        /// @brief Propagates the substitution into the external variable mapping.
        /// @throws None (noexcept).
        void rewrite_external_mapping() noexcept;

        std::size_t equivalence_class_count() const noexcept { return equivalence_class_count_; }

        std::size_t clause_rewrite_count() const noexcept { return clause_rewrite_count_; }

        std::size_t watch_rewrite_count() const noexcept { return watch_rewrite_count_; }

        std::size_t external_mapping_rewrite_count() const noexcept { return external_mapping_rewrite_count_; }

        bool rewrite_completed() const noexcept { return rewrite_completed_; }

        std::size_t pending_rewrite_count() const noexcept { return pending_rewrites_.size(); }

        std::size_t last_applied_rewrite_count() const noexcept { return last_applied_rewrite_count_; }

    private:
        std::uint32_t resolve_representative(std::uint32_t value) const noexcept;

        literal remap_literal(const literal old_literal) const noexcept;

        void rewrite_clause_set(const std::span<const cdcl::clause::ref_t> refs) noexcept;

        cdcl::clause::database* clause_database_ {};
        cdcl::bank::watch_list* watch_list_ {};
        cdcl::variable_mapper* variable_mapper_ {};
        index_pair_list_t pending_rewrites_ {};
        std::unordered_map<std::uint32_t, std::uint32_t> rewrite_map_ {};
        std::size_t equivalence_class_count_ {};
        std::size_t clause_rewrite_count_ {};
        std::size_t watch_rewrite_count_ {};
        std::size_t external_mapping_rewrite_count_ {};
        std::size_t last_applied_rewrite_count_ {};
        bool rewrite_completed_ {};
    };
}
