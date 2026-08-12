/// @file inc/kmx/sat/simplify/engine/decomposition.hpp
/// @brief SCC/ELS and decomposition in the CaDiCaL/Kissat style.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <unordered_map>
    #include <unordered_set>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/simplify/equivalence_substitutor.hpp>

namespace kmx::sat::simplify::engine
{
    /// @brief SCC/ELS and decomposition in the CaDiCaL/Kissat style.
    ///
    /// @details
    /// `engine::decomposition` finds Equivalent Literal Substitution (ELS) opportunities by computing Strongly
    /// Connected Components (SCC, via Tarjan's algorithm) over the binary implication graph: two literals in the
    /// same SCC must have the same truth value in every model, so they can be merged into one representative.
    /// `run_scc` computes the component decomposition, `find_equivalences` extracts the representative-literal
    /// mapping from it, and `emit_substitutions` hands that mapping to `equivalence_substitutor` to rewrite clauses,
    /// watches, and the external mapping consistently across the solver.
    /// @reference Tarjan's strongly connected components algorithm, applied to the binary implication graph for
    /// equivalent literal detection (as in CaDiCaL's decompose/ELS pass).
    class decomposition final
    {
    public:
        /// @brief Constructs a decomposition engine with no computed components.
        /// @throws None (noexcept).
        decomposition() noexcept = default;

        /// @brief Attaches an external substitutor to receive discovered literal substitutions.
        /// @param substitutor Receiver for emitted equivalence mappings.
        /// @throws None (noexcept).
        void attach_substitutor(equivalence_substitutor& substitutor) noexcept { substitutor_ = &substitutor; }

        /// @brief Adds an implication edge in the binary implication graph.
        /// @param from Source literal/raw identifier.
        /// @param to Destination literal/raw identifier.
        /// @throws None (noexcept).
        void add_implication(const std::uint32_t from, const std::uint32_t to) noexcept
        {
            if (from == 0u || to == 0u)
            {
                return;
            }

            adjacency_[from].push_back(to);
            nodes_.insert(from);
            nodes_.insert(to);
        }

        /// @brief Clears all implication graph state and extracted results.
        /// @throws None (noexcept).
        void clear_graph() noexcept
        {
            adjacency_.clear();
            nodes_.clear();
            components_.clear();
            substitutions_.clear();
            component_count_ = 0u;
            equivalence_count_ = 0u;
            substitutions_emitted_ = false;
        }

        /// @brief Computes the strongly connected component decomposition of the binary implication graph.
        /// @throws None (noexcept).
        void run_scc() noexcept
        {
            components_.clear();
            substitutions_.clear();
            substitutions_emitted_ = false;

            if (nodes_.empty())
            {
                component_count_ = 0u;
                equivalence_count_ = 0u;
                return;
            }

            index_.clear();
            low_link_.clear();
            on_stack_.clear();
            tarjan_stack_.clear();
            next_index_ = 0;

            for (const auto node: nodes_)
            {
                if (index_.contains(node))
                {
                    continue;
                }
                strong_connect(node);
            }

            component_count_ = components_.size();
        }

        /// @brief Extracts an equivalent-literal-substitution mapping from the computed components.
        /// @throws None (noexcept).
        void find_equivalences() noexcept
        {
            substitutions_.clear();
            equivalence_count_ = 0u;

            for (const auto& component: components_)
            {
                if (component.size() < 2u)
                {
                    continue;
                }

                const auto representative = *std::min_element(component.begin(), component.end());
                for (const auto node: component)
                {
                    if (node == representative)
                    {
                        continue;
                    }
                    substitutions_.push_back({node, representative});
                    ++equivalence_count_;
                }
            }
        }

        /// @brief Hands the discovered equivalences to `equivalence_substitutor` for solver-wide rewriting.
        /// @throws None (noexcept).
        void emit_substitutions() noexcept
        {
            if (substitutor_ != nullptr)
            {
                for (const auto& [from, to]: substitutions_)
                {
                    substitutor_->apply_equivalence_class(from, to);
                }
            }
            substitutions_emitted_ = true;
        }

        std::size_t node_count() const noexcept { return nodes_.size(); }

        std::size_t component_count() const noexcept { return component_count_; }

        std::size_t equivalence_count() const noexcept { return equivalence_count_; }

        bool substitutions_emitted() const noexcept { return substitutions_emitted_; }

        std::size_t substitution_count() const noexcept { return substitutions_.size(); }

    private:
        void strong_connect(const std::uint32_t node) noexcept
        {
            index_[node] = next_index_;
            low_link_[node] = next_index_;
            ++next_index_;
            tarjan_stack_.push_back(node);
            on_stack_.insert(node);

            const auto it = adjacency_.find(node);
            if (it != adjacency_.end())
            {
                for (const auto successor: it->second)
                {
                    if (!index_.contains(successor))
                    {
                        strong_connect(successor);
                        low_link_[node] = std::min(low_link_[node], low_link_[successor]);
                    }
                    else if (on_stack_.contains(successor))
                    {
                        low_link_[node] = std::min(low_link_[node], index_[successor]);
                    }
                }
            }

            if (low_link_[node] != index_[node])
            {
                return;
            }

            std::vector<std::uint32_t> component {};
            while (!tarjan_stack_.empty())
            {
                const auto top = tarjan_stack_.back();
                tarjan_stack_.pop_back();
                on_stack_.erase(top);
                component.push_back(top);
                if (top == node)
                {
                    break;
                }
            }
            components_.push_back(std::move(component));
        }

        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> adjacency_ {};
        std::unordered_set<std::uint32_t> nodes_ {};
        std::vector<std::vector<std::uint32_t>> components_ {};
        std::vector<std::pair<std::uint32_t, std::uint32_t>> substitutions_ {};
        equivalence_substitutor* substitutor_ {nullptr};

        std::unordered_map<std::uint32_t, std::size_t> index_ {};
        std::unordered_map<std::uint32_t, std::size_t> low_link_ {};
        std::unordered_set<std::uint32_t> on_stack_ {};
        std::vector<std::uint32_t> tarjan_stack_ {};
        std::size_t next_index_ {0u};

        std::size_t component_count_ {0u};
        std::size_t equivalence_count_ {0u};
        bool substitutions_emitted_ {false};
    };
}
