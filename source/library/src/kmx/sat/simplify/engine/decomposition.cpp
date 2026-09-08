/// @file library/src/kmx/sat/simplify/engine/decomposition.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/engine/decomposition.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/engine/decomposition.hpp>

namespace kmx::sat::simplify::engine
{
    void decomposition::reset_graph_metrics() noexcept
    {
        ++reset_timeline_tick_;
        last_reset_graph_tick_ = reset_timeline_tick_;
        ++reset_graph_invocation_count_;
        implication_attempt_count_ = 0u;
        rejected_zero_implication_count_ = 0u;
        dropped_duplicate_edge_count_ = 0u;
    }

    void decomposition::reset_run_metrics() noexcept
    {
        ++reset_timeline_tick_;
        last_reset_run_tick_ = reset_timeline_tick_;
        ++reset_run_invocation_count_;
        components_.clear();
        substitutions_.clear();
        component_count_ = 0u;
        equivalence_count_ = 0u;

        scc_run_count_ = 0u;
        scc_empty_run_count_ = 0u;
        singleton_component_count_ = 0u;
        non_singleton_component_count_ = 0u;
        tarjan_visit_count_ = 0u;
        max_tarjan_stack_depth_ = 0u;
        max_component_size_ = 0u;
        successor_scan_count_ = 0u;
        tree_edge_relaxation_count_ = 0u;
        back_edge_relaxation_count_ = 0u;
        root_component_extraction_count_ = 0u;
        find_equivalence_run_count_ = 0u;
        find_equivalence_empty_component_run_count_ = 0u;
        generated_substitution_count_ = 0u;
        non_singleton_equivalence_component_count_ = 0u;
        max_substitutions_per_component_ = 0u;
        canonicalized_successor_list_count_ = 0u;
        canonicalized_successor_edge_count_ = 0u;

        index_.clear();
        low_link_.clear();
        on_stack_.clear();
        tarjan_stack_.clear();
        next_index_ = 0u;

        substitutions_emitted_ = false;
    }

    void decomposition::reset_emit_metrics() noexcept
    {
        ++reset_timeline_tick_;
        last_reset_emit_tick_ = reset_timeline_tick_;
        ++reset_emit_invocation_count_;
        emit_run_count_ = 0u;
        emit_skip_count_ = 0u;
        emit_without_substitutor_count_ = 0u;
        total_emitted_substitution_count_ = 0u;
        last_emitted_substitution_count_ = 0u;
        substitutions_emitted_ = false;
    }

    void decomposition::reset_telemetry_metrics() noexcept
    {
        ++reset_timeline_tick_;
        last_reset_telemetry_tick_ = reset_timeline_tick_;
        ++reset_telemetry_invocation_count_;
        reset_graph_metrics();
        reset_run_metrics();
        reset_emit_metrics();
    }

    void decomposition::add_implication(const std::uint32_t from, const std::uint32_t to) noexcept
    {
        ++implication_attempt_count_;

        if ((from == 0u) || (to == 0u))
        {
            ++rejected_zero_implication_count_;
            return;
        }

        const auto edge_key = compose_edge_key(from, to);
        if (!implication_edges_.insert(edge_key).second)
        {
            ++dropped_duplicate_edge_count_;
            return;
        }

        adjacency_[from].push_back(to);
        nodes_.insert(from);
        nodes_.insert(to);
    }

    void decomposition::clear_graph() noexcept
    {
        ++reset_timeline_tick_;
        last_clear_graph_tick_ = reset_timeline_tick_;
        ++clear_graph_invocation_count_;
        adjacency_.clear();
        implication_edges_.clear();
        nodes_.clear();
        reset_telemetry_metrics();
    }

    void decomposition::run_scc() noexcept
    {
        components_.clear();
        substitutions_.clear();
        substitutions_emitted_ = false;
        ++scc_run_count_;
        max_component_size_ = 0u;
        singleton_component_count_ = 0u;
        non_singleton_component_count_ = 0u;
        tarjan_visit_count_ = 0u;
        max_tarjan_stack_depth_ = 0u;
        successor_scan_count_ = 0u;
        tree_edge_relaxation_count_ = 0u;
        back_edge_relaxation_count_ = 0u;
        root_component_extraction_count_ = 0u;
        canonicalized_successor_list_count_ = 0u;
        canonicalized_successor_edge_count_ = 0u;

        if (nodes_.empty())
        {
            ++scc_empty_run_count_;
            component_count_ = 0u;
            equivalence_count_ = 0u;
            return;
        }

        index_.clear();
        low_link_.clear();
        on_stack_.clear();
        tarjan_stack_.clear();
        next_index_ = 0u;

        canonicalize_adjacency();

        std::vector<std::uint32_t> ordered_nodes {nodes_.begin(), nodes_.end()};
        std::sort(ordered_nodes.begin(), ordered_nodes.end());

        for (const auto node: ordered_nodes)
        {
            if (index_.contains(node))
                continue;
            strong_connect(node);
        }

        canonicalize_components();
        update_component_metrics();

        component_count_ = components_.size();
    }

    void decomposition::find_equivalences() noexcept
    {
        ++find_equivalence_run_count_;
        substitutions_.clear();
        equivalence_count_ = 0u;
        generated_substitution_count_ = 0u;
        non_singleton_equivalence_component_count_ = 0u;
        max_substitutions_per_component_ = 0u;

        if (components_.empty())
            ++find_equivalence_empty_component_run_count_;

        for (const auto& component: components_)
        {
            if (component.size() < 2u)
                continue;

            ++non_singleton_equivalence_component_count_;
            const auto substitutions_before_component = substitutions_.size();

            const auto representative = *std::min_element(component.begin(), component.end());
            for (const auto node: component)
            {
                if (node == representative)
                    continue;
                substitutions_.push_back({node, representative});
                ++equivalence_count_;
            }

            const auto substitutions_for_component = substitutions_.size() - substitutions_before_component;
            if (substitutions_for_component > max_substitutions_per_component_)
                max_substitutions_per_component_ = substitutions_for_component;
        }

        generated_substitution_count_ = substitutions_.size();

        std::sort(substitutions_.begin(), substitutions_.end(),
                  [](const std::pair<std::uint32_t, std::uint32_t>& left, const std::pair<std::uint32_t, std::uint32_t>& right) noexcept
                  {
                      if (left.second != right.second)
                          return left.second < right.second;
                      return left.first < right.first;
                  });
    }

    void decomposition::emit_substitutions() noexcept
    {
        if (substitutions_emitted_)
        {
            ++emit_skip_count_;
            return;
        }

        ++emit_run_count_;
        last_emitted_substitution_count_ = substitutions_.size();
        total_emitted_substitution_count_ += substitutions_.size();

        if (substitutor_ != nullptr)
            for (const auto& [from, to]: substitutions_)
                substitutor_->apply_equivalence_class(from, to);
        else
            ++emit_without_substitutor_count_;
        substitutions_emitted_ = true;
    }

    std::size_t decomposition::outgoing_implication_count(const std::uint32_t node) const noexcept
    {
        const auto it = adjacency_.find(node);
        if (it == adjacency_.end())
            return 0u;
        return it->second.size();
    }

    std::size_t decomposition::average_outgoing_implication_per_node_floor() const noexcept
    {
        if (node_count() == 0u)
            return 0u;
        return unique_implication_edge_count() / node_count();
    }

    std::size_t decomposition::average_component_size_floor() const noexcept
    {
        if (component_count_ == 0u)
            return 0u;
        return node_count() / component_count_;
    }

    std::size_t decomposition::equivalence_coverage_per_node_per_mille() const noexcept
    {
        if (node_count() == 0u)
            return 0u;
        return (equivalence_count_ * 1000u) / node_count();
    }

    std::size_t decomposition::substitution_density_per_node_per_mille() const noexcept
    {
        if (node_count() == 0u)
            return 0u;
        return (generated_substitution_count_ * 1000u) / node_count();
    }

    std::size_t decomposition::successor_scans_per_unique_edge_per_mille() const noexcept
    {
        if (unique_implication_edge_count() == 0u)
            return 0u;
        return (successor_scan_count_ * 1000u) / unique_implication_edge_count();
    }

    std::size_t decomposition::edge_density_per_node_per_mille() const noexcept
    {
        if (node_count() == 0u)
            return 0u;
        return (unique_implication_edge_count() * 1000u) / node_count();
    }

    decomposition::graph_metrics decomposition::graph_metrics_snapshot() const noexcept
    {
        return graph_metrics {
            .node_count = node_count(),
            .unique_implication_edge_count = unique_implication_edge_count(),
            .implication_attempt_count = implication_attempt_count(),
            .rejected_zero_implication_count = rejected_zero_implication_count(),
            .accepted_implication_count = accepted_implication_count(),
            .dropped_duplicate_edge_count = dropped_duplicate_edge_count(),
            .average_outgoing_implication_per_node_floor = average_outgoing_implication_per_node_floor(),
            .edge_density_per_node_per_mille = edge_density_per_node_per_mille(),
            .has_substitutor = has_substitutor(),
        };
    }

    decomposition::scc_metrics decomposition::scc_metrics_snapshot() const noexcept
    {
        return scc_metrics {
            .scc_run_count = scc_run_count(),
            .scc_empty_run_count = scc_empty_run_count(),
            .component_count = component_count(),
            .singleton_component_count = singleton_component_count(),
            .non_singleton_component_count = non_singleton_component_count(),
            .max_component_size = max_component_size(),
            .average_component_size_floor = average_component_size_floor(),
            .tarjan_visit_count = tarjan_visit_count(),
            .max_tarjan_stack_depth = max_tarjan_stack_depth(),
            .successor_scan_count = successor_scan_count(),
            .tree_edge_relaxation_count = tree_edge_relaxation_count(),
            .back_edge_relaxation_count = back_edge_relaxation_count(),
            .root_component_extraction_count = root_component_extraction_count(),
            .canonicalized_successor_list_count = canonicalized_successor_list_count(),
            .canonicalized_successor_edge_count = canonicalized_successor_edge_count(),
            .successor_scans_per_unique_edge_per_mille = successor_scans_per_unique_edge_per_mille(),
        };
    }

    decomposition::equivalence_metrics decomposition::equivalence_metrics_snapshot() const noexcept
    {
        return equivalence_metrics {
            .find_equivalence_run_count = find_equivalence_run_count(),
            .find_equivalence_empty_component_run_count = find_equivalence_empty_component_run_count(),
            .equivalence_count = equivalence_count(),
            .substitution_count = substitution_count(),
            .generated_substitution_count = generated_substitution_count(),
            .non_singleton_equivalence_component_count = non_singleton_equivalence_component_count(),
            .max_substitutions_per_component = max_substitutions_per_component(),
            .equivalence_coverage_per_node_per_mille = equivalence_coverage_per_node_per_mille(),
            .substitution_density_per_node_per_mille = substitution_density_per_node_per_mille(),
        };
    }

    decomposition::emit_metrics decomposition::emit_metrics_snapshot() const noexcept
    {
        return emit_metrics {
            .emit_run_count = emit_run_count(),
            .emit_skip_count = emit_skip_count(),
            .emit_without_substitutor_count = emit_without_substitutor_count(),
            .total_emitted_substitution_count = total_emitted_substitution_count(),
            .last_emitted_substitution_count = last_emitted_substitution_count(),
            .substitutions_emitted = substitutions_emitted(),
        };
    }

    decomposition::reset_metrics decomposition::reset_metrics_snapshot() const noexcept
    {
        return reset_metrics {
            .reset_graph_invocation_count = reset_graph_invocation_count_,
            .reset_run_invocation_count = reset_run_invocation_count_,
            .reset_emit_invocation_count = reset_emit_invocation_count_,
            .reset_telemetry_invocation_count = reset_telemetry_invocation_count_,
            .clear_graph_invocation_count = clear_graph_invocation_count_,
            .total_reset_invocation_count = reset_graph_invocation_count_ + reset_run_invocation_count_ + reset_emit_invocation_count_ +
                                            reset_telemetry_invocation_count_ + clear_graph_invocation_count_,
        };
    }

    decomposition::reset_epoch_metrics decomposition::reset_epoch_metrics_snapshot() const noexcept
    {
        return reset_epoch_metrics {
            .reset_timeline_tick = reset_timeline_tick_,
            .last_reset_graph_tick = last_reset_graph_tick_,
            .last_reset_run_tick = last_reset_run_tick_,
            .last_reset_emit_tick = last_reset_emit_tick_,
            .last_reset_telemetry_tick = last_reset_telemetry_tick_,
            .last_clear_graph_tick = last_clear_graph_tick_,
        };
    }

    decomposition::reset_metrics_delta decomposition::reset_metrics_delta_between(const reset_metrics& before,
                                                                                  const reset_metrics& after) noexcept
    {
        return reset_metrics_delta {
            .reset_graph_invocation_count = (after.reset_graph_invocation_count >= before.reset_graph_invocation_count) ?
                                                after.reset_graph_invocation_count - before.reset_graph_invocation_count :
                                                0u,
            .reset_run_invocation_count = (after.reset_run_invocation_count >= before.reset_run_invocation_count) ?
                                              after.reset_run_invocation_count - before.reset_run_invocation_count :
                                              0u,
            .reset_emit_invocation_count = (after.reset_emit_invocation_count >= before.reset_emit_invocation_count) ?
                                               after.reset_emit_invocation_count - before.reset_emit_invocation_count :
                                               0u,
            .reset_telemetry_invocation_count = (after.reset_telemetry_invocation_count >= before.reset_telemetry_invocation_count) ?
                                                    after.reset_telemetry_invocation_count - before.reset_telemetry_invocation_count :
                                                    0u,
            .clear_graph_invocation_count = (after.clear_graph_invocation_count >= before.clear_graph_invocation_count) ?
                                                after.clear_graph_invocation_count - before.clear_graph_invocation_count :
                                                0u,
            .total_reset_invocation_count = (after.total_reset_invocation_count >= before.total_reset_invocation_count) ?
                                                after.total_reset_invocation_count - before.total_reset_invocation_count :
                                                0u,
        };
    }

    bool decomposition::reset_metrics_monotonic(const reset_metrics& before, const reset_metrics& after) noexcept
    {
        return (after.reset_graph_invocation_count >= before.reset_graph_invocation_count) &&
               (after.reset_run_invocation_count >= before.reset_run_invocation_count) &&
               (after.reset_emit_invocation_count >= before.reset_emit_invocation_count) &&
               (after.reset_telemetry_invocation_count >= before.reset_telemetry_invocation_count) &&
               (after.clear_graph_invocation_count >= before.clear_graph_invocation_count) &&
               (after.total_reset_invocation_count >= before.total_reset_invocation_count);
    }

    decomposition::consistency_metrics decomposition::consistency_metrics_snapshot() const noexcept
    {
        const auto implication_accounting_consistent =
            accepted_implication_count() + rejected_zero_implication_count() + dropped_duplicate_edge_count() ==
            implication_attempt_count();

        const auto component_accounting_consistent = singleton_component_count() + non_singleton_component_count() == component_count();

        const auto substitution_accounting_consistent =
            (generated_substitution_count() == substitution_count()) && (equivalence_count() == substitution_count());

        const auto emit_accounting_consistent = (total_emitted_substitution_count() >= last_emitted_substitution_count()) &&
                                                ((emit_run_count() == 0u) ? total_emitted_substitution_count() == 0u : true);

        const auto reset_accounting_consistent = (reset_graph_invocation_count_ >= reset_telemetry_invocation_count_) &&
                                                 (reset_run_invocation_count_ >= reset_telemetry_invocation_count_) &&
                                                 (reset_emit_invocation_count_ >= reset_telemetry_invocation_count_) &&
                                                 (reset_telemetry_invocation_count_ >= clear_graph_invocation_count_);

        const auto overall_consistent = implication_accounting_consistent && component_accounting_consistent &&
                                        substitution_accounting_consistent && emit_accounting_consistent && reset_accounting_consistent;

        return consistency_metrics {
            .implication_accounting_consistent = implication_accounting_consistent,
            .component_accounting_consistent = component_accounting_consistent,
            .substitution_accounting_consistent = substitution_accounting_consistent,
            .emit_accounting_consistent = emit_accounting_consistent,
            .reset_accounting_consistent = reset_accounting_consistent,
            .overall_consistent = overall_consistent,
        };
    }

    void decomposition::canonicalize_adjacency() noexcept
    {
        for (auto& [_, successors]: adjacency_)
        {
            ++canonicalized_successor_list_count_;
            canonicalized_successor_edge_count_ += successors.size();
            std::sort(successors.begin(), successors.end());
        }
    }

    void decomposition::canonicalize_components() noexcept
    {
        for (auto& component: components_)
            std::sort(component.begin(), component.end());

        std::sort(components_.begin(), components_.end(),
                  [](const std::vector<std::uint32_t>& left, const std::vector<std::uint32_t>& right) noexcept
                  {
                      if (left.empty() || right.empty())
                          return left.size() < right.size();
                      if (left.front() != right.front())
                          return left.front() < right.front();
                      return left.size() < right.size();
                  });
    }

    void decomposition::strong_connect(const std::uint32_t node) noexcept
    {
        ++tarjan_visit_count_;
        index_[node] = next_index_;
        low_link_[node] = next_index_;
        ++next_index_;
        tarjan_stack_.push_back(node);
        if (tarjan_stack_.size() > max_tarjan_stack_depth_)
            max_tarjan_stack_depth_ = tarjan_stack_.size();
        on_stack_.insert(node);

        const auto it = adjacency_.find(node);
        if (it != adjacency_.end())
        {
            for (const auto successor: it->second)
            {
                ++successor_scan_count_;
                if (!index_.contains(successor))
                {
                    ++tree_edge_relaxation_count_;
                    strong_connect(successor);
                    low_link_[node] = std::min(low_link_[node], low_link_[successor]);
                }
                else if (on_stack_.contains(successor))
                {
                    ++back_edge_relaxation_count_;
                    low_link_[node] = std::min(low_link_[node], index_[successor]);
                }
            }
        }

        if (low_link_[node] != index_[node])
            return;

        ++root_component_extraction_count_;

        std::vector<std::uint32_t> component {};
        while (!tarjan_stack_.empty())
        {
            const auto top = tarjan_stack_.back();
            tarjan_stack_.pop_back();
            on_stack_.erase(top);
            component.push_back(top);
            if (top == node)
                break;
        }
        components_.push_back(std::move(component));
    }

    void decomposition::update_component_metrics() noexcept
    {
        for (const auto& component: components_)
        {
            if (component.size() < 2u)
                ++singleton_component_count_;
            else
                ++non_singleton_component_count_;

            if (component.size() > max_component_size_)
                max_component_size_ = component.size();
        }
    }
}
