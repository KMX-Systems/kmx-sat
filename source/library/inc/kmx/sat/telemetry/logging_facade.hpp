/// @file inc/kmx/sat/telemetry/logging_facade.hpp
/// @brief Optionally compiled logging that does not contaminate the hot path in non-logging builds.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <optional>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/telemetry/phase_id.hpp>

namespace kmx::sat::telemetry
{
    /// @brief Optionally compiled logging that does not contaminate the hot path in non-logging builds.
    /// @details
    /// `logging_facade` is the structured-tracing counterpart to `kmx::logger`'s free-function logging: it exposes
    /// domain-specific log points (`log_clause`, `log_literal`, `log_gate`, `log_extension`, `log_phase_summary`) so
    /// call sites in `clause::database`, `extractor::gate`, `stack::extension`, and phase-boundary code can log
    /// intent-revealing events without formatting details at every call site. In non-logging builds these calls are
    /// expected to compile away entirely (for example behind a compile-time flag), so this facade never adds
    /// overhead to `propagator`/`conflict_analyzer`'s hot loops when logging is disabled.
    /// @note Being a single concrete type with no runtime-selected alternative, this class requires neither virtual
    /// dispatch nor type erasure.
    class logging_facade final
    {
    public:
        /// @brief Constructs a logging facade with default (implementation-defined) verbosity.
        /// @throws None (noexcept).
        logging_facade() noexcept = default;

        enum class event_kind
        {
            clause,
            literal,
            gate,
            extension,
            phase_summary,
        };

        struct event
        {
            event_kind kind {};
            std::uint64_t ref_offset {};
            /// @brief Phase this event belongs to; empty for events that are not phase summaries.
            std::optional<phase_id> phase {};
            literal literal_value {};
        };

        /// @brief Logs a clause-related event (for example addition, deletion, or shrink).
        /// @param ref Reference to the clause being logged.
        /// @throws None (noexcept).
        void log_clause(const cdcl::clause::ref_t ref) noexcept
        {
            last_ref_offset_ = ref.offset();
            events_.push_back({event_kind::clause, last_ref_offset_});
        }

        /// @brief Logs a literal-related event (for example assignment or watch change).
        /// @param lit Literal being logged.
        /// @throws None (noexcept).
        void log_literal(const literal lit) noexcept;

        /// @brief Logs a gate-extraction event from `extractor::gate`.
        /// @throws None (noexcept).
        void log_gate() noexcept { events_.push_back({event_kind::gate, last_ref_offset_}); }

        /// @brief Logs an extension-stack event from `stack::extension`.
        /// @throws None (noexcept).
        void log_extension() noexcept { events_.push_back({event_kind::extension, last_ref_offset_}); }

        /// @brief Logs a summary line for a completed phase.
        /// @param id Phase that just completed.
        /// @throws None (noexcept).
        void log_phase_summary(const phase_id id) noexcept { events_.push_back({event_kind::phase_summary, last_ref_offset_, id}); }

        std::size_t event_count() const noexcept { return events_.size(); }

        const event& last_event() const noexcept;

        const event& last_literal() const noexcept;

        std::uint64_t last_clause_ref() const noexcept { return last_ref_offset_; }

    private:
        std::vector<event> events_ {};
        std::uint64_t last_ref_offset_ {};
        std::size_t last_literal_index_ {};
        bool has_last_literal_ {};
    };
}
