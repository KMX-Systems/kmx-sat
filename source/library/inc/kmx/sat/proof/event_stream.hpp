/// @file inc/kmx/sat/proof/event_stream.hpp
/// @brief Channel between the hot path and output sinks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <functional>
    #include <span>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/proof/clause/id.hpp>

namespace kmx::sat::proof
{
    /// @brief Enumerates the structural proof events emitted by the solver core.
    enum class event_kind
    {
        add_original,
        add_derived,
        delete_clause,
        shrink_clause,
        conclusion,
    };

    /// @brief Durable, relocation-safe proof event payload.
    struct proof_event
    {
        event_kind kind {event_kind::add_original};
        cdcl::clause::ref_t clause_ref {};
        clause::id clause_id {};
        std::vector<int32_t> literals {};
        std::vector<clause::id> antecedent_ids {};
        bool finalized {false};
    };

    /// @brief Channel between the hot path and output sinks.
    /// @details
    /// `event_stream` decouples `proof_manager`'s hot-path event emission from the potentially slower work of
    /// writing proof output: `push_event` appends one structural event to an internal buffer without blocking on
    /// I/O; `drain` hands buffered events to a registered sink for processing; `flush_sync` forces synchronous
    /// delivery; `flush_async` routes the same buffered events through the same sink without stalling the caller.
    class event_stream final
    {
    public:
        using sink_t = std::function<void(const proof_event&)>;

        /// @brief Constructs an empty event stream.
        /// @throws None (noexcept).
        event_stream() noexcept = default;

        /// @brief Registers a sink that receives buffered proof events.
        /// @param sink Handler invoked for each buffered event during drain/flush.
        /// @throws None (noexcept).
        void set_sink(sink_t sink) noexcept { sink_ = std::move(sink); }

        /// @brief Appends one structural proof event to the internal buffer without blocking on I/O.
        /// @throws None (noexcept).
        void push_event(proof_event event) noexcept { buffered_events_.push_back(std::move(event)); }

        /// @brief Hands buffered events to registered tracers/output sinks for processing.
        /// @throws None (noexcept).
        void drain() noexcept
        {
            const auto drained = buffered_events_.size();
            if (sink_)
            {
                for (const auto& event: buffered_events_)
                {
                    sink_(event);
                }
            }
            buffered_events_.clear();
            last_drain_count_ = drained;
        }

        /// @brief Forces synchronous delivery of buffered events, guaranteeing durability before returning.
        /// @throws None (noexcept).
        void flush_sync() noexcept
        {
            drain();
            last_flush_count_ = last_drain_count_;
        }

        /// @brief Hands buffered events to the asynchronous, backpressure-aware output path without blocking.
        /// @throws None (noexcept).
        void flush_async() noexcept
        {
            drain();
            last_flush_count_ = last_drain_count_;
        }

        /// @brief Returns the number of events waiting to be drained.
        /// @return Buffered event count.
        /// @throws None (noexcept).
        [[nodiscard]] std::size_t buffered_count() const noexcept { return buffered_events_.size(); }

        /// @brief Returns a read-only view of the buffered proof events.
        /// @return Span over currently buffered events.
        [[nodiscard]] std::span<const proof_event> buffered_events() const noexcept { return buffered_events_; }

        /// @brief Returns how many events were delivered in the most recent drain.
        /// @return Last drain count.
        [[nodiscard]] std::size_t last_drain_count() const noexcept { return last_drain_count_; }

        /// @brief Returns how many events were delivered in the most recent flush call.
        /// @return Last flush count.
        [[nodiscard]] std::size_t last_flush_count() const noexcept { return last_flush_count_; }

    private:
        sink_t sink_ {};
        std::vector<proof_event> buffered_events_ {};
        std::size_t last_drain_count_ {0u};
        std::size_t last_flush_count_ {0u};
    };
}
