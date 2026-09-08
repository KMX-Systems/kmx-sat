/// @file inc/kmx/sat/simplify/engine/decomposition.hpp
/// @brief SCC/ELS and decomposition in the CaDiCaL/Kissat style.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <unordered_map>
    #include <unordered_set>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/simplify/equivalence_substitutor.hpp>

namespace kmx::sat::simplify::engine
{
    /// @brief Pairs of variable indices forming the edges of the implication graph.
    using index_pair_list_t = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

    /// @brief Read-only view over implication-graph edges.
    using index_pair_span_t = std::span<const std::pair<std::uint32_t, std::uint32_t>>;

    /// @brief Adjacency lists of the implication graph, keyed by variable index.
    using adjacency_map_t = std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>;

    /// @brief Strongly connected components, each a list of variable indices.
    using component_list_t = std::vector<std::vector<std::uint32_t>>;

    /// @brief SCC/ELS and decomposition in the CaDiCaL/Kissat style.
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

        struct graph_metrics final
        {
            std::size_t node_count {};
            std::size_t unique_implication_edge_count {};
            std::size_t implication_attempt_count {};
            std::size_t rejected_zero_implication_count {};
            std::size_t accepted_implication_count {};
            std::size_t dropped_duplicate_edge_count {};
            std::size_t average_outgoing_implication_per_node_floor {};
            std::size_t edge_density_per_node_per_mille {};
            bool has_substitutor {};
        };

        struct scc_metrics final
        {
            std::size_t scc_run_count {};
            std::size_t scc_empty_run_count {};
            std::size_t component_count {};
            std::size_t singleton_component_count {};
            std::size_t non_singleton_component_count {};
            std::size_t max_component_size {};
            std::size_t average_component_size_floor {};
            std::size_t tarjan_visit_count {};
            std::size_t max_tarjan_stack_depth {};
            std::size_t successor_scan_count {};
            std::size_t tree_edge_relaxation_count {};
            std::size_t back_edge_relaxation_count {};
            std::size_t root_component_extraction_count {};
            std::size_t canonicalized_successor_list_count {};
            std::size_t canonicalized_successor_edge_count {};
            std::size_t successor_scans_per_unique_edge_per_mille {};
        };

        struct equivalence_metrics final
        {
            std::size_t find_equivalence_run_count {};
            std::size_t find_equivalence_empty_component_run_count {};
            std::size_t equivalence_count {};
            std::size_t substitution_count {};
            std::size_t generated_substitution_count {};
            std::size_t non_singleton_equivalence_component_count {};
            std::size_t max_substitutions_per_component {};
            std::size_t equivalence_coverage_per_node_per_mille {};
            std::size_t substitution_density_per_node_per_mille {};
        };

        struct emit_metrics final
        {
            std::size_t emit_run_count {};
            std::size_t emit_skip_count {};
            std::size_t emit_without_substitutor_count {};
            std::size_t total_emitted_substitution_count {};
            std::size_t last_emitted_substitution_count {};
            bool substitutions_emitted {};
        };

        struct consistency_metrics final
        {
            bool implication_accounting_consistent {};
            bool component_accounting_consistent {};
            bool substitution_accounting_consistent {};
            bool emit_accounting_consistent {};
            bool reset_accounting_consistent {};
            bool overall_consistent {};
        };

        struct reset_metrics final
        {
            std::size_t reset_graph_invocation_count {};
            std::size_t reset_run_invocation_count {};
            std::size_t reset_emit_invocation_count {};
            std::size_t reset_telemetry_invocation_count {};
            std::size_t clear_graph_invocation_count {};
            std::size_t total_reset_invocation_count {};
        };

        struct reset_epoch_metrics final
        {
            std::size_t reset_timeline_tick {};
            std::size_t last_reset_graph_tick {};
            std::size_t last_reset_run_tick {};
            std::size_t last_reset_emit_tick {};
            std::size_t last_reset_telemetry_tick {};
            std::size_t last_clear_graph_tick {};
        };

        struct reset_metrics_delta final
        {
            std::size_t reset_graph_invocation_count {};
            std::size_t reset_run_invocation_count {};
            std::size_t reset_emit_invocation_count {};
            std::size_t reset_telemetry_invocation_count {};
            std::size_t clear_graph_invocation_count {};
            std::size_t total_reset_invocation_count {};
        };

        /// @brief Attaches an external substitutor to receive discovered literal substitutions.
        /// @param substitutor Receiver for emitted equivalence mappings.
        /// @throws None (noexcept).
        void attach_substitutor(equivalence_substitutor& substitutor) noexcept { substitutor_ = &substitutor; }

        bool has_substitutor() const noexcept { return substitutor_ != nullptr; }

        void reset_graph_metrics() noexcept;

        void reset_run_metrics() noexcept;

        void reset_emit_metrics() noexcept;

        void reset_telemetry_metrics() noexcept;

        /// @brief Adds an implication edge in the binary implication graph.
        /// @param from Source literal/raw identifier.
        /// @param to Destination literal/raw identifier.
        /// @throws None (noexcept).
        void add_implication(const std::uint32_t from, const std::uint32_t to) noexcept;

        /// @brief Clears all implication graph state and extracted results.
        /// @throws None (noexcept).
        void clear_graph() noexcept;

        /// @brief Computes the strongly connected component decomposition of the binary implication graph.
        /// @throws None (noexcept).
        void run_scc() noexcept;

        /// @brief Extracts an equivalent-literal-substitution mapping from the computed components.
        /// @throws None (noexcept).
        void find_equivalences() noexcept;

        /// @brief Hands the discovered equivalences to `equivalence_substitutor` for solver-wide rewriting.
        /// @throws None (noexcept).
        void emit_substitutions() noexcept;

        std::size_t node_count() const noexcept { return nodes_.size(); }

        std::size_t component_count() const noexcept { return component_count_; }

        std::size_t equivalence_count() const noexcept { return equivalence_count_; }

        bool substitutions_emitted() const noexcept { return substitutions_emitted_; }

        std::size_t substitution_count() const noexcept { return substitutions_.size(); }

        std::size_t unique_implication_edge_count() const noexcept { return implication_edges_.size(); }

        std::size_t outgoing_implication_count(const std::uint32_t node) const noexcept;

        bool has_implication(const std::uint32_t from, const std::uint32_t to) const noexcept
        {
            return implication_edges_.contains(compose_edge_key(from, to));
        }

        std::size_t dropped_duplicate_edge_count() const noexcept { return dropped_duplicate_edge_count_; }

        std::size_t implication_attempt_count() const noexcept { return implication_attempt_count_; }

        std::size_t rejected_zero_implication_count() const noexcept { return rejected_zero_implication_count_; }

        std::size_t accepted_implication_count() const noexcept
        {
            return implication_attempt_count_ - rejected_zero_implication_count_ - dropped_duplicate_edge_count_;
        }

        std::size_t scc_run_count() const noexcept { return scc_run_count_; }

        std::size_t scc_empty_run_count() const noexcept { return scc_empty_run_count_; }

        std::size_t singleton_component_count() const noexcept { return singleton_component_count_; }

        std::size_t non_singleton_component_count() const noexcept { return non_singleton_component_count_; }

        std::size_t tarjan_visit_count() const noexcept { return tarjan_visit_count_; }

        std::size_t max_tarjan_stack_depth() const noexcept { return max_tarjan_stack_depth_; }

        std::size_t max_component_size() const noexcept { return max_component_size_; }

        std::size_t average_outgoing_implication_per_node_floor() const noexcept;

        std::size_t average_component_size_floor() const noexcept;

        std::size_t equivalence_coverage_per_node_per_mille() const noexcept;

        std::size_t substitution_density_per_node_per_mille() const noexcept;

        std::size_t successor_scans_per_unique_edge_per_mille() const noexcept;

        std::size_t successor_scan_count() const noexcept { return successor_scan_count_; }

        std::size_t tree_edge_relaxation_count() const noexcept { return tree_edge_relaxation_count_; }

        std::size_t back_edge_relaxation_count() const noexcept { return back_edge_relaxation_count_; }

        std::size_t root_component_extraction_count() const noexcept { return root_component_extraction_count_; }

        std::size_t find_equivalence_run_count() const noexcept { return find_equivalence_run_count_; }

        std::size_t find_equivalence_empty_component_run_count() const noexcept { return find_equivalence_empty_component_run_count_; }

        std::size_t generated_substitution_count() const noexcept { return generated_substitution_count_; }

        std::size_t non_singleton_equivalence_component_count() const noexcept { return non_singleton_equivalence_component_count_; }

        std::size_t max_substitutions_per_component() const noexcept { return max_substitutions_per_component_; }

        std::size_t emit_run_count() const noexcept { return emit_run_count_; }

        std::size_t emit_skip_count() const noexcept { return emit_skip_count_; }

        std::size_t emit_without_substitutor_count() const noexcept { return emit_without_substitutor_count_; }

        std::size_t total_emitted_substitution_count() const noexcept { return total_emitted_substitution_count_; }

        std::size_t last_emitted_substitution_count() const noexcept { return last_emitted_substitution_count_; }

        std::size_t canonicalized_successor_list_count() const noexcept { return canonicalized_successor_list_count_; }

        std::size_t canonicalized_successor_edge_count() const noexcept { return canonicalized_successor_edge_count_; }

        std::size_t edge_density_per_node_per_mille() const noexcept;

        graph_metrics graph_metrics_snapshot() const noexcept;

        scc_metrics scc_metrics_snapshot() const noexcept;

        equivalence_metrics equivalence_metrics_snapshot() const noexcept;

        emit_metrics emit_metrics_snapshot() const noexcept;

        reset_metrics reset_metrics_snapshot() const noexcept;

        reset_epoch_metrics reset_epoch_metrics_snapshot() const noexcept;

        static reset_metrics_delta reset_metrics_delta_between(const reset_metrics& before, const reset_metrics& after) noexcept;

        static bool reset_metrics_monotonic(const reset_metrics& before, const reset_metrics& after) noexcept;

        consistency_metrics consistency_metrics_snapshot() const noexcept;

        bool telemetry_consistent() const noexcept { return consistency_metrics_snapshot().overall_consistent; }

        index_pair_span_t substitutions() const noexcept { return substitutions_; }

    private:
        static std::uint64_t compose_edge_key(const std::uint32_t from, const std::uint32_t to) noexcept
        {
            return (static_cast<std::uint64_t>(from) << 32u) | static_cast<std::uint64_t>(to);
        }

        void canonicalize_adjacency() noexcept;

        void canonicalize_components() noexcept;

        void strong_connect(const std::uint32_t node) noexcept;

        void update_component_metrics() noexcept;

        adjacency_map_t adjacency_ {};
        std::unordered_set<std::uint64_t> implication_edges_ {};
        std::unordered_set<std::uint32_t> nodes_ {};
        component_list_t components_ {};
        index_pair_list_t substitutions_ {};
        equivalence_substitutor* substitutor_ {};

        std::unordered_map<std::uint32_t, std::size_t> index_ {};
        std::unordered_map<std::uint32_t, std::size_t> low_link_ {};
        std::unordered_set<std::uint32_t> on_stack_ {};
        std::vector<std::uint32_t> tarjan_stack_ {};
        std::size_t next_index_ {};

        std::size_t component_count_ {};
        std::size_t equivalence_count_ {};
        std::size_t dropped_duplicate_edge_count_ {};
        std::size_t implication_attempt_count_ {};
        std::size_t rejected_zero_implication_count_ {};
        std::size_t scc_run_count_ {};
        std::size_t scc_empty_run_count_ {};
        std::size_t singleton_component_count_ {};
        std::size_t non_singleton_component_count_ {};
        std::size_t tarjan_visit_count_ {};
        std::size_t max_tarjan_stack_depth_ {};
        std::size_t max_component_size_ {};
        std::size_t successor_scan_count_ {};
        std::size_t tree_edge_relaxation_count_ {};
        std::size_t back_edge_relaxation_count_ {};
        std::size_t root_component_extraction_count_ {};
        std::size_t find_equivalence_run_count_ {};
        std::size_t find_equivalence_empty_component_run_count_ {};
        std::size_t generated_substitution_count_ {};
        std::size_t non_singleton_equivalence_component_count_ {};
        std::size_t max_substitutions_per_component_ {};
        std::size_t emit_run_count_ {};
        std::size_t emit_skip_count_ {};
        std::size_t emit_without_substitutor_count_ {};
        std::size_t total_emitted_substitution_count_ {};
        std::size_t last_emitted_substitution_count_ {};
        std::size_t canonicalized_successor_list_count_ {};
        std::size_t canonicalized_successor_edge_count_ {};
        std::size_t reset_graph_invocation_count_ {};
        std::size_t reset_run_invocation_count_ {};
        std::size_t reset_emit_invocation_count_ {};
        std::size_t reset_telemetry_invocation_count_ {};
        std::size_t clear_graph_invocation_count_ {};
        std::size_t reset_timeline_tick_ {};
        std::size_t last_reset_graph_tick_ {};
        std::size_t last_reset_run_tick_ {};
        std::size_t last_reset_emit_tick_ {};
        std::size_t last_reset_telemetry_tick_ {};
        std::size_t last_clear_graph_tick_ {};
        bool substitutions_emitted_ {};
    };
}
