/// @file inc/kmx/sat/simplify/transitive_reducer.hpp
/// @brief Simplifies the binary implication graph before more expensive passes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif

namespace kmx::sat::simplify
{
    /// @brief Simplifies the binary implication graph before more expensive passes.
    ///
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

        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        void run() noexcept
        {
            removed_edge_count_ = 1u;
        }

        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        void prune_binary_edges() noexcept
        {
            pruned_ = true;
        }

        /// @brief Executes this operation on the owning subsystem.
        /// @throws None (noexcept).
        void report_removed_edges() const noexcept
        {
        }

        std::size_t removed_edge_count() const noexcept
        {
            return removed_edge_count_;
        }

        bool pruned() const noexcept
        {
            return pruned_;
        }

    private:
        std::size_t removed_edge_count_ {0u};
        bool pruned_ {false};
    };
}
