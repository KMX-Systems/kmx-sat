/// @file library/src/kmx/sat/cdcl/var_heap.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/var_heap.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/var_heap.hpp>

namespace kmx::sat::cdcl
{
    void var_heap::resize(const index_t variable_bound) noexcept
    {
        const auto slots = static_cast<std::size_t>(variable_bound) + 1u;
        if (score_.size() < slots)
        {
            score_.resize(slots, 0.0);
            position_.resize(slots, npos);
        }
    }

    void var_heap::clear() noexcept
    {
        for (const auto index: heap_)
            position_[index] = npos;
        heap_.clear();
    }

    void var_heap::push(const index_t index) noexcept
    {
        const auto position = heap_.size();
        heap_.push_back(index);
        position_[index] = static_cast<index_t>(position);
        sift_up(position);
    }

    var_heap::index_t var_heap::pop() noexcept
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

    void var_heap::bump(const index_t index) noexcept
    {
        const auto bumped = score_[index] + increment_;
        score_[index] = bumped;
        if (bumped > rescale_limit)
            rescale();
        if (position_[index] != npos)
            sift_up(position_[index]);
    }

    void var_heap::decay() noexcept
    {
        increment_ /= decay_;
        if (increment_ > rescale_limit)
            rescale();
    }

    void var_heap::rescale_by(const double factor) noexcept
    {
        for (auto& score: score_)
            score *= factor;
        increment_ *= factor;
        ++rescale_count_;
    }

    [[nodiscard]] bool var_heap::ranks_below(const index_t left, const index_t right) const noexcept
    {
        const auto left_score = score_[left];
        const auto right_score = score_[right];
        return (left_score < right_score) || (left_score == right_score && left > right);
    }

    void var_heap::sift_up(std::size_t position) noexcept
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

    void var_heap::sift_down(std::size_t position) noexcept
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
            if ((right_position < count) && ranks_below(child, heap_[right_position]))
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

    void var_heap::rescale() noexcept
    {
        for (auto& score: score_)
            score *= rescale_factor;
        increment_ *= rescale_factor;
        ++rescale_count_;
    }
}
