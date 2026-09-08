/// @file inc/kmx/sat/cdcl/store/clause_cold.hpp
/// @brief Delta/varint-compressed storage for rarely accessed redundant clauses, shrinking the resident set of the
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <limits>
    #include <span>
    #include <unordered_map>
    #include <unordered_set>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::store
{
    /// @brief Rarely-touched clause payloads, keyed by arena offset.
    using cold_payload_map_t = std::unordered_map<clause::ref_t::offset_t, std::vector<std::uint8_t>>;

    /// @brief Delta/varint-compressed storage for rarely accessed redundant clauses, shrinking the resident set of the
    /// active clause database without discarding potentially useful clauses. Research-track optimization.
    /// @details
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

        /// @brief Enables the research-track cold path explicitly; disabled by default.
        void set_enabled(const bool enabled) noexcept;

        /// @brief Clears all cold payloads and lifecycle counters while preserving the enabled setting.
        void reset() noexcept;

        /// @brief Returns whether cold storage is explicitly enabled.
        bool enabled() const noexcept { return enabled_; }

        /// @brief Compresses a redundant clause and moves it out of the hot arena into cold storage.
        /// @param ref Reference to the clause to demote.
        /// @throws None (noexcept).
        void demote_to_cold(const clause::ref_t ref) noexcept;

        /// @brief Compresses a clause literal payload into opt-in cold storage.
        void demote_to_cold(const clause::ref_t ref, const std::span<const literal> literals) noexcept;

        /// @brief Decompresses a cold clause and reinstates it in ordinary arena storage.
        /// @param ref Reference to the cold clause to promote.
        /// @return Reference to the clause's new location in ordinary arena storage.
        /// @throws None (noexcept).
        clause::ref_t promote_from_cold(const clause::ref_t ref) noexcept;

        /// @brief Decompresses a cold clause transparently for one-off access without permanently promoting it.
        /// @param ref Reference to the cold clause being accessed.
        /// @throws None (noexcept).
        void decode_on_access(const clause::ref_t ref) noexcept
        {
            if (enabled_ && (cold_refs_.find(ref.offset()) != cold_refs_.end()))
                ++access_count_;
        }

        /// @brief Decodes a cold payload for an explicitly cold access without promoting it.
        std::vector<literal> decode_literals(const clause::ref_t ref) const noexcept;

        /// @brief Returns the total compressed footprint of all clauses currently held in cold storage.
        /// @return Footprint in bytes.
        /// @throws None (noexcept).
        std::size_t cold_footprint_bytes() const noexcept { return cold_footprint_; }

        /// @brief Returns how many cold clauses have been promoted.
        std::size_t promotion_count() const noexcept { return promotion_count_; }

        /// @brief Returns how many cold accesses were observed.
        std::size_t access_count() const noexcept { return access_count_; }

        /// @brief Returns whether a reference is currently tracked as cold.
        bool is_cold(const clause::ref_t ref) const noexcept { return enabled_ && (cold_refs_.find(ref.offset()) != cold_refs_.end()); }

        /// @brief Rewrites a tracked cold reference after a physical clause move.
        void rewrite_ref_after_gc(const clause::ref_t old_ref, const clause::ref_t new_ref) noexcept;

        /// @brief Re-encodes a tracked cold payload after clause literals are rewritten by compaction.
        void rewrite_literals_after_compaction(const clause::ref_t ref, const std::span<const literal> literals) noexcept;

    private:
        static constexpr std::size_t estimated_entry_bytes_ {8u};

        void store_payload(const clause::ref_t ref, const std::span<const literal> literals) noexcept;

        void erase_payload(const clause::ref_t ref) noexcept;

        bool enabled_ {};
        std::unordered_set<clause::ref_t::offset_t> cold_refs_ {};
        cold_payload_map_t payloads_ {};
        std::size_t cold_footprint_ {};
        std::size_t promotion_count_ {};
        std::size_t access_count_ {};
    };
}
