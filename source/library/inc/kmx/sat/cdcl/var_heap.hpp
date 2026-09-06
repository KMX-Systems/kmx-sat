/// @file inc/kmx/sat/cdcl/var_heap.hpp
/// @brief Binary max-heap of branching candidates ordered by EVSIDS activity.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Binary max-heap of branching candidates ordered by EVSIDS activity.
    /// @details
    /// Activity lives in a flat per-variable array and the heap holds variable indices, so a bump is one array
    /// write and, when the variable is currently a candidate, one sift. The search maintains the invariant that
    /// every unassigned variable is in the heap: variables are pushed back as backtracking unassigns them, and a
    /// popped variable that turns out to be assigned is simply discarded. An empty heap is therefore the proof
    /// that every variable is assigned, which is what makes the satisfiability test free.
    ///
    /// Scores age by growing the shared increment rather than decaying every score (the exponential-VSIDS scheme
    /// of MiniSat and CaDiCaL); when the increment approaches the floating-point range everything is rescaled
    /// together, which preserves the ordering exactly.
    /// @reference Exponential VSIDS as used by MiniSat, CaDiCaL and Kissat.
    class var_heap final
    {
    public:
        using index_t = variable::index_t;
        static constexpr index_t npos {static_cast<index_t>(-1)};

        var_heap() noexcept = default;

        /// @brief Grows the per-variable tables to cover `variable_bound`; scores of new variables start at zero.
        void resize(const index_t variable_bound) noexcept
        {
            const auto slots = static_cast<std::size_t>(variable_bound) + 1u;
            if (score_.size() < slots)
            {
                score_.resize(slots, 0.0);
                position_.resize(slots, npos);
            }
        }

        /// @brief Empties the heap without touching the scores.
        void clear() noexcept
        {
            for (const auto index: heap_)
                position_[index] = npos;
            heap_.clear();
        }

        [[nodiscard]] bool empty() const noexcept { return heap_.empty(); }

        [[nodiscard]] std::size_t size() const noexcept { return heap_.size(); }

        [[nodiscard]] bool contains(const index_t index) const noexcept { return position_[index] != npos; }

        [[nodiscard]] double score(const index_t index) const noexcept { return score_[index]; }

        [[nodiscard]] index_t top() const noexcept { return heap_.front(); }

        /// @brief Inserts a variable that is not currently a candidate.
        void push(const index_t index) noexcept
        {
            const auto position = heap_.size();
            heap_.push_back(index);
            position_[index] = static_cast<index_t>(position);
            sift_up(position);
        }

        /// @brief Removes and returns the highest-activity candidate.
        index_t pop() noexcept
        {
            const auto best = heap_.front();
            const auto last = heap_.back();
            heap_.pop_back();
            position_[best] = npos;
            if (!heap_.empty())
            {
                heap_.front() = last;
                position_[last] = 0u;
                sift_down(0u);
            }
            return best;
        }

        /// @brief Adds the current increment to a variable's activity and restores its heap position.
        void bump(const index_t index) noexcept
        {
            const auto bumped = score_[index] + increment_;
            score_[index] = bumped;
            if (bumped > rescale_limit)
                rescale();
            if (position_[index] != npos)
                sift_up(position_[index]);
        }

        /// @brief Ages every score relative to future bumps; call once per conflict.
        void decay() noexcept
        {
            increment_ /= decay_;
            if (increment_ > rescale_limit)
                rescale();
        }

        /// @brief Renormalizes every score and the increment by `factor`, preserving the ordering exactly.
        /// @details Periodic maintenance keeps the magnitudes small between the overflow-driven rescales;
        /// it never changes which variable ranks first.
        void rescale_by(const double factor) noexcept
        {
            for (auto& score: score_)
                score *= factor;
            increment_ *= factor;
            ++rescale_count_;
        }

        /// @brief Sets the per-conflict decay factor in (0, 1]; values outside that range are ignored.
        void set_decay(const double decay) noexcept
        {
            if (decay > 0.0 && decay <= 1.0)
                decay_ = decay;
        }

        [[nodiscard]] double increment() const noexcept { return increment_; }

        [[nodiscard]] std::uint32_t rescale_count() const noexcept { return rescale_count_; }

    private:
        static constexpr double rescale_limit {1e150};
        static constexpr double rescale_factor {1e-150};

        /// @brief True when `left` ranks strictly below `right`; ties go to the lower variable index.
        [[nodiscard]] bool ranks_below(const index_t left, const index_t right) const noexcept
        {
            const auto left_score = score_[left];
            const auto right_score = score_[right];
            return left_score < right_score || (left_score == right_score && left > right);
        }

        void place(const std::size_t position, const index_t index) noexcept
        {
            heap_[position] = index;
            position_[index] = static_cast<index_t>(position);
        }

        void sift_up(std::size_t position) noexcept
        {
            const auto index = heap_[position];
            while (position != 0u)
            {
                const auto parent_position = (position - 1u) / 2u;
                const auto parent = heap_[parent_position];
                if (!ranks_below(parent, index))
                    break;
                place(position, parent);
                position = parent_position;
            }
            place(position, index);
        }

        void sift_down(std::size_t position) noexcept
        {
            const auto index = heap_[position];
            const auto count = heap_.size();
            for (;;)
            {
                auto child_position = 2u * position + 1u;
                if (child_position >= count)
                    break;
                auto child = heap_[child_position];
                const auto right_position = child_position + 1u;
                if (right_position < count && ranks_below(child, heap_[right_position]))
                {
                    child_position = right_position;
                    child = heap_[right_position];
                }
                if (!ranks_below(index, child))
                    break;
                place(position, child);
                position = child_position;
            }
            place(position, index);
        }

        void rescale() noexcept
        {
            for (auto& score: score_)
                score *= rescale_factor;
            increment_ *= rescale_factor;
            ++rescale_count_;
        }

        std::vector<double> score_ {};
        std::vector<index_t> heap_ {};
        std::vector<index_t> position_ {};
        double increment_ {1.0};
        double decay_ {0.95};
        std::uint32_t rescale_count_ {};
    };
}
