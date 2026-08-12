/// @file inc/kmx/sat/cdcl/store/clause_cold.hpp
/// @brief Delta/varint-compressed storage for rarely accessed redundant clauses, shrinking the resident set of the
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::cdcl::store
{
    /// @brief Delta/varint-compressed storage for rarely accessed redundant clauses, shrinking the resident set of the
    /// active clause database without discarding potentially useful clauses. Research-track optimization.
    ///
    /// Neither CaDiCaL nor Kissat compresses cold learned clauses; this component targets that gap by allowing
    /// `reduce_controller`/`memory_governor` to `demote_to_cold` a redundant clause that has not been used recently
    /// instead of deleting it outright, storing it delta/varint-encoded to shrink the arena's resident set.
    /// `decode_on_access` transparently decompresses a clause the rare time it is touched again (for example if it
    /// becomes a propagation reason once more), and `promote_from_cold` moves it back into ordinary `bank::arena`
    /// storage for regular hot-path access. `cold_footprint_bytes` reports the compressed footprint to
    /// `memory_governor` for budget accounting.
    /// @warning Decompression must never occur on the `propagator::propagate` hot path; only cold, rarely-taken
    /// access paths may call `decode_on_access`. This component is research-track and must clear the comparative
    /// benchmarking harness before being enabled by default.
    class clause_cold final
    {
    public:
        /// @brief Constructs a cold clause store with no compressed clauses.
        /// @throws None (noexcept).
        clause_cold() noexcept = default;

        /// @brief Compresses a redundant clause and moves it out of the hot arena into cold storage.
        /// @param ref Reference to the clause to demote.
        /// @throws None (noexcept).
        void demote_to_cold(const clause::ref_t ref) noexcept {}

        /// @brief Decompresses a cold clause and reinstates it in ordinary arena storage.
        /// @param ref Reference to the cold clause to promote.
        /// @return Reference to the clause's new location in ordinary arena storage.
        /// @throws None (noexcept).
        clause::ref_t promote_from_cold(const clause::ref_t ref) noexcept { return ref; }

        /// @brief Decompresses a cold clause transparently for one-off access without permanently promoting it.
        /// @param ref Reference to the cold clause being accessed.
        /// @throws None (noexcept).
        void decode_on_access(const clause::ref_t ref) noexcept {}

        /// @brief Returns the total compressed footprint of all clauses currently held in cold storage.
        /// @return Footprint in bytes.
        /// @throws None (noexcept).
        std::size_t cold_footprint_bytes() const noexcept { return {}; }
    };
}
