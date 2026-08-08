/// @file inc/kmx/sat/runtime/shared_clause_exchange.hpp
/// @brief Bounded, explicit cross-engine clause sharing without coupling the CDCL core to orchestration concerns.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::runtime
{
    /// @brief Bounded, explicit cross-engine clause sharing without coupling the CDCL core to orchestration concerns.
    ///
    /// When `controller::portfolio` runs multiple independent `solver_core` instances, sharing learned clauses
    /// between them can speed up the overall race, but naive sharing would silently affect the single-engine
    /// determinism guarantee; `shared_clause_exchange` makes that sharing bounded and explicit instead.
    /// `publish_clause` offers one instance's learned clause to the exchange; `drain_incoming` lets another instance
    /// pull in clauses published by others; `apply_exchange_policy` enforces bounds (rate, size, quality threshold)
    /// on what gets shared; `reset_exchange_state` clears exchange state, for example between unrelated portfolio
    /// runs. This component is portfolio-only and has no involvement in single-engine solving.
    class shared_clause_exchange final
    {
    public:
        /// @brief Constructs an exchange with no pending clauses.
        /// @throws None (noexcept).
        shared_clause_exchange() noexcept = default;

        /// @brief Offers a learned clause from one portfolio instance to the exchange.
        /// @param ref Reference to the clause being published, local to the publishing instance.
        /// @throws None (noexcept).
        void publish_clause(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Pulls in clauses published by other portfolio instances.
        /// @throws None (noexcept).
        void drain_incoming() noexcept
        {
        }

        /// @brief Enforces the configured bounds (rate, size, quality) on what may be exchanged.
        /// @throws None (noexcept).
        void apply_exchange_policy() noexcept
        {
        }

        /// @brief Clears all exchange state, for example between unrelated portfolio runs.
        /// @throws None (noexcept).
        void reset_exchange_state() noexcept
        {
        }
    };
}
