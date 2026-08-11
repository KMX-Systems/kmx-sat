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
    ///
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
            std::size_t irredundant_count {0};
            std::size_t redundant_count {0};
            std::size_t garbage_count {0};
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
            {
                return ref;
            }

            if (redundant)
            {
                redundant_refs_.push_back(ref);
            }
            else
            {
                irredundant_refs_.push_back(ref);
            }
            tiers_[ref.offset()] = redundant ? default_tier : 0u;
            return ref;
        }

        /// @brief Marks a clause as garbage, making it eligible for physical reclamation by the garbage collector.
        /// @param ref Reference to the clause to mark.
        /// @throws None (noexcept).
        void mark_garbage(const ref_t ref) noexcept
        {
            if (ref.valid())
            {
                garbage_.insert(ref.offset());
            }
        }

        /// @brief Marks a clause as currently serving as an implication reason on the trail.
        /// @param ref Reference to the clause to mark.
        /// @throws None (noexcept).
        void mark_reason_clause(const ref_t ref) noexcept
        {
            if (ref.valid())
            {
                reasons_.insert(ref.offset());
            }
        }

        /// @brief Checks whether a clause is currently marked garbage.
        /// @param ref Reference to the clause to query.
        /// @return True if `ref` was previously passed to `mark_garbage`.
        /// @throws None (noexcept).
        [[nodiscard]] bool is_garbage(const ref_t ref) const noexcept
        {
            return ref.valid() && garbage_.find(ref.offset()) != garbage_.end();
        }

        /// @brief Checks whether a clause is currently serving as an implication reason.
        /// @param ref Reference to the clause to query.
        /// @return True if `ref` was previously passed to `mark_reason_clause` and not yet cleared.
        /// @throws None (noexcept).
        [[nodiscard]] bool is_reason_clause(const ref_t ref) const noexcept
        {
            return ref.valid() && reasons_.find(ref.offset()) != reasons_.end();
        }

        /// @brief Returns a clause's current tier, or `default_tier` if it is not tracked.
        /// @param ref Reference to the clause to query.
        /// @return Current tier of `ref`.
        /// @throws None (noexcept).
        [[nodiscard]] tier_t tier_of(const ref_t ref) const noexcept
        {
            if (const auto it = tiers_.find(ref.offset()); it != tiers_.end())
            {
                return it->second;
            }
            return default_tier;
        }

        /// @brief Moves a clause to a higher-quality tier, typically after repeated useful activity.
        /// @param ref Reference to the clause to promote.
        /// @throws None (noexcept).
        void promote_clause(const ref_t ref) noexcept
        {
            if (!ref.valid())
            {
                return;
            }
            auto& tier = tiers_[ref.offset()];
            if (tier > 0u)
            {
                --tier;
            }
        }

        /// @brief Moves a clause to a lower-quality tier, typically after prolonged inactivity.
        /// @param ref Reference to the clause to demote.
        /// @throws None (noexcept).
        void demote_clause(const ref_t ref) noexcept
        {
            if (!ref.valid())
            {
                return;
            }
            auto& tier = tiers_[ref.offset()];
            if (tier < lowest_tier)
            {
                ++tier;
            }
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
            return stats {irredundant_refs_.size(), redundant_refs_.size(), garbage_.size()};
        }

        /// @brief Returns the underlying physical clause storage.
        /// @return Reference to the owned `clause::storage` instance.
        /// @throws None (noexcept).
        [[nodiscard]] storage& storage_of() noexcept
        {
            return storage_;
        }

        /// @copydoc storage_of
        [[nodiscard]] const storage& storage_of() const noexcept
        {
            return storage_;
        }

    private:
        template <typename visitor_t>
        void iterate(const std::vector<ref_t>& refs, visitor_t&& visitor) const noexcept
        {
            for (const auto ref : refs)
            {
                if (!is_garbage(ref))
                {
                    visitor(ref);
                }
            }
        }

        template <typename predicate_t>
        void flush_matching(std::vector<ref_t>& refs, predicate_t&& is_satisfied) noexcept
        {
            std::size_t write_index {0};
            for (std::size_t read_index {0}; read_index < refs.size(); ++read_index)
            {
                const auto ref = refs[read_index];
                if (is_satisfied(ref))
                {
                    storage_.destroy_clause(ref);
                    garbage_.erase(ref.offset());
                    reasons_.erase(ref.offset());
                    tiers_.erase(ref.offset());
                    continue;
                }
                refs[write_index++] = ref;
            }
            refs.resize(write_index);
        }

        storage storage_ {};
        std::vector<ref_t> irredundant_refs_ {};
        std::vector<ref_t> redundant_refs_ {};
        std::unordered_set<ref_t::offset_t> garbage_ {};
        std::unordered_set<ref_t::offset_t> reasons_ {};
        std::unordered_map<ref_t::offset_t, tier_t> tiers_ {};
    };
}

