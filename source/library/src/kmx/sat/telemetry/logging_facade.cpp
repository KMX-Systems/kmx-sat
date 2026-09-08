/// @file library/src/kmx/sat/telemetry/logging_facade.cpp
/// @brief Out-of-line definitions declared by kmx/sat/telemetry/logging_facade.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/telemetry/logging_facade.hpp>

namespace kmx::sat::telemetry
{
    void logging_facade::log_literal(const literal lit) noexcept
    {
        events_.push_back({event_kind::literal, last_ref_offset_, {}, lit});
        last_literal_index_ = events_.size() - 1u;
        has_last_literal_ = true;
    }

    const logging_facade::event& logging_facade::last_event() const noexcept
    {
        if (events_.empty())
        {
            static const event empty_event {};
            return empty_event;
        }
        return events_.back();
    }

    const logging_facade::event& logging_facade::last_literal() const noexcept
    {
        if (has_last_literal_)
            return events_[last_literal_index_];
        return last_event();
    }
}
