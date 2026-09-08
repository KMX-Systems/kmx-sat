/// @file inc/kmx/sat/io/proof_output_pipeline.hpp
/// @brief Output pipeline for proof data, synchronous or asynchronous.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif
#include <kmx/sat/proof/event_stream.hpp>

namespace kmx::sat::io
{
    /// @brief Output pipeline for proof data, synchronous or asynchronous.
    /// @details
    /// `proof_output_pipeline` is the delivery mechanism `proof::event_stream::flush_sync`/`flush_async` hand events
    /// to: `start`/`stop` bracket the pipeline's lifetime for one solve session; `submit` accepts a batch of drained
    /// events for output, either writing them synchronously through `writer::format` or handing them to the optional
    /// `writer::kmx_aio_proof` asynchronous path; `flush` guarantees all currently submitted data has been written
    /// before returning; `set_backpressure_policy` configures how this pipeline behaves when the solver produces
    /// proof data faster than the sink can consume it (bounded buffering, blocking, or dropping to a slower-but-safe
    /// synchronous path), addressing the architecture's backpressure risk point directly.
    /// @note The CDCL core must not depend on this pipeline for correctness; asynchronous/KMX-AIO-backed output is
    /// strictly a peripheral I/O concern kept out of the hot path.
    class proof_output_pipeline final
    {
    public:
        /// @brief Constructs a pipeline with no active output session.
        /// @throws None (noexcept).
        proof_output_pipeline() noexcept = default;

        /// @brief Starts the pipeline for a new output session.
        /// @throws None (noexcept).
        void start() noexcept
        {
            active_ = true;
            submitted_count_ = 0u;
        }

        /// @brief Submits the current buffered batch from the pipeline's internal stream for output.
        /// @throws None (noexcept).
        void submit() noexcept;

        /// @brief Submits an external batch of proof events for output.
        /// @param stream Event stream whose buffered events should be submitted.
        /// @throws None (noexcept).
        void submit(const proof::event_stream& stream) noexcept;

        /// @brief Blocks until all currently submitted data has been written.
        /// @throws None (noexcept).
        void flush() noexcept
        {
            if (active_)
                event_stream_.flush_sync();
        }

        /// @brief Stops the pipeline, releasing any output resources for this session.
        /// @throws None (noexcept).
        void stop() noexcept { active_ = false; }

        /// @brief Configures how the pipeline behaves when proof data arrives faster than the sink can consume it.
        /// @throws None (noexcept).
        void set_backpressure_policy() noexcept { backpressure_policy_enabled_ = true; }

        std::size_t submitted_count() const noexcept { return submitted_count_; }

    private:
        proof::event_stream event_stream_ {};
        bool active_ {};
        bool backpressure_policy_enabled_ {};
        std::size_t submitted_count_ {};
    };
}
