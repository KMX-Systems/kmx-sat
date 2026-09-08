/// @file library/src/kmx/sat/cdcl/engine/backtrack.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/engine/backtrack.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/engine/backtrack.hpp>

namespace kmx::sat::cdcl::engine
{
    void backtrack::attach_state(trail& trail_state, store::assignment& assignment, stack::decision_frame& decision_frames) noexcept
    {
        trail_state_ = &trail_state;
        assignment_ = &assignment;
        decision_frames_ = &decision_frames;
    }

    void backtrack::backtrack_to_level(const std::uint32_t level) noexcept
    {
        if ((trail_state_ == nullptr) || (assignment_ == nullptr) || (decision_frames_ == nullptr))
            return;

        const auto current_level = decision_frames_->current_level();
        const auto target_level = (level < current_level) ? level : current_level;
        {
            const auto trail_head = trail_state_->current_head();
            const auto trail_base = decision_frames_->trail_base_of_level(target_level + 1u);
            const auto preserved_head = (trail_base < trail_head) ? trail_base : trail_head;
            trail_state_->pop_to((preserved_head < trail_head) ? preserved_head : trail_head);
        }

        const auto new_level = (target_level == 0u) ? 0u : target_level;
        decision_frames_->pop_to_level(new_level);
        assignment_->unassign_above_level(target_level);
        assignment_->set_current_level(new_level);
        assignment_->set_current_trail_position(trail_state_->current_head());
        last_backtracked_level_ = target_level;
    }

    void backtrack::chronological_backtrack() noexcept
    {
        if ((trail_state_ == nullptr) || (assignment_ == nullptr) || (decision_frames_ == nullptr))
            return;
        const auto current_level = decision_frames_->current_level();
        if (current_level != 0u)
            backtrack_to_level(current_level - 1u);
    }

    void backtrack::reuse_trail() noexcept
    {
        if (decision_frames_ != nullptr)
        {
            decision_frames_->reuse_trail_metadata();
            ++trail_reuse_count_;
        }
    }
}
