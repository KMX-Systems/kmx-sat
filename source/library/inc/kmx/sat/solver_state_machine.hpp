/// @file inc/kmx/sat/solver_state_machine.hpp
/// @brief Models legal API transitions instead of relying on historical bit masks.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif

namespace kmx::sat
{
    /// @brief Models legal API transitions instead of relying on historical bit masks.
    ///
    /// Historically, solvers in the CaDiCaL/Kissat family track legal API usage with ad hoc boolean/bitmask fields
    /// (for example "clause adding allowed", "currently solving") checked ad hoc at each call site. This type
    /// replaces that pattern with one explicit `state` value so that `solver` can express its lifecycle contract
    /// ("add only while configuring/adding", "solve only once per episode", "no mutation after an unrecoverable
    /// error") as a single small state machine instead of scattered flags.
    ///
    /// `error` is a terminal state reached only through an explicit `to(state::error)` call from a caller that has
    /// detected a `state_error`, `domain_error`, or `consistency_error`; per the library's error-recovery semantics,
    /// once in `error` every mutating operation must be rejected by the caller except a full `reset_session`, which is
    /// expected to transition the machine back to `configuring` only after clause database, assignment store,
    /// extension stack, and proof epoch state have all been reset consistently.
    ///
    /// @note This type intentionally has no dependency on `solver_core` or any other subsystem; it only tracks the
    /// `state` value itself; enforcing the transition legality (which callers may call which methods) is the
    /// responsibility of `solver`/`external_frontend`, not of this class.
    class solver_state_machine final
    {
    public:
        /// @brief Enumerates every legal lifecycle state of one incremental solving session.
        enum class state
        {
            /// @brief Initial state: options/configuration profile may be set, no clauses added yet.
            configuring,
            /// @brief Clauses and assumptions may be appended; no search is currently running.
            adding,
            /// @brief A `solve` episode is actively running inside `search_coordinator`.
            solving,
            /// @brief The most recent `solve` episode terminated with a satisfiable result.
            sat,
            /// @brief The most recent `solve` episode terminated with an unsatisfiable result.
            unsat,
            /// @brief The most recent `solve` episode terminated without a definite result (unknown/terminated).
            steady,
            /// @brief Terminal error state; only `reset_session` and read-only diagnostics remain legal.
            error
        };

        /// @brief Constructs the state machine in the configuring state.
        /// @throws None (noexcept).
        solver_state_machine() noexcept = default;

        /// @brief Returns the current lifecycle state.
        /// @return Current machine state value.
        /// @throws None (noexcept).
        state current_state() const noexcept
        {
            return state_;
        }

        /// @brief Checks whether the machine is in configuring state.
        /// @return True if the current state is configuring.
        /// @throws None (noexcept).
        bool expect_configuring() const noexcept
        {
            return state_ == state::configuring;
        }

        /// @brief Checks whether the machine is in adding state.
        /// @return True if the current state is adding.
        /// @throws None (noexcept).
        bool expect_adding() const noexcept
        {
            return state_ == state::adding;
        }

        /// @brief Checks whether the machine is in solving state.
        /// @return True if the current state is solving.
        /// @throws None (noexcept).
        bool expect_solving() const noexcept
        {
            return state_ == state::solving;
        }

        /// @brief Transitions the machine to the requested state.
        /// @param state Target state to set.
        /// @throws None (noexcept).
        void to(const state state) noexcept
        {
            state_ = state;
        }

    private:
        state state_ {state::configuring};
    };
}
