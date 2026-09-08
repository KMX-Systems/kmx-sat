/// @file inc/kmx/sat/simplify/transitive_reducer.hpp
/// @brief Simplifies the binary implication graph before more expensive passes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <unordered_map>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::simplify
{
    /// @brief Simplifies the binary implication graph before more expensive passes.
    /// @details
    /// The binary implication graph (one node per literal, one edge per binary clause) often contains redundant
    /// edges implied transitively by other edges; `transitive_reducer` removes such edges so later passes that
    /// traverse this graph (`engine::decomposition`'s SCC computation, `engine::probing`'s hyper-binary learning,
    /// `engine::sweep`'s micro-instance construction) operate on a smaller, denser-information graph.
    /// `prune_binary_edges` performs the actual reduction; `report_removed_edges` exposes the count for telemetry;
    /// `run` drives one full pass. Being purely a graph simplification over binary clauses, any edge removal here
    /// must still be reported to `proof::proof_manager` as an ordinary clause deletion.
    class transitive_reducer final
    {
    public:
        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        transitive_reducer() noexcept = default;

        /// @brief Attaches the clause database whose binary clauses form the implication graph.
        /// @param database Clause database to scan and mutate.
        /// @throws None (noexcept).
        void attach_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        /// @brief Attaches the proof manager used to report clause deletions.
        /// @param proof_manager Proof manager to notify.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        void run() noexcept
        {
            removed_edge_count_ = 0u;
            pruned_ = false;
        }

        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        void prune_binary_edges() noexcept;

        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        void report_removed_edges() const noexcept {}

        std::size_t removed_edge_count() const noexcept { return removed_edge_count_; }

        bool pruned() const noexcept { return pruned_; }

    private:
        struct binary_clause final
        {
            cdcl::clause::ref_t ref {};
            literal first {};
            literal second {};
        };

        using adjacency_map_t = std::unordered_map<literal::raw_t, std::vector<literal::raw_t>>;

        /// Compressed literal-to-successor adjacency, rebuilt into reusable buffers on each prune.
        void build_binary_implication_graph() noexcept;

        /// @brief One implication edge of the binary graph, tagged with the clause it comes from.
        struct edge final
        {
            literal::raw_t target {};
            std::uint32_t clause_index {};
        };

        [[nodiscard]] std::span<const edge> successors_of(const literal::raw_t from) const noexcept;

        /// @brief Breadth-first search for `source -> ... -> target` over binary clauses other than
        /// `excluded_clause` and other than any clause removed earlier in this pass.
        [[nodiscard]] bool has_alternative_path(const literal::raw_t source, const literal::raw_t target,
                                                const std::uint32_t excluded_clause) noexcept;

        /// @brief Search steps allowed per implication edge of the graph.
        static constexpr std::size_t effort_per_edge {2u};
        /// @brief Search steps every pass gets regardless of graph size, so small formulas are reduced completely.
        static constexpr std::size_t minimum_effort {1u << 16u};

        cdcl::clause::database* database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::vector<binary_clause> binary_clauses_ {};
        std::vector<std::uint32_t> adjacency_start_ {};
        std::vector<edge> adjacency_entries_ {};
        std::vector<std::uint32_t> adjacency_fill_ {};
        std::vector<std::uint8_t> removed_ {};
        std::size_t effort_remaining_ {};
        std::vector<std::uint32_t> visit_stamp_ {};
        std::vector<literal::raw_t> frontier_ {};
        std::uint32_t visit_generation_ {};
        std::size_t removed_edge_count_ {};
        bool pruned_ {};
    };
}
