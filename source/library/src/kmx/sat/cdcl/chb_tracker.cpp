/// @file library/src/kmx/sat/cdcl/chb_tracker.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/chb_tracker.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/chb_tracker.hpp>

namespace kmx::sat::cdcl
{
    void chb_tracker::decay_step() noexcept
    {
        for (auto& entry: scores_)
            entry.second *= decay_factor_;
        ++decay_count_;
    }

    void chb_tracker::update_score(const variable var, const double reward) noexcept
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
}
