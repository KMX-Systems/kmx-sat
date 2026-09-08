/// @file inc/kmx/sat/io/writer/kmx_aio_proof.hpp
/// @brief Optional adapter to KMX-AIO for non-blocking output.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <span>
#endif

namespace kmx::sat::io::writer
{
    /// @brief Optional adapter to KMX-AIO for non-blocking output.
    /// @details
    /// This is the optional asynchronous backend `io::proof_output_pipeline` may use for proof output: `open_sink`
    /// establishes a non-blocking output channel through the KMX-AIO/P2300-based asynchronous I/O layer;
    /// `submit_buffer` hands a serialized proof buffer to that channel without blocking the caller; `await_flush`
    /// waits for previously submitted buffers to be durably written; `close_sink` releases the channel. Being a
    /// single concrete type with no runtime-selected alternative, it requires no virtual dispatch or type erasure.
    /// @note KMX-AIO is recommended only for peripheral I/O such as this proof-output path; it must never be a
    /// dependency of the CDCL core, and this component is entirely optional/feature-gated — its absence must not
    /// alter baseline solver behavior.
    class kmx_aio_proof final
    {
    public:
        /// @brief Constructs an adapter with no open sink.
        /// @throws None (noexcept).
        kmx_aio_proof() noexcept = default;

        /// @brief Opens a non-blocking KMX-AIO output channel.
        /// @throws None (noexcept).
        void open_sink() noexcept;

        /// @brief Submits a serialized proof buffer to the channel without blocking.
        /// @param buffer Serialized bytes to submit.
        /// @throws None (noexcept).
        void submit_buffer(const std::span<const std::byte> buffer) noexcept;

        /// @brief Waits for all previously submitted buffers to be durably written.
        /// @throws None (noexcept).
        void await_flush() noexcept
        {
            if (opened_ && !closed_)
                flushed_ = true;
        }

        /// @brief Closes the output channel, releasing its resources.
        /// @throws None (noexcept).
        void close_sink() noexcept
        {
            if (opened_)
                closed_ = true;
        }

        [[nodiscard]] bool opened() const noexcept { return opened_; }

        [[nodiscard]] std::uint32_t submitted_count() const noexcept { return submitted_count_; }

        [[nodiscard]] bool flushed() const noexcept { return flushed_; }

        [[nodiscard]] bool closed() const noexcept { return closed_; }

    private:
        bool opened_ {};
        bool flushed_ {};
        bool closed_ {};
        std::uint32_t submitted_count_ {};
        std::size_t last_buffer_size_ {};
    };
}
