/// @file inc/kmx/sat/cdcl/clause/database.hpp
/// @brief Logical orchestration of clauses, clause tiers, and their relation to watched literals.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <unordered_map>
    #include <unordered_set>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/cdcl/clause/storage.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::clause
{
    /// @brief Logical orchestration of clauses, clause tiers, and their relation to watched literals.
    /// @details
    /// `clause::database` is the logical layer built on top of `clause::storage`: it decides which clauses are
    /// irredundant (original) versus redundant (learned), tracks tier membership used by `reduce_controller` for
    /// retention decisions, marks clauses as garbage or as active reasons, and exposes `iterate_irredundant`/
    /// `iterate_redundant` for passes (`forward_subsumer`, `vivifier`, proof replay) that must visit one class of
    /// clause without scanning the other. `promote_clause`/`demote_clause` move a clause between tiers as its
    /// activity/glue changes; `flush_satisfied` removes clauses already satisfied at decision level zero;
    /// `stats_snapshot` feeds `telemetry::solver_statistics`.
    /// @note This class only coordinates clause lifecycle and tiering; physical byte-level operations always
    /// delegate to the owned `clause::storage` instance, and watch-list bookkeeping is owned separately by
    /// `bank::watch_list`.
    class database final
    {
    public:
        /// @brief Clause quality tier; 0 is the highest-quality (core) tier, increasing values are lower quality.
        using tier_t = std::uint32_t;
        static constexpr tier_t default_tier {1};
        static constexpr tier_t lowest_tier {2};

        /// @brief Snapshot of clause-count/tier statistics for telemetry reporting.
        struct stats final
        {
            std::size_t irredundant_count {};
            std::size_t redundant_count {};
            std::size_t garbage_count {};
        };

        /// @brief Quality metadata used by learned-clause retention and reduction policies.
        struct quality final
        {
            tier_t tier {};
            std::uint32_t glue {};
            std::uint32_t used_count {};
            std::uint32_t size {};
            double activity {};
        };

        /// @brief Constructs a database with an empty underlying clause storage.
        /// @throws None (noexcept).
        database() noexcept = default;

        /// @brief Registers a new clause with the logical database, assigning it a default tier.
        /// @param literals Literals composing the clause.
        /// @param redundant True if the clause is a learned (redundant) clause rather than an original one.
        /// @return Reference to the newly registered clause.
        /// @throws None (noexcept).
        ref_t add_clause(const std::span<const literal> literals, const bool redundant = false) noexcept
        {
            const auto ref = redundant ? storage_.create_learned_clause(literals) : storage_.create_original_clause(literals);
            if (!ref.valid())
                return ref;

            if (redundant)
                redundant_refs_.push_back(ref);
            else
                irredundant_refs_.push_back(ref);
            auto& entry = metadata_for(ref.offset());
            entry.tracked = true;
            entry.tier = redundant ? default_tier : tier_t {};
            entry.glue = static_cast<std::uint32_t>(literals.size());
            entry.used_count = 0u;
            entry.activity = {};
            return ref;
        }

        /// @brief Returns live quality metadata for a clause.
        [[nodiscard]] quality quality_of(const ref_t ref) const noexcept
        {
            const auto resolved = storage_.resolve_ref(ref);
            const auto* entry = metadata_of(resolved.offset());
            return quality {tier_of(resolved), entry != nullptr ? entry->glue : 0u, entry != nullptr ? entry->used_count : 0u,
                            storage_.literal_count(resolved), entry != nullptr ? entry->activity : 0.0};
        }

        /// @brief Records a clause's current LBD/glue value.
        void set_glue(const ref_t ref, const std::uint32_t glue) noexcept
        {
            if (ref.valid())
                mark_tracked(metadata_for(storage_.resolve_ref(ref).offset())).glue = glue;
        }

        /// @brief Records one use of a clause as an implication reason.
        void increment_used_count(const ref_t ref) noexcept
        {
            if (ref.valid())
                ++mark_tracked(metadata_for(storage_.resolve_ref(ref).offset())).used_count;
        }

        /// @brief Adds conflict-derived activity to a clause's retention score.
        void increment_activity(const ref_t ref, const double amount = 1.0) noexcept
        {
            if (ref.valid())
                mark_tracked(metadata_for(storage_.resolve_ref(ref).offset())).activity += amount;
        }

        /// @brief Ages activity and usage metadata so old conflict history cannot dominate indefinitely.
        void decay_quality(const double factor = 0.5) noexcept
        {
            const auto bounded_factor = factor < 0.0 ? 0.0 : (factor > 1.0 ? 1.0 : factor);
            for (auto& entry: metadata_)
            {
                entry.activity *= bounded_factor;
                entry.used_count = static_cast<std::uint32_t>(static_cast<double>(entry.used_count) * bounded_factor);
            }
        }

        /// @brief Marks a clause as garbage, making it eligible for physical reclamation by the garbage collector.
        /// @param ref Reference to the clause to mark.
        /// @throws None (noexcept).
        void mark_garbage(const ref_t ref) noexcept
        {
            if (!ref.valid())
                return;
            auto& entry = metadata_for(ref.offset());
            if (!entry.garbage)
            {
                entry.garbage = true;
                ++garbage_count_;
            }
        }

        /// @brief Marks a clause as currently serving as an implication reason on the trail.
        /// @param ref Reference to the clause to mark.
        /// @throws None (noexcept).
        void mark_reason_clause(const ref_t ref) noexcept
        {
            if (ref.valid())
                metadata_for(ref.offset()).reason = true;
        }

        /// @brief Clears all transient implication-reason marks for a fresh solve episode.
        void clear_reason_clauses() noexcept
        {
            for (auto& entry: metadata_)
                entry.reason = false;
        }

        /// @brief Rewrites database bookkeeping after a live clause moves to a new physical reference.
        void rewrite_ref_after_gc(const ref_t old_ref, const ref_t new_ref) noexcept
        {
            if (!old_ref.valid() || !new_ref.valid() || old_ref == new_ref)
                return;
            rewrite_ref_in_vector(irredundant_refs_, old_ref, new_ref);
            rewrite_ref_in_vector(redundant_refs_, old_ref, new_ref);
            const auto* old_entry = metadata_of(old_ref.offset());
            if (old_entry == nullptr)
                return;
            const auto migrated = *old_entry;
            reset_metadata(old_ref.offset());
            auto& new_entry = metadata_for(new_ref.offset());
            if (migrated.garbage && !new_entry.garbage)
                ++garbage_count_;
            else if (!migrated.garbage && new_entry.garbage)
                --garbage_count_;
            new_entry = migrated;
        }

        /// @brief Checks whether a clause is currently marked garbage.
        /// @param ref Reference to the clause to query.
        /// @return True if `ref` was previously passed to `mark_garbage`.
        /// @throws None (noexcept).
        [[nodiscard]] bool is_garbage(const ref_t ref) const noexcept
        {
            const auto* entry = ref.valid() ? metadata_of(ref.offset()) : nullptr;
            return entry != nullptr && entry->garbage;
        }

        /// @brief Checks whether a clause is currently serving as an implication reason.
        /// @param ref Reference to the clause to query.
        /// @return True if `ref` was previously passed to `mark_reason_clause` and not yet cleared.
        /// @throws None (noexcept).
        [[nodiscard]] bool is_reason_clause(const ref_t ref) const noexcept
        {
            const auto* entry = ref.valid() ? metadata_of(ref.offset()) : nullptr;
            return entry != nullptr && entry->reason;
        }

        /// @brief Returns a clause's current tier, or `default_tier` if it is not tracked.
        /// @param ref Reference to the clause to query.
        /// @return Current tier of `ref`.
        /// @throws None (noexcept).
        [[nodiscard]] tier_t tier_of(const ref_t ref) const noexcept
        {
            const auto* entry = metadata_of(ref.offset());
            return entry != nullptr && entry->tracked ? entry->tier : default_tier;
        }

        /// @brief Moves a clause to a higher-quality tier, typically after repeated useful activity.
        /// @param ref Reference to the clause to promote.
        /// @throws None (noexcept).
        void promote_clause(const ref_t ref) noexcept
        {
            if (!ref.valid())
                return;
            auto& entry = mark_tracked(metadata_for(ref.offset()));
            if (entry.tier > 0u)
                --entry.tier;
        }

        /// @brief Moves a clause to a lower-quality tier, typically after prolonged inactivity.
        /// @param ref Reference to the clause to demote.
        /// @throws None (noexcept).
        void demote_clause(const ref_t ref) noexcept
        {
            if (!ref.valid())
                return;
            auto& entry = mark_tracked(metadata_for(ref.offset()));
            if (entry.tier < lowest_tier)
                ++entry.tier;
        }

        /// @brief Visits every irredundant (original) clause currently in the database.
        /// @param visitor Callable invoked with each non-garbage irredundant clause's `ref_t`.
        /// @throws None (noexcept).
        template <typename visitor_t>
        void iterate_irredundant(visitor_t&& visitor) const noexcept
        {
            iterate(irredundant_refs_, visitor);
        }

        /// @brief Visits every redundant (learned) clause currently in the database.
        /// @param visitor Callable invoked with each non-garbage redundant clause's `ref_t`.
        /// @throws None (noexcept).
        template <typename visitor_t>
        void iterate_redundant(visitor_t&& visitor) const noexcept
        {
            iterate(redundant_refs_, visitor);
        }

        /// @brief Removes clauses already satisfied at decision level zero from the database.
        /// @param is_satisfied Predicate returning true for a clause `ref_t` that is satisfied and removable.
        /// @throws None (noexcept).
        template <typename predicate_t>
        void flush_satisfied(predicate_t&& is_satisfied) noexcept
        {
            flush_matching(irredundant_refs_, is_satisfied);
            flush_matching(redundant_refs_, is_satisfied);
        }

        /// @brief Captures a snapshot of clause-count/tier statistics for telemetry reporting.
        /// @return Current clause-count statistics.
        /// @throws None (noexcept).
        [[nodiscard]] stats stats_snapshot() const noexcept
        {
            return stats {irredundant_refs_.size(), redundant_refs_.size(), garbage_count_};
        }

        /// @brief Returns the underlying physical clause storage.
        /// @return Reference to the owned `clause::storage` instance.
        /// @throws None (noexcept).
        [[nodiscard]] storage& storage_of() noexcept { return storage_; }

        /// @copydoc storage_of
        [[nodiscard]] const storage& storage_of() const noexcept { return storage_; }

        /// @brief Exposes the raw irredundant reference set, including garbage-marked entries.
        /// @return Snapshot view over irredundant references.
        [[nodiscard]] std::span<const ref_t> irredundant_refs() const noexcept { return irredundant_refs_; }

        /// @brief Exposes the raw redundant reference set, including garbage-marked entries.
        /// @return Snapshot view over redundant references.
        [[nodiscard]] std::span<const ref_t> redundant_refs() const noexcept { return redundant_refs_; }

    private:
        /// Per-clause bookkeeping, kept in a flat table indexed by 4-byte-aligned arena offset.
        struct clause_metadata final
        {
            double activity {};
            std::uint32_t glue {};
            std::uint32_t used_count {};
            tier_t tier {};
            bool tracked {};
            bool garbage {};
            bool reason {};
        };

        static std::size_t metadata_slot_of(const ref_t::offset_t offset) noexcept
        {
            return static_cast<std::size_t>(offset) / sizeof(std::uint32_t);
        }

        const clause_metadata* metadata_of(const ref_t::offset_t offset) const noexcept
        {
            const auto slot = metadata_slot_of(offset);
            return slot < metadata_.size() ? &metadata_[slot] : nullptr;
        }

        clause_metadata& metadata_for(const ref_t::offset_t offset) noexcept
        {
            const auto slot = metadata_slot_of(offset);
            if (slot >= metadata_.size())
                metadata_.resize(slot + 1u);
            return metadata_[slot];
        }

        static clause_metadata& mark_tracked(clause_metadata& entry) noexcept
        {
            entry.tracked = true;
            return entry;
        }

        void reset_metadata(const ref_t::offset_t offset) noexcept
        {
            const auto slot = metadata_slot_of(offset);
            if (slot >= metadata_.size())
                return;
            if (metadata_[slot].garbage)
                --garbage_count_;
            metadata_[slot] = clause_metadata {};
        }

        static void rewrite_ref_in_vector(std::vector<ref_t>& refs, const ref_t old_ref, const ref_t new_ref) noexcept
        {
            for (auto& ref: refs)
                if (ref == old_ref)
                    ref = new_ref;
        }

        template <typename visitor_t>
        void iterate(const std::vector<ref_t>& refs, visitor_t&& visitor) const noexcept
        {
            for (const auto ref: refs)
                if (!is_garbage(ref))
                    visitor(ref);
        }

        template <typename predicate_t>
        void flush_matching(std::vector<ref_t>& refs, predicate_t&& is_satisfied) noexcept
        {
            std::size_t write_index {};
            for (std::size_t read_index {}; read_index < refs.size(); ++read_index)
            {
                const auto ref = refs[read_index];
                if (is_satisfied(ref))
                {
                    storage_.destroy_clause(ref);
                    reset_metadata(ref.offset());
                    continue;
                }
                refs[write_index++] = ref;
            }
            refs.resize(write_index);
        }

        storage storage_ {};
        std::vector<ref_t> irredundant_refs_ {};
        std::vector<ref_t> redundant_refs_ {};
        std::vector<clause_metadata> metadata_ {};
        std::size_t garbage_count_ {};
    };
}
