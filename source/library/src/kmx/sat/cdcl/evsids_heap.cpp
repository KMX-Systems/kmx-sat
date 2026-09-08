/// @file library/src/kmx/sat/cdcl/evsids_heap.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/evsids_heap.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/evsids_heap.hpp>

namespace kmx::sat::cdcl
{
    void evsids_heap::increase_score(const variable var) noexcept
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

    void evsids_heap::insert(const variable var) noexcept
    {
        const auto index = static_cast<std::size_t>(var.index());
        if (position_of(index) != npos)
            return;
        scores_.emplace_back(var, retained_activity(index));
        const auto new_position = scores_.size() - 1u;
        set_position(index, new_position);
        sift_up(new_position);
    }

    void evsids_heap::decay() noexcept
    {
        bump_increment_ /= variable_decay_;
        if (bump_increment_ > rescale_threshold)
            rescale_by(1.0 / rescale_threshold);
    }

    std::optional<variable> evsids_heap::extract_best() noexcept
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

    double evsids_heap::retained_activity(const std::size_t index) const noexcept
    {
        if (index < direct_activity_limit)
            return (index < retained_activity_.size()) ? retained_activity_[index] : 0.0;
        const auto it = overflow_retained_activity_.find(index);
        return (it == overflow_retained_activity_.end()) ? 0.0 : it->second;
    }

    void evsids_heap::set_retained_activity(const std::size_t index, const double activity) noexcept
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

    std::size_t evsids_heap::position_of(const std::size_t index) const noexcept
    {
        if (index < direct_index_limit)
            return (index < positions_.size()) ? positions_[index] : npos;
        const auto it = overflow_positions_.find(index);
        return (it == overflow_positions_.end()) ? npos : it->second;
    }

    void evsids_heap::set_position(const std::size_t index, const std::size_t position) noexcept
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

    void evsids_heap::erase_position(const std::size_t index) noexcept
    {
        if (index < direct_index_limit)
        {
            if (index < positions_.size())
                positions_[index] = npos;
            return;
        }
        overflow_positions_.erase(index);
    }

    bool evsids_heap::heap_compare::operator()(const std::pair<variable, double>& left,
                                               const std::pair<variable, double>& right) const noexcept
    {
        if (left.second != right.second)
            return left.second < right.second;
        return left.first.index() > right.first.index();
    }

    void evsids_heap::swap_entries(const std::size_t left, const std::size_t right) noexcept
    {
        std::swap(scores_[left], scores_[right]);
        set_position(static_cast<std::size_t>(scores_[left].first.index()), left);
        set_position(static_cast<std::size_t>(scores_[right].first.index()), right);
    }

    void evsids_heap::sift_up(std::size_t position) noexcept
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

    void evsids_heap::sift_down(std::size_t position) noexcept
    {
        for (;;)
        {
            const auto first_child = position * 4u + 1u;
            if (first_child >= scores_.size())
                return;
            auto best = first_child;
            const auto second_child = first_child + 1u;
            if ((second_child < scores_.size()) && precedes(second_child, best))
                best = second_child;
            const auto third_child = first_child + 2u;
            if ((third_child < scores_.size()) && precedes(third_child, best))
                best = third_child;
            const auto fourth_child = first_child + 3u;
            if ((fourth_child < scores_.size()) && precedes(fourth_child, best))
                best = fourth_child;
            if (!precedes(best, position))
                return;
            swap_entries(position, best);
            position = best;
        }
    }

    void evsids_heap::rebuild_positions() noexcept
    {
        std::fill(positions_.begin(), positions_.end(), npos);
        overflow_positions_.clear();
        for (std::size_t index {}; index < scores_.size(); ++index)
            set_position(static_cast<std::size_t>(scores_[index].first.index()), index);
    }

    void evsids_heap::rescale_by(const double factor) noexcept
    {
        for (auto& entry: scores_)
            entry.second *= factor;
        for (auto& activity: retained_activity_)
            activity *= factor;
        for (auto& [index, activity]: overflow_retained_activity_)
            activity *= factor;
        bump_increment_ *= factor;
    }
}
