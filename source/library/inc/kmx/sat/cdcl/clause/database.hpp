/// @file inc/kmx/sat/cdcl/clause/database.hpp
/// @brief Logical orchestration of clauses, clause tiers, and their relation to watched literals.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <limits>
    #include <span>
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
    /// irredundant (original) versus redundant (learned), tracks tier membership used by `controller::reduce` for
    /// retention decisions, marks clauses as garbage or as active reasons, and exposes `iterate_irredundant`/
    /// `iterate_redundant` for passes that must visit one class of clause without scanning the other.
    ///
    /// Every per-clause attribute (glue, tier, usage, activity, flags) lives in the clause's own arena header, so
    /// the database keeps no side table: it owns the two reference vectors and the garbage count, nothing else.
    /// `compact` is the garbage collector: it slides live clauses down the arena and reports every move so the
    /// owner can rewrite watches and reasons.
    /// @note A clause flagged as an active reason is never flushed, so deleting passes cannot invalidate an
    /// implication on the trail; the search flags trail reasons before any deleting pass runs and clears them after.
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
            auto& header = storage_.header_at(ref);
            header.glue = static_cast<std::uint32_t>(literals.size());
            header.tier = static_cast<std::uint8_t>(redundant ? default_tier : tier_t {});
            header.flags |= bank::tracked_flag;
            header.used = 0u;
            header.activity = 0.0f;
            return ref;
        }

        /// @brief Returns live quality metadata for a clause.
        [[nodiscard]] quality quality_of(const ref_t ref) const noexcept
        {
            const auto header = storage_.header_of(ref);
            return quality {tier_from(header), header.glue, header.used, header.size, static_cast<double>(header.activity)};
        }

        /// @brief Records a clause's current LBD/glue value.
        void set_glue(const ref_t ref, const std::uint32_t glue) noexcept
        {
            if (!storage_.is_alive(ref))
                return;
            auto& header = storage_.header_at(ref);
            header.glue = glue;
            header.flags |= bank::tracked_flag;
        }

        /// @brief Records one use of a clause as an implication reason; saturates at the counter's range.
        void increment_used_count(const ref_t ref) noexcept
        {
            if (!storage_.is_alive(ref))
                return;
            auto& used = storage_.header_at(ref).used;
            if (used != std::numeric_limits<std::uint8_t>::max())
                ++used;
        }

        /// @brief Records a use of a clause in conflict analysis: two counts, so that the reduction pass can tell a
        /// use since the last pass (two or more) from the one pass of grace it leaves behind (one).
        void note_clause_used(const ref_t ref) noexcept
        {
            if (!storage_.is_alive(ref))
                return;
            auto& used = storage_.header_at(ref).used;
            used = static_cast<std::uint8_t>(std::min<unsigned>(255u, static_cast<unsigned>(used) + 2u));
        }

        /// @brief Overwrites a clause's use counter; the reduction pass uses it to grant a pass of grace.
        void set_used_count(const ref_t ref, const std::uint8_t used) noexcept
        {
            if (storage_.is_alive(ref))
                storage_.header_at(ref).used = used;
        }

        /// @brief Adds conflict-derived activity to a clause's retention score.
        void increment_activity(const ref_t ref, const double amount = 1.0) noexcept
        {
            if (!storage_.is_alive(ref))
                return;
            storage_.header_at(ref).activity += static_cast<float>(amount);
        }

        /// @brief Ages activity and usage metadata so old conflict history cannot dominate indefinitely.
        void decay_quality(const double factor = 0.5) noexcept
        {
            const auto bounded_factor = factor < 0.0 ? 0.0 : (factor > 1.0 ? 1.0 : factor);
            const auto age = [&](const ref_t ref) noexcept
            {
                if (!storage_.is_alive(ref))
                    return;
                auto& header = storage_.header_at(ref);
                header.activity = static_cast<float>(static_cast<double>(header.activity) * bounded_factor);
                header.used = static_cast<std::uint8_t>(static_cast<double>(header.used) * bounded_factor);
            };
            for (const auto ref: irredundant_refs_)
                age(ref);
            for (const auto ref: redundant_refs_)
                age(ref);
        }

        /// @brief Marks a clause as garbage, making it eligible for physical reclamation.
        /// @param ref Reference to the clause to mark.
        /// @throws None (noexcept).
        void mark_garbage(const ref_t ref) noexcept
        {
            if (!storage_.is_alive(ref))
                return;
            auto& header = storage_.header_at(ref);
            if ((header.flags & bank::garbage_flag) != 0u)
                return;
            header.flags |= bank::garbage_flag;
            ++garbage_count_;
        }

        /// @brief Marks a clause as currently serving as an implication reason on the trail.
        void mark_reason_clause(const ref_t ref) noexcept
        {
            if (storage_.is_alive(ref))
                storage_.header_at(ref).flags |= bank::reason_flag;
        }

        /// @brief Drops the implication-reason mark from a clause that has stopped being a reason.
        void unmark_reason_clause(const ref_t ref) noexcept
        {
            if (storage_.is_alive(ref))
                storage_.header_at(ref).flags &= static_cast<std::uint8_t>(~bank::reason_flag);
        }

        /// @brief Clears all transient implication-reason marks for a fresh solve episode.
        void clear_reason_clauses() noexcept
        {
            for (const auto ref: irredundant_refs_)
                unmark_reason_clause(ref);
            for (const auto ref: redundant_refs_)
                unmark_reason_clause(ref);
        }

        /// @brief Turns a learned clause into an irredundant one, so that reduction can never remove it.
        /// @details Required whenever a learned clause is used to delete an original clause it subsumes: the
        /// original is implied by the learned one only while the learned one exists, so the learned one has to
        /// inherit the original's permanence. Returns false if the clause is not a live learned clause.
        bool make_irredundant(const ref_t ref) noexcept
        {
            if (!storage_.is_alive(ref))
                return false;
            const auto it = std::find(redundant_refs_.begin(), redundant_refs_.end(), ref);
            if (it == redundant_refs_.end())
                return false;
            redundant_refs_.erase(it);
            irredundant_refs_.push_back(ref);
            auto& header = storage_.header_at(ref);
            header.flags &= static_cast<std::uint8_t>(~bank::redundant_flag);
            header.tier = 0u;
            return true;
        }

        /// @brief Rewrites database bookkeeping after a live clause moves to a new physical reference.
        void rewrite_ref_after_gc(const ref_t old_ref, const ref_t new_ref) noexcept
        {
            if (!old_ref.valid() || !new_ref.valid() || old_ref == new_ref)
                return;
            rewrite_ref_in_vector(irredundant_refs_, old_ref, new_ref);
            rewrite_ref_in_vector(redundant_refs_, old_ref, new_ref);
        }

        /// @brief Checks whether a clause is currently marked garbage.
        [[nodiscard]] [[gnu::always_inline]] inline bool is_garbage(const ref_t ref) const noexcept
        {
            return storage_.is_alive(ref) && (storage_.header_of(ref).flags & bank::garbage_flag) != 0u;
        }

        /// @brief Checks whether a clause is currently serving as an implication reason.
        [[nodiscard]] bool is_reason_clause(const ref_t ref) const noexcept
        {
            return storage_.is_alive(ref) && (storage_.header_of(ref).flags & bank::reason_flag) != 0u;
        }

        /// @brief Returns a clause's current tier, or `default_tier` if it is not tracked.
        [[nodiscard]] tier_t tier_of(const ref_t ref) const noexcept { return tier_from(storage_.header_of(ref)); }

        /// @brief Assigns a clause's tier outright, clamped to the valid range.
        void set_tier(const ref_t ref, const tier_t tier) noexcept
        {
            if (!storage_.is_alive(ref))
                return;
            auto& header = storage_.header_at(ref);
            header.flags |= bank::tracked_flag;
            header.tier = static_cast<std::uint8_t>(tier > lowest_tier ? lowest_tier : tier);
        }

        /// @brief Moves a clause to a higher-quality tier, typically after repeated useful activity.
        void promote_clause(const ref_t ref) noexcept
        {
            if (!storage_.is_alive(ref))
                return;
            auto& header = storage_.header_at(ref);
            header.flags |= bank::tracked_flag;
            if (header.tier > 0u)
                --header.tier;
        }

        /// @brief Moves a clause to a lower-quality tier, typically after prolonged inactivity.
        void demote_clause(const ref_t ref) noexcept
        {
            if (!storage_.is_alive(ref))
                return;
            auto& header = storage_.header_at(ref);
            header.flags |= bank::tracked_flag;
            if (header.tier < lowest_tier)
                ++header.tier;
        }

        /// @brief Visits every irredundant (original) clause currently in the database.
        template <typename visitor_t>
        void iterate_irredundant(visitor_t&& visitor) const noexcept
        {
            iterate(irredundant_refs_, visitor);
        }

        /// @brief Visits every redundant (learned) clause currently in the database.
        template <typename visitor_t>
        void iterate_redundant(visitor_t&& visitor) const noexcept
        {
            iterate(redundant_refs_, visitor);
        }

        /// @brief Removes clauses already satisfied at decision level zero from the database.
        /// @details A clause flagged as an active implication reason is never removed, whatever the predicate
        /// says: an implication on the trail must not lose the clause that justifies it. Such a clause has its
        /// garbage mark cleared instead.
        /// @param is_satisfied Predicate returning true for a clause `ref_t` that is satisfied and removable.
        template <typename predicate_t>
        void flush_satisfied(predicate_t&& is_satisfied) noexcept
        {
            flush_matching(irredundant_refs_, is_satisfied);
            flush_matching(redundant_refs_, is_satisfied);
        }

        /// @brief Slides every live clause down the arena in offset order and reports each move.
        /// @param forward Callable `(ref_t old_ref, ref_t new_ref)` invoked for every live clause, moved or not.
        /// @details Dead clauses simply disappear; the caller drops their watches when `forward` never names them.
        template <typename forward_t>
        void compact(forward_t&& forward) noexcept
        {
            compaction_slots_.clear();
            compaction_slots_.reserve(irredundant_refs_.size() + redundant_refs_.size());
            for (auto& ref: irredundant_refs_)
                compaction_slots_.push_back(&ref);
            for (auto& ref: redundant_refs_)
                compaction_slots_.push_back(&ref);
            std::sort(compaction_slots_.begin(), compaction_slots_.end(),
                      [](const ref_t* left, const ref_t* right) noexcept { return left->offset() < right->offset(); });
            storage_.compact(compaction_slots_, forward);
        }

        /// @brief Captures a snapshot of clause-count/tier statistics for telemetry reporting.
        [[nodiscard]] stats stats_snapshot() const noexcept
        {
            return stats {irredundant_refs_.size(), redundant_refs_.size(), garbage_count_};
        }

        /// @brief Forgets the redundant clauses added after the first `count`, for a caller that rolls the arena back.
        void discard_redundant_since(const std::size_t count) noexcept
        {
            if (redundant_refs_.size() > count)
                redundant_refs_.resize(count);
        }

        /// @brief Returns the underlying physical clause storage.
        [[nodiscard]] storage& storage_of() noexcept { return storage_; }

        /// @copydoc storage_of
        [[nodiscard]] const storage& storage_of() const noexcept { return storage_; }

        /// @brief Exposes the raw irredundant reference set, including garbage-marked entries.
        [[nodiscard]] std::span<const ref_t> irredundant_refs() const noexcept { return irredundant_refs_; }

        /// @brief Exposes the raw redundant reference set, including garbage-marked entries.
        [[nodiscard]] std::span<const ref_t> redundant_refs() const noexcept { return redundant_refs_; }

    private:
        static tier_t tier_from(const bank::clause_header& header) noexcept
        {
            return (header.flags & bank::tracked_flag) != 0u ? tier_t {header.tier} : default_tier;
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
                    if (is_reason_clause(ref))
                    {
                        auto& header = storage_.header_at(ref);
                        if ((header.flags & bank::garbage_flag) != 0u)
                        {
                            header.flags &= static_cast<std::uint8_t>(~bank::garbage_flag);
                            if (garbage_count_ != 0u)
                                --garbage_count_;
                        }
                        refs[write_index++] = ref;
                        continue;
                    }
                    if (is_garbage(ref) && garbage_count_ != 0u)
                        --garbage_count_;
                    storage_.destroy_clause(ref);
                    continue;
                }
                refs[write_index++] = ref;
            }
            refs.resize(write_index);
        }

        storage storage_ {};
        std::vector<ref_t> irredundant_refs_ {};
        std::vector<ref_t> redundant_refs_ {};
        std::vector<ref_t*> compaction_slots_ {};
        std::size_t garbage_count_ {};
    };
}
