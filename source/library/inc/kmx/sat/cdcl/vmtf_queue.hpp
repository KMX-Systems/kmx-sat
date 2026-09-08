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
    /// @details
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
        void activate(const variable var) noexcept;

        /// @brief Moves a variable to the front of the queue after it participates in a conflict/learned clause.
        /// @param var Variable to bump.
        /// @throws None (noexcept).
        void bump(const variable var) noexcept;

        /// @brief Returns the frontmost currently unassigned variable, the next branching candidate.
        /// @return Candidate variable, or `std::nullopt` if none is unassigned.
        /// @throws None (noexcept).
        std::optional<variable> front_candidate() const noexcept;

        /// @brief Returns the frontmost variable satisfying `selectable`, without modifying the queue.
        /// @details The decision engine previously called `remove` on every candidate it rejected, which drops the
        /// variable permanently even though rejection only means "currently assigned". Walking the list leaves the
        /// bump order intact, so a variable becomes a candidate again as soon as backtracking unassigns it.
        /// @tparam Predicate Callable of signature `bool(variable)`.
        /// @param selectable Predicate identifying an acceptable candidate.
        /// @return Frontmost acceptable variable, or `std::nullopt` when none qualifies.
        /// @throws None (noexcept).
        template <typename Predicate>
        std::optional<variable> front_candidate_if(Predicate&& selectable) const noexcept
        {
            for (auto node_index = head_; node_index != npos; node_index = nodes_storage_[node_index].next)
            {
                const auto candidate = nodes_storage_[node_index].value;
                if (selectable(candidate))
                    return candidate;
            }
            return {};
        }

        /// @brief Removes a variable from the queue, typically when eliminated by simplification.
        /// @param var Variable to remove.
        /// @throws None (noexcept).
        void remove(const variable var) noexcept;

        /// @brief Reinserts a previously removed variable, typically when backtracking restores it.
        /// @param var Variable to reinsert.
        /// @throws None (noexcept).
        void reinsert(const variable var) noexcept { activate(var); }

        /// @brief Randomizes the queue order, used by randomized restart/rephase strategies.
        /// @throws None (noexcept).
        void shuffle() noexcept;

        /// @brief Perturbs the queue order with caller-provided deterministic salt (e.g. restart count).
        /// @param salt External salt used to diversify the next shuffle stride deterministically.
        /// @throws None (noexcept).
        void shuffle(const std::uint32_t salt) noexcept;

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

        /// Variable indices below this bound use the flat table; anything above falls back to the overflow map.
        static constexpr std::size_t direct_index_limit {std::size_t {1u} << 24};

        std::size_t node_index_of(const std::size_t variable_index) const noexcept;

        void set_node_index(const std::size_t variable_index, const std::size_t node_index) noexcept;

        void erase_node_index(const std::size_t variable_index) noexcept;

        void unlink(const std::size_t node_index) noexcept;

        void relink(const std::vector<std::size_t>& order) noexcept;

        std::vector<node> nodes_storage_ {};
        std::vector<std::size_t> node_indices_ {};
        std::unordered_map<std::size_t, std::size_t> overflow_node_indices_ {};
        std::size_t head_ {npos};
        std::size_t tail_ {npos};
        std::size_t size_ {};
        std::uint32_t shuffle_epoch_ {};
        std::uint32_t last_shuffle_stride_ {};
    };
}
