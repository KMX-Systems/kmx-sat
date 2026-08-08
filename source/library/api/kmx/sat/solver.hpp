/// @file api/kmx/sat/solver.hpp
/// @brief The single C++ API facade; coordinates solver state, validates boundaries, and routes operations to the internal subsystems.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <functional>
    #include <optional>
    #include <span>
    #include <string_view>
#endif
#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/solve_result.hpp>
#include <kmx/sat/telemetry/solver_statistics.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::proof::tracer
{
    class view;
}

namespace kmx::sat
{
    /// @brief The single C++ API facade; coordinates solver state, validates boundaries, and routes operations to the
    /// internal subsystems.
    ///
    /// `solver` is the sole public entry point of the library (namespace `kmx::sat`) and the composition root that a
    /// caller sees; internally it forwards to `solver_state_machine`, `external_frontend`, `search_coordinator`
    /// (through `solver_core`), `telemetry::solver_statistics`, and `proof::proof_manager`, none of which are exposed
    /// directly. The class owns a private `impl` behind a pointer (pImpl) so that the internal types listed above never
    /// become part of the stable ABI; only `solver`, `solve_request`, `solve_result`, `telemetry::solver_options`, and
    /// `telemetry::solver_statistics` are covered by semantic-versioning guarantees.
    ///
    /// The lifecycle enforced by `solver_state_machine` gates which methods may run in which order: literals and
    /// clauses may only be added while `adding`/`configuring`, `solve` transitions through `solving` into `sat`,
    /// `unsat`, or `steady` (terminated/unknown), and any internally detected inconsistency moves the instance into
    /// `error`, after which only `reset_session` and read-only diagnostics remain legal per the error-recovery policy.
    /// `reset_session` is the only supported recovery path and must fully reset the clause database, assignment
    /// store, extension stack, and proof epoch state without reusing arena memory left in an inconsistent state.
    ///
    /// @note Two `solver` instances built with identical clauses, assumptions, option values, and
    /// `telemetry::solver_options`-controlled random seed must produce identical status, identical model/failed-core
    /// output, and identical proof byte streams for a fixed build configuration; this determinism guarantee applies
    /// only to single-threaded, single-engine use.
    /// @warning A single `solver` instance is not thread-safe. Concurrent calls into the same instance from multiple
    /// threads are undefined behavior; only independent instances (as used by a portfolio controller) may run
    /// concurrently, each with its own instance.
    class solver final
    {
    public:
        using terminate_callback_t = std::function<bool()>;
        using learn_callback_t = std::function<void(std::span<const literal>)>;
        using external_propagator_hook_t = std::function<void()>;

        /// @brief Constructs a solver facade with an initialized internal implementation.
        /// @throws None (noexcept).
        solver() noexcept;
        /// @brief Destroys the solver and releases all owned internal resources.
        /// @throws None (noexcept).
        ~solver() noexcept;
        /// @brief Copy construction is disabled to preserve unique internal ownership.
        /// @throws None (noexcept).
        solver(const solver&) = delete;
        /// @brief Copy assignment is disabled to preserve unique internal ownership.
        /// @return Reference to this object (unreachable because assignment is deleted).
        /// @throws None (noexcept).
        solver& operator=(const solver&) = delete;
        /// @brief Move-constructs a solver and transfers ownership of the internal implementation.
        /// @throws None (noexcept).
        solver(solver&&) noexcept;
        /// @brief Move-assigns a solver and transfers ownership of the internal implementation.
        /// @return Reference to this solver after the move assignment.
        /// @throws None (noexcept).
        solver& operator=(solver&&) noexcept;

        /// @brief Reserves capacity for at least the specified number of variables in the current session.
        /// @param variable_count Number of variables to preallocate internal storage for.
        /// @throws None (noexcept).
        void reserve(const variable::index_t variable_count) noexcept;
        /// @brief Adds one literal token to the incremental clause input stream.
        /// @param lit Literal value to append to the currently assembled clause representation.
        /// @throws None (noexcept).
        void add_literal(const literal lit) noexcept;
        /// @brief Adds a full clause in one call.
        /// @param clause Read-only span containing the clause literals.
        /// @throws None (noexcept).
        void add_clause(const std::span<const literal> clause) noexcept;
        /// @brief Registers one assumption literal for the next solve episode.
        /// @param lit Assumption literal to activate for the next call to solve.
        /// @throws None (noexcept).
        void assume(const literal lit) noexcept;
        /// @brief Executes one solve episode under the provided request limits and options.
        /// @param request Immutable solve configuration containing assumptions, limits, and mode flags.
        /// @return Aggregated solve outcome containing status, optional model/core views, statistics, and proof summary.
        /// @throws May propagate implementation-defined exceptions from dependent operations.
        solve_result solve(const solve_request& request);
        /// @brief Queries the truth value of an external variable in the current model view.
        /// @param var Variable to inspect.
        /// @return `std::nullopt` if unavailable, otherwise the assigned boolean value.
        /// @throws None (noexcept).
        std::optional<bool> value_of(const variable var) const noexcept;
        /// @brief Checks whether an assumption literal belongs to the failed core of the last UNSAT call.
        /// @param lit Assumption literal to test.
        /// @return True if the literal is marked failed in the latest failed-core extraction.
        /// @throws None (noexcept).
        bool failed(const literal lit) const noexcept;
        /// @brief Sets one numeric solver option by symbolic name.
        /// @param name Option identifier.
        /// @param value Option value to assign.
        /// @throws None (noexcept).
        void set_option(const std::string_view name, const std::int64_t value) noexcept;
        /// @brief Applies a predefined option profile.
        /// @param profile_name Configuration/profile name to load.
        /// @throws None (noexcept).
        void set_configuration(const std::string_view profile_name) noexcept;
        /// @brief Attaches a proof tracer sink to receive proof events.
        /// @param sink Concrete tracer view object used as an event sink.
        /// @throws None (noexcept).
        void attach_proof_sink(proof::tracer::view& sink) noexcept;
        /// @brief Registers a termination callback polled by the solve loop.
        /// @param callback User callback returning true when solving should stop.
        /// @throws None (noexcept).
        void set_terminate(terminate_callback_t callback) noexcept;
        /// @brief Registers a learned-clause callback.
        /// @param callback User callback invoked with learned clause literals.
        /// @throws None (noexcept).
        void set_learn(learn_callback_t callback) noexcept;
        /// @brief Registers an external propagator hook for integration workflows.
        /// @param hook Callback entry point for external propagation interactions.
        /// @throws None (noexcept).
        void set_external_propagator(external_propagator_hook_t hook) noexcept;
        /// @brief Returns a snapshot of currently collected solver statistics.
        /// @return Immutable statistics snapshot.
        /// @throws None (noexcept).
        telemetry::solver_statistics::snapshot statistics() const noexcept;
        /// @brief Resets the current incremental session state.
        /// @throws None (noexcept).
        void reset_session() noexcept;
        /// @brief Clears assumptions retained for incremental solving.
        /// @throws None (noexcept).
        void release_incremental_assumptions() noexcept;

    private:
        class impl;
        impl* impl_ {};
    };
}
