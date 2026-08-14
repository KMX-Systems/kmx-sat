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

        /// @brief Enables the research-track cold path explicitly; disabled by default.
        void set_enabled(const bool enabled) noexcept
        {
            enabled_ = enabled;
            if (!enabled_)
            {
                cold_refs_.clear();
                payloads_.clear();
                cold_footprint_ = 0u;
                promotion_count_ = 0u;
                access_count_ = 0u;
            }
        }

        /// @brief Clears all cold payloads and lifecycle counters while preserving the enabled setting.
        void reset() noexcept
        {
            cold_refs_.clear();
            payloads_.clear();
            cold_footprint_ = 0u;
            promotion_count_ = 0u;
            access_count_ = 0u;
        }

        /// @brief Returns whether cold storage is explicitly enabled.
        bool enabled() const noexcept { return enabled_; }

        /// @brief Compresses a redundant clause and moves it out of the hot arena into cold storage.
        /// @param ref Reference to the clause to demote.
        /// @throws None (noexcept).
        void demote_to_cold(const clause::ref_t ref) noexcept
        {
            if (!enabled_ || !ref.valid())
                return;
            store_payload(ref, {});
        }

        /// @brief Compresses a clause literal payload into opt-in cold storage.
        void demote_to_cold(const clause::ref_t ref, const std::span<const literal> literals) noexcept
        {
            if (!enabled_ || !ref.valid())
                return;
            store_payload(ref, literals);
        }

        /// @brief Decompresses a cold clause and reinstates it in ordinary arena storage.
        /// @param ref Reference to the cold clause to promote.
        /// @return Reference to the clause's new location in ordinary arena storage.
        /// @throws None (noexcept).
        clause::ref_t promote_from_cold(const clause::ref_t ref) noexcept
        {
            if (enabled_ && cold_refs_.erase(ref.offset()) != 0u)
            {
                erase_payload(ref);
                ++promotion_count_;
            }
            return ref;
        }

        /// @brief Decompresses a cold clause transparently for one-off access without permanently promoting it.
        /// @param ref Reference to the cold clause being accessed.
        /// @throws None (noexcept).
        void decode_on_access(const clause::ref_t ref) noexcept
        {
            if (enabled_ && cold_refs_.find(ref.offset()) != cold_refs_.end())
                ++access_count_;
        }

        /// @brief Decodes a cold payload for an explicitly cold access without promoting it.
        std::vector<literal> decode_literals(const clause::ref_t ref) const noexcept
        {
            std::vector<literal> result {};
            const auto it = payloads_.find(ref.offset());
            if (!enabled_ || it == payloads_.end())
                return result;

            std::int64_t previous {};
            for (std::size_t index {}; index < it->second.size();)
            {
                std::uint64_t encoded {};
                std::uint32_t shift {};
                for (;;)
                {
                    if (index >= it->second.size())
                        return {};
                    const auto byte = it->second[index++];
                    if (shift == 28u && (byte & 0x7fu) > 0x1fu)
                        return {};
                    encoded |= static_cast<std::uint64_t>(byte & 0x7fu) << shift;
                    if ((byte & 0x80u) == 0u)
                        break;
                    shift += 7u;
                    if (shift >= 32u)
                        return {};
                }
                const auto delta = static_cast<std::int64_t>(encoded >> 1u) ^ -static_cast<std::int64_t>(encoded & 1u);
                previous += delta;
                if (previous < 0 || previous > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
                    return {};
                result.emplace_back(literal {static_cast<std::uint32_t>(previous)});
            }
            return result;
        }

        /// @brief Returns the total compressed footprint of all clauses currently held in cold storage.
        /// @return Footprint in bytes.
        /// @throws None (noexcept).
        std::size_t cold_footprint_bytes() const noexcept { return cold_footprint_; }

        /// @brief Returns how many cold clauses have been promoted.
        std::size_t promotion_count() const noexcept { return promotion_count_; }

        /// @brief Returns how many cold accesses were observed.
        std::size_t access_count() const noexcept { return access_count_; }

        /// @brief Returns whether a reference is currently tracked as cold.
        bool is_cold(const clause::ref_t ref) const noexcept { return enabled_ && cold_refs_.find(ref.offset()) != cold_refs_.end(); }

        /// @brief Rewrites a tracked cold reference after a physical clause move.
        void rewrite_ref_after_gc(const clause::ref_t old_ref, const clause::ref_t new_ref) noexcept
        {
            if (!enabled_ || !old_ref.valid() || !new_ref.valid() || old_ref == new_ref)
                return;
            if (cold_refs_.erase(old_ref.offset()) != 0u)
                cold_refs_.insert(new_ref.offset());
            if (const auto it = payloads_.find(old_ref.offset()); it != payloads_.end())
            {
                auto payload = std::move(it->second);
                payloads_.erase(it);
                payloads_.emplace(new_ref.offset(), std::move(payload));
            }
        }

        /// @brief Re-encodes a tracked cold payload after clause literals are rewritten by compaction.
        void rewrite_literals_after_compaction(const clause::ref_t ref, const std::span<const literal> literals) noexcept
        {
            if (!enabled_ || !is_cold(ref))
                return;
            erase_payload(ref);
            cold_refs_.erase(ref.offset());
            store_payload(ref, literals);
        }

    private:
        static constexpr std::size_t estimated_entry_bytes_ {8u};

        void store_payload(const clause::ref_t ref, const std::span<const literal> literals) noexcept
        {
            if (!cold_refs_.insert(ref.offset()).second)
                return;

            std::vector<std::uint8_t> encoded_payload {};
            std::int64_t previous {};
            for (const auto lit: literals)
            {
                const auto current = static_cast<std::int64_t>(lit.raw());
                const auto delta = current - previous;
                previous = current;
                auto encoded = (static_cast<std::uint64_t>(delta) << 1u) ^ static_cast<std::uint64_t>(delta >> 63u);
                do
                {
                    auto byte = static_cast<std::uint8_t>(encoded & 0x7fu);
                    encoded >>= 7u;
                    if (encoded != 0u)
                        byte |= 0x80u;
                    encoded_payload.push_back(byte);
                } while (encoded != 0u);
            }
            cold_footprint_ += encoded_payload.empty() ? estimated_entry_bytes_ : encoded_payload.size();
            payloads_.emplace(ref.offset(), std::move(encoded_payload));
        }

        void erase_payload(const clause::ref_t ref) noexcept
        {
            const auto it = payloads_.find(ref.offset());
            const auto bytes = it == payloads_.end() || it->second.empty() ? estimated_entry_bytes_ : it->second.size();
            cold_footprint_ = cold_footprint_ >= bytes ? cold_footprint_ - bytes : 0u;
            if (it != payloads_.end())
                payloads_.erase(it);
        }

        bool enabled_ {};
        std::unordered_set<clause::ref_t::offset_t> cold_refs_ {};
        std::unordered_map<clause::ref_t::offset_t, std::vector<std::uint8_t>> payloads_ {};
        std::size_t cold_footprint_ {};
        std::size_t promotion_count_ {};
        std::size_t access_count_ {};
    };
}
