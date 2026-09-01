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
        void increase_score(const variable var) noexcept
        {
            const auto index = static_cast<std::size_t>(var.index());
            const auto activity = retained_activity(index) + bump_increment_;
            set_retained_activity(index, activity);

            const auto position = position_of(index);
            if (position != npos)
            {
                scores_[position].second = activity;
                sift_up(position);
            }
            else
            {
                scores_.emplace_back(var, activity);
                const auto new_position = scores_.size() - 1u;
                set_position(index, new_position);
                sift_up(new_position);
            }
        }

        /// @brief Re-inserts a variable at its retained activity, typically when backtracking unassigns it.
        /// @details `extract_best` pops the heap entry, so without a retained activity array a popped variable
        /// could only ever come back at the default score, and a variable rejected merely for being assigned would
        /// be lost for the rest of the episode. Activity therefore lives in `retained_activity_`, indexed by
        /// variable, and the heap entry is a view onto it.
        /// @param var Variable to re-insert.
        /// @throws None (noexcept).
        void insert(const variable var) noexcept
        {
            const auto index = static_cast<std::size_t>(var.index());
            if (position_of(index) != npos)
                return;
            scores_.emplace_back(var, retained_activity(index));
            const auto new_position = scores_.size() - 1u;
            set_position(index, new_position);
            sift_up(new_position);
        }

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
        void decay() noexcept
        {
            bump_increment_ /= variable_decay_;
            if (bump_increment_ > rescale_threshold)
                rescale_by(1.0 / rescale_threshold);
        }

        /// @brief Sets the per-conflict decay factor; smaller values forget older conflicts faster.
        /// @param decay Factor in (0, 1]; values outside that range are ignored.
        /// @throws None (noexcept).
        void set_variable_decay(const double decay) noexcept
        {
            if (decay > 0.0 && decay <= 1.0)
                variable_decay_ = decay;
        }

        /// @brief Pops and returns the currently unassigned variable with the highest activity score.
        /// @return Highest-activity variable, or `std::nullopt` if the heap is empty.
        /// @throws None (noexcept).
        std::optional<variable> extract_best() noexcept
        {
            if (scores_.empty())
                return {};

            const variable best = scores_.front().first;
            erase_position(static_cast<std::size_t>(best.index()));
            if (scores_.size() == 1u)
            {
                scores_.pop_back();
                return best;
            }

            scores_.front() = scores_.back();
            scores_.pop_back();
            set_position(static_cast<std::size_t>(scores_.front().first.index()), 0u);
            sift_down(0u);
            return best;
        }

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
        static constexpr std::size_t direct_activity_limit {std::size_t {1} << 24};

        double retained_activity(const std::size_t index) const noexcept
        {
            if (index < direct_activity_limit)
                return index < retained_activity_.size() ? retained_activity_[index] : 0.0;
            const auto it = overflow_retained_activity_.find(index);
            return it == overflow_retained_activity_.end() ? 0.0 : it->second;
        }

        void set_retained_activity(const std::size_t index, const double activity) noexcept
        {
            if (index < direct_activity_limit)
            {
                if (index >= retained_activity_.size())
                    retained_activity_.resize(index + 1u, 0.0);
                retained_activity_[index] = activity;
                return;
            }
            overflow_retained_activity_[index] = activity;
        }

        std::vector<double> retained_activity_ {};
        std::unordered_map<std::size_t, double> overflow_retained_activity_ {};
        /// Variable indices below this bound use the flat table; anything above falls back to the overflow map.
        static constexpr std::size_t direct_index_limit {std::size_t {1u} << 24};

        std::size_t position_of(const std::size_t index) const noexcept
        {
            if (index < direct_index_limit)
                return index < positions_.size() ? positions_[index] : npos;
            const auto it = overflow_positions_.find(index);
            return it == overflow_positions_.end() ? npos : it->second;
        }

        void set_position(const std::size_t index, const std::size_t position) noexcept
        {
            if (index < direct_index_limit)
            {
                if (index >= positions_.size())
                    positions_.resize(index + 1u, npos);
                positions_[index] = position;
                return;
            }
            overflow_positions_[index] = position;
        }

        void erase_position(const std::size_t index) noexcept
        {
            if (index < direct_index_limit)
            {
                if (index < positions_.size())
                    positions_[index] = npos;
                return;
            }
            overflow_positions_.erase(index);
        }

        struct heap_compare final
        {
            bool operator()(const std::pair<variable, double>& left, const std::pair<variable, double>& right) const noexcept
            {
                if (left.second != right.second)
                    return left.second < right.second;
                return left.first.index() > right.first.index();
            }
        };

        bool precedes(const std::size_t left, const std::size_t right) const noexcept
        {
            return heap_compare {}(scores_[right], scores_[left]);
        }

        void swap_entries(const std::size_t left, const std::size_t right) noexcept
        {
            std::swap(scores_[left], scores_[right]);
            set_position(static_cast<std::size_t>(scores_[left].first.index()), left);
            set_position(static_cast<std::size_t>(scores_[right].first.index()), right);
        }

        void sift_up(std::size_t position) noexcept
        {
            while (position != 0u)
            {
                const auto parent = (position - 1u) / 4u;
                if (!precedes(position, parent))
                    break;
                swap_entries(position, parent);
                position = parent;
            }
        }

        void sift_down(std::size_t position) noexcept
        {
            for (;;)
            {
                const auto first_child = position * 4u + 1u;
                if (first_child >= scores_.size())
                    return;
                auto best = first_child;
                const auto second_child = first_child + 1u;
                if (second_child < scores_.size() && precedes(second_child, best))
                    best = second_child;
                const auto third_child = first_child + 2u;
                if (third_child < scores_.size() && precedes(third_child, best))
                    best = third_child;
                const auto fourth_child = first_child + 3u;
                if (fourth_child < scores_.size() && precedes(fourth_child, best))
                    best = fourth_child;
                if (!precedes(best, position))
                    return;
                swap_entries(position, best);
                position = best;
            }
        }

        void rebuild_positions() noexcept
        {
            std::fill(positions_.begin(), positions_.end(), npos);
            overflow_positions_.clear();
            for (std::size_t index {}; index < scores_.size(); ++index)
                set_position(static_cast<std::size_t>(scores_[index].first.index()), index);
        }

        std::vector<std::pair<variable, double>> scores_ {};
        std::vector<std::size_t> positions_ {};
        std::unordered_map<std::size_t, std::size_t> overflow_positions_ {};
        /// @brief Multiplicative factor applied per conflict; 0.95 matches the MiniSat-family default.
        static constexpr double default_variable_decay {0.95};
        /// @brief Score ceiling that triggers a proportional rescale, keeping the doubles far from overflow.
        static constexpr double rescale_threshold {1e100};

        /// @brief Scales every score and the shared increment by `factor`, preserving their relative order.
        /// @throws None (noexcept).
        void rescale_by(const double factor) noexcept
        {
            for (auto& entry: scores_)
                entry.second *= factor;
            for (auto& activity: retained_activity_)
                activity *= factor;
            for (auto& [index, activity]: overflow_retained_activity_)
                activity *= factor;
            bump_increment_ *= factor;
        }

        double variable_decay_ {default_variable_decay};
        double bump_increment_ {1.0};
    };
}
