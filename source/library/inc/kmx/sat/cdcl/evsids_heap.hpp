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
    ///
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
            const auto position_it = positions_.find(index);
            if (position_it != positions_.end())
            {
                scores_[position_it->second].second += bump_increment_;
                sift_up(position_it->second);
            }
            else
            {
                scores_.emplace_back(var, bump_increment_);
                const auto position = scores_.size() - 1u;
                positions_[index] = position;
                sift_up(position);
            }
        }

        /// @brief Renormalizes all activity scores and the shared bump increment to avoid floating-point overflow.
        /// @throws None (noexcept).
        void rescale() noexcept
        {
            for (auto& entry: scores_)
                entry.second *= 0.5;
            bump_increment_ *= 0.5;
        }

        /// @brief Pops and returns the currently unassigned variable with the highest activity score.
        /// @return Highest-activity variable, or `std::nullopt` if the heap is empty.
        /// @throws None (noexcept).
        std::optional<variable> extract_best() noexcept
        {
            if (scores_.empty())
                return {};

            const variable best = scores_.front().first;
            positions_.erase(static_cast<std::size_t>(best.index()));
            if (scores_.size() == 1u)
            {
                scores_.pop_back();
                return best;
            }

            scores_.front() = scores_.back();
            scores_.pop_back();
            positions_[static_cast<std::size_t>(scores_.front().first.index())] = 0u;
            sift_down(0u);
            return best;
        }

        /// @brief Checks whether a variable is currently present in the heap.
        /// @param var Variable to query.
        /// @return True if present (unassigned and not eliminated).
        /// @throws None (noexcept).
        bool contains(const variable var) const noexcept
        {
            return positions_.find(static_cast<std::size_t>(var.index())) != positions_.end();
        }

        /// @brief Restores heap ordering invariants after bulk score or membership changes.
        /// @throws None (noexcept).
        void rebuild() noexcept
        {
            std::make_heap(scores_.begin(), scores_.end(), heap_compare {});
            rebuild_positions();
        }

    private:
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
            positions_[static_cast<std::size_t>(scores_[left].first.index())] = left;
            positions_[static_cast<std::size_t>(scores_[right].first.index())] = right;
        }

        void sift_up(std::size_t position) noexcept
        {
            while (position != 0u)
            {
                const auto parent = (position - 1u) / 2u;
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
                const auto left = position * 2u + 1u;
                if (left >= scores_.size())
                    return;
                auto best = left;
                const auto right = left + 1u;
                if (right < scores_.size() && precedes(right, left))
                    best = right;
                if (!precedes(best, position))
                    return;
                swap_entries(position, best);
                position = best;
            }
        }

        void rebuild_positions() noexcept
        {
            positions_.clear();
            for (std::size_t index {}; index < scores_.size(); ++index)
                positions_[static_cast<std::size_t>(scores_[index].first.index())] = index;
        }

        std::vector<std::pair<variable, double>> scores_ {};
        std::unordered_map<std::size_t, std::size_t> positions_ {};
        double bump_increment_ {1.0};
    };
}
