/// @file library/src/kmx/sat/io/proof_output_pipeline.cpp
/// @brief Out-of-line definitions declared by kmx/sat/io/proof_output_pipeline.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/io/proof_output_pipeline.hpp>

namespace kmx::sat::io
{
    void proof_output_pipeline::submit() noexcept
    {
        if (!active_)
            return;
        ++submitted_count_;
        const auto buffered = event_stream_.buffered_count();
        if (buffered > 0u)
            submitted_count_ += buffered;
        event_stream_.drain();
    }

    void proof_output_pipeline::submit(const proof::event_stream& stream) noexcept
    {
        if (!active_)
            return;
        ++submitted_count_;
        submitted_count_ += stream.buffered_count();
        if (backpressure_policy_enabled_)
            submitted_count_ += 1u;
    }
}
