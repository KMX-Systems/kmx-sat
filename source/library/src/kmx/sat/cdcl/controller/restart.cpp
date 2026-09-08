/// @file library/src/kmx/sat/cdcl/controller/restart.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/controller/restart.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/controller/restart.hpp>

namespace kmx::sat::cdcl::controller
{
    void restart::reset() noexcept
    {
        conflict_count_ = 0u;
        decision_count_ = 0u;
        restart_count_ = 0u;
        restart_sequence_index_ = 0u;
        decision_restart_sequence_index_ = 0u;
        conflicts_at_last_restart_ = 0u;
        next_scheduled_restart_at_ = restart_interval_;
        next_scheduled_decision_restart_at_ = decision_restart_interval_;
        restart_pending_ = false;
        fast_glue_ema_ = 0.0;
        slow_glue_ema_ = 0.0;
        glue_observation_count_ = 0u;
    }

    void restart::tick_conflict() noexcept
    {
        ++conflict_count_;

        if (restart_interval_ == 0u)
            return;

        if (conflict_count_ >= next_scheduled_restart_at_)
        {
            restart_pending_ = true;
            next_scheduled_restart_at_ = conflict_count_ + next_restart_budget();
        }
    }

    void restart::observe_glue(const std::uint32_t glue) noexcept
    {
        const auto value = static_cast<double>(glue);
        if (glue_observation_count_ == 0u)
        {
            fast_glue_ema_ = value;
            slow_glue_ema_ = value;
        }
        else
        {
            fast_glue_ema_ = fast_glue_ema_ * fast_glue_alpha_ + value * (1.0 - fast_glue_alpha_);
            slow_glue_ema_ = slow_glue_ema_ * slow_glue_alpha_ + value * (1.0 - slow_glue_alpha_);
        }
        ++glue_observation_count_;
        if ((glue_restart_threshold_ != 0.0) && (glue_observation_count_ >= glue_restart_warmup_) &&
            (fast_glue_ema_ > slow_glue_ema_ * glue_restart_threshold_))
        {
            restart_pending_ = true;
        }
    }

    void restart::tick_decision() noexcept
    {
        ++decision_count_;

        if (decision_restart_interval_ == 0u)
            return;

        if (decision_count_ >= next_scheduled_decision_restart_at_)
        {
            restart_pending_ = true;
            next_scheduled_decision_restart_at_ = decision_count_ + next_decision_restart_budget();
        }
    }

    void restart::reset_after_inprocess() noexcept
    {
        if (restart_pending_)
            ++restart_count_;
        restart_pending_ = false;
        conflicts_at_last_restart_ = conflict_count_;

        rebase_schedule();
    }

    counter_t restart::current_restart_budget() const noexcept
    {
        if (restart_interval_ == 0u)
            return 0u;

        if (restart_pending_)
            return 0u;

        if (conflict_count_ >= next_scheduled_restart_at_)
            return 0u;

        return next_scheduled_restart_at_ - conflict_count_;
    }

    void restart::set_restart_interval(const counter_t interval) noexcept
    {
        restart_interval_ = interval;
        restart_sequence_index_ = 0u;
        next_scheduled_restart_at_ = conflict_count_ + restart_interval_;
    }

    void restart::set_decision_restart_interval(const counter_t interval) noexcept
    {
        decision_restart_interval_ = interval;
        decision_restart_sequence_index_ = 0u;
        next_scheduled_decision_restart_at_ = decision_count_ + decision_restart_interval_;
    }

    counter_t restart::current_decision_restart_budget() const noexcept
    {
        if (decision_restart_interval_ == 0u)
            return 0u;

        if (restart_pending_)
            return 0u;

        if (decision_count_ >= next_scheduled_decision_restart_at_)
            return 0u;

        return next_scheduled_decision_restart_at_ - decision_count_;
    }

    counter_t restart::luby_multiplier(counter_t index) noexcept
    {
        static constexpr counter_t max_sequence_exponent {24u};

        counter_t size = 1u;
        counter_t sequence {};
        while ((size < index + 1u) && (sequence < max_sequence_exponent))
        {
            ++sequence;
            size = 2u * size + 1u;
        }
        while ((size - 1u != index) && (size > 1u))
        {
            size = (size - 1u) >> 1u;
            --sequence;
            index = index % size;
        }
        return counter_t {1u} << sequence;
    }

    void restart::rebase_schedule() noexcept
    {
        if (restart_interval_ != 0u)
            next_scheduled_restart_at_ = conflict_count_ + next_restart_budget();
        if (decision_restart_interval_ != 0u)
            next_scheduled_decision_restart_at_ = decision_count_ + decision_restart_interval_;
    }
}
