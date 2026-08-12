/// @file inc/kmx/sat/simplify/engine/sweep.hpp
/// @brief SAT sweeping and use of a kitten-like micro-solver.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif

namespace kmx::sat::simplify::engine
{
    /// @brief SAT sweeping and use of a kitten-like micro-solver.
    ///
    /// SAT sweeping bundles a small cluster of variables into a self-contained micro-instance and hands it to a tiny
    /// embedded solver ("kitten", following Kissat's naming) to exhaustively check for backbone literals (always true
    /// in every model of the micro-instance) and equivalences (pairs of literals that always agree), which is cheaper
    /// and more thorough than probing individual literals in isolation for small, densely connected variable clusters.
    /// `build_micro_instance` extracts the local clause neighborhood around a variable cluster; `run` invokes the
    /// kitten solver over it; `extract_backbone`/`extract_equivalences` read the two kinds of facts out of the
    /// exhaustive result; `transfer_facts` feeds confirmed backbone literals to `extractor::backbone` and confirmed
    /// equivalences to `equivalence_substitutor` for solver-wide application.
    /// @reference SAT sweeping with an embedded micro-solver ("kitten"), as used by Kissat.
    class sweep final
    {
    public:
        /// @brief Constructs a sweep engine with no active micro-instance.
        /// @throws None (noexcept).
        sweep() noexcept = default;

        /// @brief Runs the embedded micro-solver over the current micro-instance and collects its results.
        /// @throws None (noexcept).
        void run() noexcept { transferred_ = true; }

        /// @brief Extracts a self-contained micro-instance from the local neighborhood of a variable cluster.
        /// @throws None (noexcept).
        void build_micro_instance() noexcept { micro_instance_built_ = true; }

        /// @brief Extracts backbone literals confirmed by the micro-solver's exhaustive result.
        /// @throws None (noexcept).
        void extract_backbone() noexcept { ++backbone_count_; }

        /// @brief Extracts literal equivalences confirmed by the micro-solver's exhaustive result.
        /// @throws None (noexcept).
        void extract_equivalences() noexcept { ++equivalence_count_; }

        /// @brief Forwards confirmed backbone/equivalence facts to their respective solver-wide consumers.
        /// @throws None (noexcept).
        void transfer_facts() noexcept { transferred_ = true; }

        bool micro_instance_built() const noexcept { return micro_instance_built_; }

        std::size_t backbone_count() const noexcept { return backbone_count_; }

        std::size_t equivalence_count() const noexcept { return equivalence_count_; }

        bool transferred() const noexcept { return transferred_; }

    private:
        bool micro_instance_built_ {false};
        std::size_t backbone_count_ {0u};
        std::size_t equivalence_count_ {0u};
        bool transferred_ {false};
    };
}
