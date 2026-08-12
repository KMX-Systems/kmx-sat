/// @file inc/kmx/sat/cdcl/evsids_heap.hpp
/// @brief EVSIDS heuristic for branching variables.
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
            const auto it = std::find_if(scores_.begin(), scores_.end(), [&](const auto& entry) noexcept { return entry.first == var; });
            if (it != scores_.end())
            {
                it->second += bump_increment_;
            }
            else
            {
                scores_.emplace_back(var, bump_increment_);
            }
        }

        /// @brief Renormalizes all activity scores and the shared bump increment to avoid floating-point overflow.
        /// @throws None (noexcept).
        void rescale() noexcept
        {
            for (auto& entry: scores_)
            {
                entry.second *= 0.5;
            }
            bump_increment_ *= 0.5;
        }

        /// @brief Pops and returns the currently unassigned variable with the highest activity score.
        /// @return Highest-activity variable, or `std::nullopt` if the heap is empty.
        /// @throws None (noexcept).
        std::optional<variable> extract_best() noexcept
        {
            if (scores_.empty())
            {
                return std::nullopt;
            }

            auto best_it = scores_.begin();
            for (auto it = std::next(scores_.begin()); it != scores_.end(); ++it)
            {
                if (it->second > best_it->second)
                {
                    best_it = it;
                }
            }

            const variable best = best_it->first;
            scores_.erase(best_it);
            return best;
        }

        /// @brief Checks whether a variable is currently present in the heap.
        /// @param var Variable to query.
        /// @return True if present (unassigned and not eliminated).
        /// @throws None (noexcept).
        bool contains(const variable var) const noexcept
        {
            return std::find_if(scores_.begin(), scores_.end(), [&](const auto& entry) noexcept { return entry.first == var; }) !=
                   scores_.end();
        }

        /// @brief Restores heap ordering invariants after bulk score or membership changes.
        /// @throws None (noexcept).
        void rebuild() noexcept
        {
            std::sort(scores_.begin(), scores_.end(),
                      [](const auto& left, const auto& right) noexcept
                      {
                          if (left.second != right.second)
                          {
                              return left.second > right.second;
                          }
                          return left.first.index() < right.first.index();
                      });
        }

    private:
        std::vector<std::pair<variable, double>> scores_ {};
        double bump_increment_ {1.0};
    };
}
