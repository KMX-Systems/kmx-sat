/// @file inc/kmx/sat/cdcl/chb_tracker.hpp
/// @brief Conflict History-Based branching scores as a third candidate heuristic alongside VMTF/EVSIDS. Research-track
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <optional>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Conflict History-Based branching scores as a third candidate heuristic alongside VMTF/EVSIDS. Research-track
    /// heuristic to be validated against the comparative benchmarking harness before becoming a default.
    ///
    /// CHB assigns each variable a score updated by a "reward" signal tied to how recently it was involved in a
    /// conflict, decaying over time (`decay_step`) rather than depending purely on bump-and-rescale like
    /// `evsids_heap`. `update_on_assignment`/`update_on_conflict` feed the reward signal, and `score_of` exposes the
    /// current score so `engine::decision::select_heuristic_blend` can combine it with VMTF/EVSIDS output under an
    /// explicit blending policy.
    /// @note This is a research-track heuristic: it must be validated on the comparative benchmarking harness
    /// against pinned CaDiCaL/Kissat releases before `engine::decision` may use it by default; until then it is an
    /// optional signal `engine::decision` may consult alongside VMTF/EVSIDS.
    /// @reference Conflict History-Based (CHB) branching heuristic (Liang, Ganesh, Poupart, Czarnecki, "Learning
    /// Rate Based Branching Heuristic for SAT Solvers").
    class chb_tracker final
    {
    public:
        static constexpr double zero_d {0.0};

        /// @brief Constructs a CHB tracker with zero scores for every variable.
        /// @throws None (noexcept).
        chb_tracker() noexcept = default;

        /// @brief Updates the reward signal for a variable when it is assigned.
        /// @param var Variable being assigned.
        /// @throws None (noexcept).
        void update_on_assignment(const variable var) noexcept { update_score(var, 0.25); }

        /// @brief Updates the reward signal for a variable involved in a conflict.
        /// @param var Variable involved in the conflict.
        /// @throws None (noexcept).
        void update_on_conflict(const variable var) noexcept { update_score(var, 1.0); }

        /// @brief Returns the current CHB score for a variable.
        /// @param var Variable to query.
        /// @return Current score value.
        /// @throws None (noexcept).
        double score_of(const variable var) const noexcept
        {
            const auto slot = slot_of(var);
            return slot == npos ? zero_d : scores_[slot].second;
        }

        /// @brief Returns the highest-scoring tracked variable accepted by an optional predicate.
        template <typename predicate_t>
        std::optional<variable> best_candidate(predicate_t&& selectable) const noexcept
        {
            const std::pair<variable, double>* best {};
            for (const auto& entry: scores_)
            {
                if (entry.second <= zero_d || !selectable(entry.first))
                    continue;
                if (best == nullptr || entry.second > best->second ||
                    (entry.second == best->second && entry.first.index() < best->first.index()))
                {
                    best = &entry;
                }
            }
            return best == nullptr ? std::nullopt : std::optional<variable> {best->first};
        }

        /// @brief Applies one decay step to all scores, reducing the weight of older conflict participation.
        /// @throws None (noexcept).
        void decay_step() noexcept
        {
            for (auto& entry: scores_)
                entry.second *= decay_factor_;
            ++decay_count_;
        }

        /// @brief Sets the exponential reward learning rate in the inclusive range [0, 1].
        void set_learning_rate(const double rate) noexcept { learning_rate_ = std::clamp(rate, zero_d, 1.0); }

        /// @brief Sets the multiplicative decay factor in the inclusive range [0, 1].
        void set_decay_factor(const double factor) noexcept { decay_factor_ = std::clamp(factor, zero_d, 1.0); }

        /// @brief Returns how many explicit decay steps have been applied.
        std::uint32_t decay_count() const noexcept { return decay_count_; }

        /// @brief Returns the number of tracked variables with a non-zero CHB score.
        /// @return Number of tracked variables.
        std::size_t tracked_variable_count() const noexcept { return scores_.size(); }

    private:
        static constexpr std::size_t npos {static_cast<std::size_t>(-1)};

        std::size_t slot_of(const variable var) const noexcept
        {
            const auto index = static_cast<std::size_t>(var.index());
            return index < slots_.size() ? slots_[index] : npos;
        }

        void update_score(const variable var, const double reward) noexcept
        {
            const auto index = static_cast<std::size_t>(var.index());
            const auto slot = slot_of(var);
            if (slot != npos)
            {
                auto& score = scores_[slot].second;
                score += learning_rate_ * (reward - score);
                return;
            }
            if (index >= slots_.size())
                slots_.resize(index + 1u, npos);
            slots_[index] = scores_.size();
            scores_.emplace_back(var, learning_rate_ * reward);
        }

        std::vector<std::pair<variable, double>> scores_ {};
        std::vector<std::size_t> slots_ {};
        double learning_rate_ {0.1};
        double decay_factor_ {0.95};
        std::uint32_t decay_count_ {};
    };
}
