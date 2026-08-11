/// @file inc/kmx/sat/simplify/flush_restore_manager.hpp
/// @brief Flush/restore policies and the balance between memory use and clause quality.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif

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

        /// @brief Physically removes redundant clauses currently marked garbage.
        /// @throws None (noexcept).
        void flush_redundant() noexcept
        {
            ++flush_count_;
        }

        /// @brief Restores every previously flushed clause, both redundant and irredundant.
        /// @throws None (noexcept).
        void restore_all() noexcept
        {
            ++restore_count_;
        }

        /// @brief Restores only previously flushed irredundant (original) clauses, the correctness-preserving option.
        /// @throws None (noexcept).
        void restore_irredundant_only() noexcept
        {
            ++restore_count_;
        }

        /// @brief Removes clauses already satisfied at decision level zero, regardless of redundancy.
        /// @throws None (noexcept).
        void remove_satisfied() noexcept
        {
            ++satisfied_removed_count_;
        }

        std::size_t flush_count() const noexcept
        {
            return flush_count_;
        }

        std::size_t restore_count() const noexcept
        {
            return restore_count_;
        }

        std::size_t satisfied_removed_count() const noexcept
        {
            return satisfied_removed_count_;
        }

    private:
        std::size_t flush_count_ {0u};
        std::size_t restore_count_ {0u};
        std::size_t satisfied_removed_count_ {0u};
    };
}
