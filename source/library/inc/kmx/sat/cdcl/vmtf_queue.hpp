/// @file inc/kmx/sat/cdcl/vmtf_queue.hpp
/// @brief Variable Move To Front heuristic.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <optional>
    #include <unordered_map>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Variable Move To Front heuristic.
    ///
    /// VMTF (as used by CaDiCaL) maintains variables in a doubly-linked queue ordered by recency of conflict
    /// involvement rather than a numeric score: `bump` moves a variable to the front of the queue whenever it
    /// participates in a learned clause, `front_candidate` hands `engine::decision` the frontmost currently
    /// unassigned variable as the next branching candidate, and `activate`/`remove`/`reinsert` manage queue
    /// membership as variables become newly relevant, get eliminated, or are restored by backtracking. `shuffle`
    /// supports randomized restarts of the queue order. Being pointer-based rather than heap-based, VMTF avoids the
    /// `O(log n)` rebalancing cost of `evsids_heap` at the expense of coarser-grained prioritization.
    /// @reference Variable Move-to-Front heuristic, as used by CaDiCaL (see also the original VMTF/BerkMin lineage in
    /// SAT solver decision heuristics literature).
    class vmtf_queue final
    {
    public:
        /// @brief Constructs an empty VMTF queue.
        /// @throws None (noexcept).
        vmtf_queue() noexcept = default;

        /// @brief Inserts a variable into the queue, typically when it first becomes relevant.
        /// @param var Variable to activate.
        /// @throws None (noexcept).
        void activate(const variable var) noexcept
        {
            const auto variable_index = static_cast<std::size_t>(var.index());
            if (nodes_.find(variable_index) != nodes_.end())
                return;

            node node_value {var, npos, npos};
            const auto node_index = nodes_storage_.size();
            nodes_storage_.push_back(node_value);
            nodes_.emplace(variable_index, node_index);
            if (tail_ == npos)
            {
                head_ = node_index;
                tail_ = node_index;
            }
            else
            {
                nodes_storage_[tail_].next = node_index;
                nodes_storage_[node_index].previous = tail_;
                tail_ = node_index;
            }
            ++size_;
        }

        /// @brief Moves a variable to the front of the queue after it participates in a conflict/learned clause.
        /// @param var Variable to bump.
        /// @throws None (noexcept).
        void bump(const variable var) noexcept
        {
            const auto it = nodes_.find(static_cast<std::size_t>(var.index()));
            if (it == nodes_.end() || it->second == head_)
                return;

            const auto node_index = it->second;
            unlink(node_index);
            nodes_storage_[node_index].previous = npos;
            nodes_storage_[node_index].next = head_;
            nodes_storage_[head_].previous = node_index;
            head_ = node_index;
        }

        /// @brief Returns the frontmost currently unassigned variable, the next branching candidate.
        /// @return Candidate variable, or `std::nullopt` if none is unassigned.
        /// @throws None (noexcept).
        std::optional<variable> front_candidate() const noexcept
        {
            if (head_ == npos)
                return {};
            return nodes_storage_[head_].value;
        }

        /// @brief Removes a variable from the queue, typically when eliminated by simplification.
        /// @param var Variable to remove.
        /// @throws None (noexcept).
        void remove(const variable var) noexcept
        {
            const auto it = nodes_.find(static_cast<std::size_t>(var.index()));
            if (it != nodes_.end())
            {
                unlink(it->second);
                nodes_.erase(it);
                --size_;
            }
        }

        /// @brief Reinserts a previously removed variable, typically when backtracking restores it.
        /// @param var Variable to reinsert.
        /// @throws None (noexcept).
        void reinsert(const variable var) noexcept { activate(var); }

        /// @brief Randomizes the queue order, used by randomized restart/rephase strategies.
        /// @throws None (noexcept).
        void shuffle() noexcept
        {
            if (size_ <= 1u)
                return;

            const auto max_stride = static_cast<std::uint32_t>(size_ - 1u);
            const auto stride = static_cast<std::uint32_t>((shuffle_epoch_ % max_stride) + 1u);
            std::vector<std::size_t> order {};
            order.reserve(size_);
            for (auto current = head_; current != npos; current = nodes_storage_[current].next)
                order.push_back(current);
            std::rotate(order.begin(), order.begin() + stride, order.end());
            relink(order);
            last_shuffle_stride_ = stride;
            ++shuffle_epoch_;
        }

        /// @brief Perturbs the queue order with caller-provided deterministic salt (e.g. restart count).
        /// @param salt External salt used to diversify the next shuffle stride deterministically.
        /// @throws None (noexcept).
        void shuffle(const std::uint32_t salt) noexcept
        {
            if (size_ <= 1u)
                return;

            if (size_ > 2u)
            {
                const auto max_stride = static_cast<std::uint32_t>(size_ - 1u);
                shuffle_epoch_ = (shuffle_epoch_ + salt) % max_stride;
            }
            shuffle();
        }

        /// @brief Returns the number of currently tracked variables in the queue.
        /// @return Number of active variables.
        std::size_t size() const noexcept { return size_; }

        std::uint32_t last_shuffle_stride() const noexcept { return last_shuffle_stride_; }

    private:
        static constexpr std::size_t npos {static_cast<std::size_t>(-1)};

        struct node final
        {
            variable value {};
            std::size_t previous {npos};
            std::size_t next {npos};
        };

        void unlink(const std::size_t node_index) noexcept
        {
            const auto previous = nodes_storage_[node_index].previous;
            const auto next = nodes_storage_[node_index].next;
            if (previous == npos)
                head_ = next;
            else
                nodes_storage_[previous].next = next;
            if (next == npos)
                tail_ = previous;
            else
                nodes_storage_[next].previous = previous;
        }

        void relink(const std::vector<std::size_t>& order) noexcept
        {
            head_ = order.front();
            tail_ = order.back();
            for (std::size_t index {}; index < order.size(); ++index)
            {
                auto& current = nodes_storage_[order[index]];
                current.previous = index == 0u ? npos : order[index - 1u];
                current.next = index + 1u == order.size() ? npos : order[index + 1u];
            }
        }

        std::vector<node> nodes_storage_ {};
        std::unordered_map<std::size_t, std::size_t> nodes_ {};
        std::size_t head_ {npos};
        std::size_t tail_ {npos};
        std::size_t size_ {};
        std::uint32_t shuffle_epoch_ {};
        std::uint32_t last_shuffle_stride_ {};
    };
}
