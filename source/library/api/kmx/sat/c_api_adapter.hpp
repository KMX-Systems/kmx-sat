/// @file api/kmx/sat/c_api_adapter.hpp
/// @brief C compatibility and competition-facing API support bridging IPASIR and IPASIR-UP.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <optional>
    #include <string_view>
#endif
#include <kmx/sat/solver.hpp>

namespace kmx::sat
{
    /// @brief C compatibility and competition-facing API support bridging IPASIR and IPASIR-UP.
    ///
    /// This adapter wraps one owned `solver` instance and re-expresses its operations under IPASIR's signed-integer
    /// literal encoding and status-code conventions (`10` = SAT, `20` = UNSAT, `0` = unknown, matching the IPASIR
    /// convention), so that SAT-competition harnesses and other IPASIR/IPASIR-UP-based tooling can drive the solver
    /// without depending on the native C++ surface. Its own compatibility guarantees (literal encoding, status codes,
    /// external-propagator callback shape) follow the IPASIR/IPASIR-UP specifications independently of the semantic
    /// versioning applied to the native `solver`/`solve_request`/`solve_result` surface.
    /// @reference IPASIR: "The Re-entrant Incremental Satisfiability Application Program Interface" (Balyo, Biere,
    /// Iser), and its IPASIR-UP extension for user-propagated external constraints.
    class c_api_adapter final
    {
    public:
        /// @brief Constructs a C API adapter with an embedded solver facade.
        /// @throws None (noexcept).
        c_api_adapter() noexcept;
        /// @brief Destroys the adapter and releases all owned resources.
        /// @throws None (noexcept).
        ~c_api_adapter() noexcept;
        /// @brief Copy construction is disabled to preserve unique adapter ownership.
        /// @throws None (noexcept).
        c_api_adapter(const c_api_adapter&) = delete;
        /// @brief Copy assignment is disabled to preserve unique adapter ownership.
        /// @return Reference to this object (unreachable because assignment is deleted).
        /// @throws None (noexcept).
        c_api_adapter& operator=(const c_api_adapter&) = delete;

        /// @brief Initializes the IPASIR-style solving session.
        /// @throws None (noexcept).
        void ipasir_init() noexcept;
        /// @brief Adds one IPASIR literal token to the input stream.
        /// @param lit Signed literal value in IPASIR encoding.
        /// @throws None (noexcept).
        void ipasir_add(const std::int32_t lit) noexcept;
        /// @brief Adds one IPASIR assumption literal for the next solve call.
        /// @param lit Signed assumption literal in IPASIR encoding.
        /// @throws None (noexcept).
        void ipasir_assume(const std::int32_t lit) noexcept;
        /// @brief Runs the solver using IPASIR semantics.
        /// @return IPASIR status code representing SAT, UNSAT, or UNKNOWN/terminated.
        /// @throws None (noexcept).
        int ipasir_solve() noexcept;
        /// @brief Queries the assignment of a literal after a satisfiable result.
        /// @param lit Signed literal requested by the caller.
        /// @return Signed literal indicating truth assignment per IPASIR conventions.
        /// @throws None (noexcept).
        std::int32_t ipasir_val(const std::int32_t lit) const noexcept;
        /// @brief Checks whether an assumption literal is in the failed core.
        /// @param lit Signed assumption literal requested by the caller.
        /// @return True if the literal is marked as failed.
        /// @throws None (noexcept).
        bool ipasir_failed(const std::int32_t lit) const noexcept;

        /// @brief Sets one persisted option override through the wrapped solver facade.
        /// @param name Option identifier.
        /// @param value Option value.
        /// @throws None (noexcept).
        void ipasir_set_option(std::string_view name, std::int64_t value) noexcept;

        /// @brief Applies a named persisted configuration profile through the wrapped solver facade.
        /// @param profile_name Configuration profile identifier.
        /// @throws None (noexcept).
        void ipasir_set_configuration(std::string_view profile_name) noexcept;

        /// @brief Clears persisted configuration overrides through the wrapped solver facade.
        /// @throws None (noexcept).
        void ipasir_clear_persisted_configuration() noexcept;

        /// @brief Returns whether the wrapped solver core currently marks a persisted option subset.
        /// @return True if a persisted option subset marker is set.
        /// @throws None (noexcept).
        bool ipasir_persisted_option_subset() const noexcept;

        /// @brief Returns whether any persisted configuration override is currently present.
        /// @return True if at least one persisted option/profile override is configured.
        /// @throws None (noexcept).
        bool ipasir_has_persisted_configuration() const noexcept;

        /// @brief Returns the configured default conflict limit or `-1` when unset.
        /// @return Configured conflict limit, or `-1` when no override exists.
        /// @throws None (noexcept).
        std::int64_t ipasir_configured_conflict_limit() const noexcept;

        /// @brief Returns the configured default decision limit or `-1` when unset.
        /// @return Configured decision limit, or `-1` when no override exists.
        /// @throws None (noexcept).
        std::int64_t ipasir_configured_decision_limit() const noexcept;

        /// @brief Returns the configured strict-mode override (`1`/`0`) or `-1` when unset.
        /// @return `1` for strict, `0` for non-strict, `-1` when unset.
        /// @throws None (noexcept).
        std::int32_t ipasir_configured_strict_mode() const noexcept;

        /// @brief Returns the current configuration profile name if set, else empty.
        /// @return Current persisted configuration profile name.
        /// @throws None (noexcept).
        std::string_view ipasir_configuration_profile_name() const noexcept;

        /// @brief Enables or disables verbose statistics reporting mode.
        /// @param verbose Non-zero enables verbose mode; zero enables compact mode.
        /// @throws None (noexcept).
        void ipasir_set_statistics_verbose_reporting(std::int32_t verbose) noexcept;

        /// @brief Returns whether verbose statistics reporting mode is currently enabled.
        /// @return `1` when verbose mode is enabled, else `0`.
        /// @throws None (noexcept).
        std::int32_t ipasir_statistics_verbose_reporting() const noexcept;

        /// @brief Returns the formatted statistics report line.
        /// @return Statistics report line in the current detail mode.
        /// @throws None.
        std::string ipasir_statistics_report_line() const;

        /// @brief Returns the count of emitted solve-checkpoint statistics report lines.
        /// @return Number of emitted checkpoint report lines retained by the facade.
        /// @throws None (noexcept).
        std::size_t ipasir_statistics_report_emission_count() const noexcept;

        /// @brief Returns the latest emitted solve-checkpoint statistics report line.
        /// @return Empty string view when no checkpoint line has been emitted.
        /// @throws None (noexcept).
        std::string_view ipasir_last_emitted_statistics_report_line() const noexcept;

        /// @brief Returns the previous emitted solve-checkpoint statistics report line.
        /// @return Empty string view when fewer than two checkpoint lines have been emitted.
        /// @throws None (noexcept).
        std::string_view ipasir_previous_emitted_statistics_report_line() const noexcept;

        /// @brief Returns the latest emitted solve-checkpoint statistics snapshot.
        /// @return Latest emitted checkpoint snapshot, or `std::nullopt` when unavailable.
        /// @throws None (noexcept).
        std::optional<telemetry::solver_statistics::snapshot> ipasir_last_emitted_statistics_snapshot() const noexcept;

        /// @brief Returns the previous emitted solve-checkpoint statistics snapshot.
        /// @return Previous emitted checkpoint snapshot, or `std::nullopt` when unavailable.
        /// @throws None (noexcept).
        std::optional<telemetry::solver_statistics::snapshot> ipasir_previous_emitted_statistics_snapshot() const noexcept;

        /// @brief Returns whether the last two emitted checkpoint snapshots are monotonic.
        /// @return `1` when monotonic (or insufficient samples), else `0`.
        /// @throws None (noexcept).
        std::int32_t ipasir_statistics_emission_tail_monotonic() const noexcept;

        /// @brief Returns the non-negative per-counter delta between previous and latest emitted checkpoint snapshots.
        /// @return Snapshot delta, or `std::nullopt` when fewer than two snapshots exist.
        /// @throws None (noexcept).
        std::optional<telemetry::solver_statistics::snapshot> ipasir_statistics_emission_tail_delta() const noexcept;

    private:
        solver solver_ {};
    };
}
