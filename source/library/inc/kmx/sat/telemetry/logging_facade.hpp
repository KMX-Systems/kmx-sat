/// @file inc/kmx/sat/telemetry/logging_facade.hpp
/// @brief Optionally compiled logging that does not contaminate the hot path in non-logging builds.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <string_view>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::telemetry
{
    /// @brief Optionally compiled logging that does not contaminate the hot path in non-logging builds.
    ///
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

        /// @brief Logs a clause-related event (for example addition, deletion, or shrink).
        /// @param ref Reference to the clause being logged.
        /// @throws None (noexcept).
        void log_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Logs a literal-related event (for example assignment or watch change).
        /// @param lit Literal being logged.
        /// @throws None (noexcept).
        void log_literal(const literal lit) noexcept
        {
        }

        /// @brief Logs a gate-extraction event from `extractor::gate`.
        /// @throws None (noexcept).
        void log_gate() noexcept
        {
        }

        /// @brief Logs an extension-stack event from `stack::extension`.
        /// @throws None (noexcept).
        void log_extension() noexcept
        {
        }

        /// @brief Logs a summary line for a completed phase.
        /// @param phase_name Identifier of the phase that just completed.
        /// @throws None (noexcept).
        void log_phase_summary(const std::string_view phase_name) noexcept
        {
        }
    };
}
