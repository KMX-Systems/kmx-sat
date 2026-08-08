/// @file api/kmx/sat/c_api_adapter.hpp
/// @brief C compatibility and competition-facing API support bridging IPASIR and IPASIR-UP.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
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

    private:
        solver solver_ {};
    };
}
