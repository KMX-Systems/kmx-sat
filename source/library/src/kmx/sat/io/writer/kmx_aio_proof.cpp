/// @file library/src/kmx/sat/io/writer/kmx_aio_proof.cpp
/// @brief Out-of-line definitions declared by kmx/sat/io/writer/kmx_aio_proof.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/io/writer/kmx_aio_proof.hpp>

namespace kmx::sat::io::writer
{
    void kmx_aio_proof::open_sink() noexcept
    {
        opened_ = true;
        closed_ = false;
        flushed_ = false;
    }

    void kmx_aio_proof::submit_buffer(const std::span<const std::byte> buffer) noexcept
    {
        if (!opened_ || closed_)
            return;
        submitted_count_ += 1u;
        last_buffer_size_ = buffer.size();
    }
}
