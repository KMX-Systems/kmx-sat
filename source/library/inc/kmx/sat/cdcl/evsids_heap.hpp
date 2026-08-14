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
            const auto position = position_of(index);
            if (position != npos)
            {
                scores_[position].second += bump_increment_;
                sift_up(position);
            }
            else
            {
                scores_.emplace_back(var, bump_increment_);
                const auto new_position = scores_.size() - 1u;
                set_position(index, new_position);
                sift_up(new_position);
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
        static constexpr std::size_t npos {static_cast<std::size_t>(-1)};
        /// Variable indices below this bound use the flat table; anything above falls back to the overflow map.
        static constexpr std::size_t direct_index_limit {std::size_t {1} << 24};

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
            std::fill(positions_.begin(), positions_.end(), npos);
            overflow_positions_.clear();
            for (std::size_t index {}; index < scores_.size(); ++index)
                set_position(static_cast<std::size_t>(scores_[index].first.index()), index);
        }

        std::vector<std::pair<variable, double>> scores_ {};
        std::vector<std::size_t> positions_ {};
        std::unordered_map<std::size_t, std::size_t> overflow_positions_ {};
        double bump_increment_ {1.0};
    };
}
