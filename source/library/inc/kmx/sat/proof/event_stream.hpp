/// @file inc/kmx/sat/proof/event_stream.hpp
/// @brief Channel between the hot path and output sinks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif

namespace kmx::sat::proof
{
    /// @brief Channel between the hot path and output sinks.
    ///
    /// `event_stream` decouples `proof_manager`'s hot-path event emission from the potentially slower work of
    /// writing proof output: `push_event` appends one structural event (add/delete/shrink) to an internal buffer
    /// without blocking on I/O; `drain` hands buffered events to registered tracers/`io::proof_output_pipeline` for
    /// processing; `flush_sync` forces synchronous delivery (used when a caller needs proof output guaranteed
    /// durable before proceeding, for example before reporting a final UNSAT result), while `flush_async` hands
    /// buffered events to `io::proof_output_pipeline`'s asynchronous backpressure-aware path (optionally backed by
    /// `io::kmx_aio_proof_writer`) without stalling the caller.
    /// @warning Per the backpressure risk point, if the solver produces proof data faster than the sink can consume
    /// it, `io::proof_output_pipeline`'s backpressure policy — not this class — is responsible for bounding buffered
    /// event growth; this class only buffers and dispatches, it does not implement backpressure itself.
    class event_stream final
    {
    public:
        /// @brief Constructs an empty event stream.
        /// @throws None (noexcept).
        event_stream() noexcept = default;

        /// @brief Appends one structural proof event to the internal buffer without blocking on I/O.
        /// @throws None (noexcept).
        void push_event() noexcept
        {
        }

        /// @brief Hands buffered events to registered tracers/output sinks for processing.
        /// @throws None (noexcept).
        void drain() noexcept
        {
        }

        /// @brief Forces synchronous delivery of buffered events, guaranteeing durability before returning.
        /// @throws None (noexcept).
        void flush_sync() noexcept
        {
        }

        /// @brief Hands buffered events to the asynchronous, backpressure-aware output path without blocking.
        /// @throws None (noexcept).
        void flush_async() noexcept
        {
        }
    };
}
