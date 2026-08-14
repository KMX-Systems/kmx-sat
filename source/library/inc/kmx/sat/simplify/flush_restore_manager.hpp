/// @file inc/kmx/sat/simplify/flush_restore_manager.hpp
/// @brief Flush/restore policies and the balance between memory use and clause quality.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <span>
    #include <vector>
#endif

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::simplify
{
    /// @brief Flush/restore policies and the balance between memory use and clause quality.
    ///
    /// After `controller::reduce` marks low-quality redundant clauses garbage, `flush_restore_manager` decides how
    /// aggressively to actually remove them versus keeping them available for potential future reuse:
    /// `flush_redundant` physically removes garbage-marked redundant clauses to reclaim memory; `restore_all`/
    /// `restore_irredundant_only` support the inverse operation used by some inprocessing schedules that
    /// temporarily flush and later restore clauses (irredundant-only restore is the cheaper, correctness-preserving
    /// option since original clauses must always be retained); `remove_satisfied` additionally removes clauses
    /// already satisfied at decision level zero regardless of redundancy. This manager is also one of the
    /// components `memory_governor` invokes as part of its soft-ceiling escalation ladder (more aggressive flushing,
    /// then `remove_satisfied`).
    class flush_restore_manager final
    {
    public:
        /// @brief Constructs a flush/restore manager with default flush aggressiveness.
        /// @throws None (noexcept).
        flush_restore_manager() noexcept = default;

        /// @brief Attaches the clause database managed by this flush/restore policy.
        /// @param database Clause database to mutate.
        /// @throws None (noexcept).
        void attach_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        /// @brief Physically removes redundant clauses currently marked garbage.
        /// @throws None (noexcept).
        void flush_redundant() noexcept
        {
            ++flush_count_;
            if (database_ == nullptr)
                return;

            flush_matching([&](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
        }

        /// @brief Restores every previously flushed clause, both redundant and irredundant.
        /// @throws None (noexcept).
        void restore_all() noexcept
        {
            ++restore_count_;
            if (database_ == nullptr)
                return;

            restore_matching([](const flushed_clause&) noexcept { return true; });
        }

        /// @brief Restores only previously flushed irredundant (original) clauses, the correctness-preserving option.
        /// @throws None (noexcept).
        void restore_irredundant_only() noexcept
        {
            ++restore_count_;
            if (database_ == nullptr)
                return;

            restore_matching([](const flushed_clause& record) noexcept { return !record.redundant; });
        }

        /// @brief Removes clauses already satisfied at decision level zero, regardless of redundancy.
        /// @throws None (noexcept).
        void remove_satisfied() noexcept { ++satisfied_removed_count_; }

        /// @brief Removes clauses deemed satisfied by the provided predicate.
        /// @param is_satisfied Predicate deciding which clauses to remove.
        /// @throws None (noexcept).
        template <typename predicate_t>
        void remove_satisfied(predicate_t&& is_satisfied) noexcept
        {
            ++satisfied_removed_count_;
            if (database_ == nullptr)
                return;

            flush_matching([&](const cdcl::clause::ref_t ref) noexcept { return is_satisfied(ref); });
        }

        std::size_t flush_count() const noexcept { return flush_count_; }

        std::size_t restore_count() const noexcept { return restore_count_; }

        std::size_t satisfied_removed_count() const noexcept { return satisfied_removed_count_; }

        std::size_t buffered_clause_count() const noexcept { return flushed_clauses_.size(); }

        std::size_t restored_clause_count() const noexcept { return restored_clause_count_; }

        std::size_t last_flush_removed_count() const noexcept { return last_flush_removed_count_; }

    private:
        struct flushed_clause final
        {
            std::vector<literal> literals {};
            bool redundant {};
        };

        template <typename predicate_t>
        void flush_matching(predicate_t&& should_flush) noexcept
        {
            if (database_ == nullptr)
                return;

            last_flush_removed_count_ = 0u;
            capture_and_remove(*database_, true, should_flush);
            capture_and_remove(*database_, false, should_flush);
        }

        template <typename predicate_t>
        void capture_and_remove(cdcl::clause::database& database, const bool redundant, predicate_t&& should_flush) noexcept
        {
            std::vector<cdcl::clause::ref_t> to_flush {};
            const auto refs = redundant ? database.redundant_refs() : database.irredundant_refs();

            for (const auto ref: refs)
            {
                if (should_flush(ref))
                {
                    flushed_clauses_.push_back(flushed_clause {database.storage_of().literals_of(ref), redundant});
                    to_flush.push_back(ref);
                }
            }

            if (to_flush.empty())
                return;

            database.flush_satisfied(
                [&](const cdcl::clause::ref_t ref) noexcept
                {
                    for (const auto candidate: to_flush)
                        if (candidate == ref)
                            return true;
                    return false;
                });
            last_flush_removed_count_ += to_flush.size();
        }

        template <typename predicate_t>
        void restore_matching(predicate_t&& should_restore) noexcept
        {
            if (database_ == nullptr)
                return;

            std::vector<flushed_clause> remaining {};
            remaining.reserve(flushed_clauses_.size());
            restored_clause_count_ = 0u;

            for (auto& record: flushed_clauses_)
            {
                if (should_restore(record))
                {
                    database_->add_clause(record.literals, record.redundant);
                    ++restored_clause_count_;
                }
                else
                {
                    remaining.push_back(std::move(record));
                }
            }

            flushed_clauses_ = std::move(remaining);
        }

        cdcl::clause::database* database_ {};
        std::vector<flushed_clause> flushed_clauses_ {};
        std::size_t flush_count_ {};
        std::size_t restore_count_ {};
        std::size_t satisfied_removed_count_ {};
        std::size_t restored_clause_count_ {};
        std::size_t last_flush_removed_count_ {};
    };
}
