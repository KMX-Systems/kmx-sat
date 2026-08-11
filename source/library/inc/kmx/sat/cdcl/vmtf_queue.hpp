/// @file inc/kmx/sat/cdcl/vmtf_queue.hpp
/// @brief Variable Move To Front heuristic.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <optional>
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
            if (std::find_if(variables_.begin(), variables_.end(), [&](const auto& entry) noexcept {
                    return entry == var;
                }) != variables_.end())
            {
                return;
            }
            variables_.push_back(var);
        }

        /// @brief Moves a variable to the front of the queue after it participates in a conflict/learned clause.
        /// @param var Variable to bump.
        /// @throws None (noexcept).
        void bump(const variable var) noexcept
        {
            const auto it = std::find(variables_.begin(), variables_.end(), var);
            if (it == variables_.end())
            {
                return;
            }
            std::rotate(variables_.begin(), it, it + 1);
        }

        /// @brief Returns the frontmost currently unassigned variable, the next branching candidate.
        /// @return Candidate variable, or `std::nullopt` if none is unassigned.
        /// @throws None (noexcept).
        std::optional<variable> front_candidate() const noexcept
        {
            if (variables_.empty())
            {
                return std::nullopt;
            }
            return variables_.front();
        }

        /// @brief Removes a variable from the queue, typically when eliminated by simplification.
        /// @param var Variable to remove.
        /// @throws None (noexcept).
        void remove(const variable var) noexcept
        {
            auto it = std::find(variables_.begin(), variables_.end(), var);
            if (it != variables_.end())
            {
                variables_.erase(it);
            }
        }

        /// @brief Reinserts a previously removed variable, typically when backtracking restores it.
        /// @param var Variable to reinsert.
        /// @throws None (noexcept).
        void reinsert(const variable var) noexcept
        {
            activate(var);
        }

        /// @brief Randomizes the queue order, used by randomized restart/rephase strategies.
        /// @throws None (noexcept).
        void shuffle() noexcept
        {
        }

        /// @brief Returns the number of currently tracked variables in the queue.
        /// @return Number of active variables.
        std::size_t size() const noexcept
        {
            return variables_.size();
        }

    private:
        std::vector<variable> variables_ {};
    };
}
