/// @file library/src/kmx/sat/proof/event_stream.cpp
/// @brief Out-of-line definitions declared by kmx/sat/proof/event_stream.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/proof/event_stream.hpp>

namespace kmx::sat::proof
{
    void event_stream::drain() noexcept
    {
        const auto drained = buffered_events_.size();
        if (sink_)
            for (const auto& event: buffered_events_)
                sink_(event);
        buffered_events_.clear();
        last_drain_count_ = drained;
    }
}
