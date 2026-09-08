/// @file inc/kmx/sat/cdcl/evsids_heap.hpp
/// @brief EVSIDS heuristic for branching variables.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <optional>
    #include <unordered_map>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Per-variable activity scores held as a flat, cache-friendly list.
    using variable_score_list_t = std::vector<std::pair<variable, double>>;

    /// @brief EVSIDS heuristic for branching variables.
    /// @details
    /// EVSIDS (Exponential Variable State Independent Decaying Sum, the MiniSat/Kissat-style descendant of VSIDS)
    /// keeps one floating-point activity score per variable in a binary max-heap: `increase_score` bumps a
    /// variable's score when it participates in conflict analysis, using an exponentially increasing bump amount
    /// (implemented as a shared increment that grows over time) rather than periodically decaying every score
    /// individually; `rescale` renormalizes all scores and the increment when they approach floating-point range
    /// limits; `extract_best` pops the highest-activity unassigned variable for `engine::decision` to branch on;
    /// `contains` checks heap membership (false once a variable is assigned or eliminated); `rebuild` restores heap
    /// invariants after bulk score changes (for example after `rescale` or after eliminations change membership).
    /// @reference VSIDS (Variable State Independent Decaying Sum), Chaff (Moskewicz et al.), with the EVSIDS
    /// exponential-bump variant popularized by MiniSat and used by Kissat.
    class evsids_heap final
    {
    public:
        /// @brief Constructs an empty EVSIDS heap.
        /// @throws None (noexcept).
        evsids_heap() noexcept = default;

        /// @brief Bumps a variable's activity score, typically after conflict-analysis participation.
        /// @param var Variable to bump.
        /// @throws None (noexcept).
        void increase_score(const variable var) noexcept;

        /// @brief Re-inserts a variable at its retained activity, typically when backtracking unassigns it.
        /// @details `extract_best` pops the heap entry, so without a retained activity array a popped variable
        /// could only ever come back at the default score, and a variable rejected merely for being assigned would
        /// be lost for the rest of the episode. Activity therefore lives in `retained_activity_`, indexed by
        /// variable, and the heap entry is a view onto it.
        /// @param var Variable to re-insert.
        /// @throws None (noexcept).
        void insert(const variable var) noexcept;

        /// @brief Renormalizes all activity scores and the shared bump increment to avoid floating-point overflow.
        /// @throws None (noexcept).
        void rescale() noexcept { rescale_by(0.5); }

        /// @brief Ages every score by making subsequent bumps worth more; call once per conflict.
        /// @details This is what makes the heuristic exponential rather than a plain tally. `increase_score` adds a
        /// shared increment, so growing that increment by `1 / decay` after each conflict means a bump from the
        /// current conflict outweighs one from N conflicts ago by `decay^-N`, and the ranking tracks the
        /// subproblem the search is actually in. Without it every score is an all-time participation count with no
        /// recency weighting, and `rescale` alone cannot supply that: halving scores and the increment together
        /// leaves the ordering identical.
        /// @throws None (noexcept).
        void decay() noexcept;

        /// @brief Sets the per-conflict decay factor; smaller values forget older conflicts faster.
        /// @param decay Factor in (0, 1]; values outside that range are ignored.
        /// @throws None (noexcept).
        void set_variable_decay(const double decay) noexcept
        {
            if ((decay > 0.0) && (decay <= 1.0))
                variable_decay_ = decay;
        }

        /// @brief Pops and returns the currently unassigned variable with the highest activity score.
        /// @return Highest-activity variable, or `std::nullopt` if the heap is empty.
        /// @throws None (noexcept).
        std::optional<variable> extract_best() noexcept;

        /// @brief Checks whether a variable is currently present in the heap.
        /// @param var Variable to query.
        /// @return True if present (unassigned and not eliminated).
        /// @throws None (noexcept).
        bool contains(const variable var) const noexcept { return position_of(static_cast<std::size_t>(var.index())) != npos; }

        /// @brief Restores heap ordering invariants after bulk score or membership changes.
        /// @throws None (noexcept).
        void rebuild() noexcept
        {
            std::make_heap(scores_.begin(), scores_.end(), heap_compare {});
            rebuild_positions();
        }

    private:
        static constexpr std::size_t npos {static_cast<std::size_t>(~0u)};
        static constexpr std::size_t direct_activity_limit {std::size_t {1u} << 24};

        double retained_activity(const std::size_t index) const noexcept;

        void set_retained_activity(const std::size_t index, const double activity) noexcept;

        std::vector<double> retained_activity_ {};
        std::unordered_map<std::size_t, double> overflow_retained_activity_ {};
        /// Variable indices below this bound use the flat table; anything above falls back to the overflow map.
        static constexpr std::size_t direct_index_limit {std::size_t {1u} << 24};

        std::size_t position_of(const std::size_t index) const noexcept;

        void set_position(const std::size_t index, const std::size_t position) noexcept;

        void erase_position(const std::size_t index) noexcept;

        struct heap_compare final
        {
            bool operator()(const std::pair<variable, double>& left, const std::pair<variable, double>& right) const noexcept;
        };

        bool precedes(const std::size_t left, const std::size_t right) const noexcept
        {
            return heap_compare {}(scores_[right], scores_[left]);
        }

        void swap_entries(const std::size_t left, const std::size_t right) noexcept;

        void sift_up(std::size_t position) noexcept;

        void sift_down(std::size_t position) noexcept;

        void rebuild_positions() noexcept;

        variable_score_list_t scores_ {};
        std::vector<std::size_t> positions_ {};
        std::unordered_map<std::size_t, std::size_t> overflow_positions_ {};
        /// @brief Multiplicative factor applied per conflict; 0.95 matches the MiniSat-family default.
        static constexpr double default_variable_decay {0.95};
        /// @brief Score ceiling that triggers a proportional rescale, keeping the doubles far from overflow.
        static constexpr double rescale_threshold {1e100};

        /// @brief Scales every score and the shared increment by `factor`, preserving their relative order.
        /// @throws None (noexcept).
        void rescale_by(const double factor) noexcept;

        double variable_decay_ {default_variable_decay};
        double bump_increment_ {1.0};
    };
}
