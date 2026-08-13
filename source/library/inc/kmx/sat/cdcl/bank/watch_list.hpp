/// @file inc/kmx/sat/cdcl/bank/watch_list.hpp
/// @brief All watch lists, partitioned by literal.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <unordered_map>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::bank
{
    /// @brief All watch lists, partitioned by literal.
    ///
    /// @details
    /// `bank::watch_list` holds one list of `watch` entries per literal (positive and negative occurrence of every
    /// variable get independent lists), the core data structure `propagator::propagate` scans on every unit
    /// propagation step. `watch_literal`/`unwatch_literal` add/remove one watch entry, invoked when
    /// `propagator::attach_clause`/`detach_clause` change which two literals of a clause are watched.
    /// `replace_clause_ref_after_gc` rewrites every stored `clause::ref_t` after `garbage_collector::collect`
    /// relocates clauses, and `reindex_after_compaction` rebuilds the per-literal partition after
    /// `compaction_service` renumbers variables; both must run before propagation resumes, since a stale reference in
    /// a watch list would otherwise dereference relocated or renumbered storage. `flush_large_watches` prunes watch
    /// entries for clauses that have been marked garbage to keep list scans short.
    /// @warning A `watch_list` left un-rewritten after garbage collection or compaction contains dangling
    /// `clause::ref_t` values; both rewrite steps are mandatory, not optional, cleanup after those operations.
    class watch_list final
    {
    public:
        /// @brief Constructs an empty set of watch lists.
        /// @throws None (noexcept).
        watch_list() noexcept = default;

        /// @brief Adds one watch entry to the list for the given literal.
        /// @param lit Literal whose list receives the new entry.
        /// @param entry Watch entry to add.
        /// @throws None (noexcept).
        void watch_literal(const literal lit, const watch entry) noexcept
        {
            auto& list = ensure_list(lit);
            const auto existing = std::find_if(list.begin(), list.end(), [&](const watch& current) noexcept
                                               { return current.clause_ref() == entry.clause_ref(); });
            if (existing != list.end())
            {
                *existing = entry;
                return;
            }
            list.push_back(entry);
        }

        /// @brief Removes one watch entry from the list for the given literal.
        /// @param lit Literal whose list loses the entry.
        /// @param entry Watch entry to remove.
        /// @throws None (noexcept).
        void unwatch_literal(const literal lit, const watch entry) noexcept
        {
            const auto index = lit.index_in_watch_bank();
            const auto it = lists_.find(index);
            if (it == lists_.end())
            {
                return;
            }
            auto& list = it->second;
            const auto old_size = list.size();
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&](const auto& current) noexcept { return current.clause_ref() == entry.clause_ref(); }),
                       list.end());
            if (list.size() == old_size)
            {
                return;
            }
            if (list.empty())
            {
                lists_.erase(it);
            }
        }

        /// @brief Checks whether the given watch entry is currently registered for the given literal.
        /// @param lit Literal whose watch list is inspected.
        /// @param entry Watch entry to look for.
        /// @return True if an identical watch entry exists in the list for `lit`.
        [[nodiscard]] bool contains(const literal lit, const watch entry) const noexcept
        {
            const auto index = lit.index_in_watch_bank();
            const auto it = lists_.find(index);
            if (it == lists_.end())
            {
                return false;
            }
            const auto& list = it->second;
            return std::find(list.begin(), list.end(), entry) != list.end();
        }

        /// @brief Visits every watch entry currently registered for the given literal.
        /// @param lit Literal whose watch list is visited.
        /// @param visitor Callable invoked with each `watch` entry currently registered for `lit`.
        /// @throws None (noexcept).
        template <typename visitor_t>
        void iterate(const literal lit, visitor_t&& visitor) const noexcept
        {
            ++iterate_call_count_;
            const auto index = lit.index_in_watch_bank();
            const auto it = lists_.find(index);
            if (it == lists_.end())
            {
                return;
            }
            for (const auto& entry: it->second)
            {
                visitor(entry);
            }
        }

        /// @brief Returns how many watch entries are currently registered for the given literal.
        /// @param lit Literal whose watch list size is queried.
        /// @return Number of watch entries currently registered for `lit`.
        /// @throws None (noexcept).
        [[nodiscard]] std::size_t size_of(const literal lit) const noexcept
        {
            const auto it = lists_.find(lit.index_in_watch_bank());
            return it != lists_.end() ? it->second.size() : 0u;
        }

        /// @brief Returns how many watch partitions have been iterated since construction/reset.
        std::size_t iterate_call_count() const noexcept { return iterate_call_count_; }

        /// @brief Clears watch-list diagnostic counters without changing stored watches.
        void reset_diagnostics() noexcept { iterate_call_count_ = 0u; }

        /// @brief Rewrites every stored clause reference after a garbage-collection cycle relocates one clause.
        /// @param old_ref Clause reference before relocation.
        /// @param new_ref Clause reference after relocation.
        /// @throws None (noexcept).
        void replace_clause_ref_after_gc(const clause::ref_t old_ref, const clause::ref_t new_ref) noexcept
        {
            if (!old_ref.valid() || !new_ref.valid() || old_ref == new_ref)
            {
                return;
            }
            for (auto& [index, list]: lists_)
            {
                (void) index;
                for (auto& entry: list)
                {
                    if (entry.clause_ref() == old_ref)
                    {
                        watch rewritten_entry {entry.blocking_literal(), new_ref, entry.is_binary()};
                        if (entry.is_binary())
                        {
                            rewritten_entry.set_binary_literal(entry.binary_literal());
                        }
                        entry = rewritten_entry;
                    }
                }
                std::vector<watch> unique_entries {};
                unique_entries.reserve(list.size());
                for (const auto& entry: list)
                {
                    const auto duplicate = std::find_if(unique_entries.begin(), unique_entries.end(),
                                                        [&](const watch& existing) noexcept
                                                        { return existing.clause_ref() == entry.clause_ref(); });
                    if (duplicate == unique_entries.end())
                    {
                        unique_entries.push_back(entry);
                    }
                }
                list.swap(unique_entries);
            }
        }

        /// @brief Rebuilds the per-literal partition after `compaction_service` renumbers variables.
        /// @param remap Callable mapping an old literal to its new, post-compaction literal.
        /// @throws None (noexcept).
        template <typename remap_fn>
        void reindex_after_compaction(remap_fn&& remap) noexcept
        {
            std::unordered_map<literal::raw_t, std::vector<watch>> rebuilt {};
            std::vector<literal::raw_t> indices {};
            indices.reserve(lists_.size());
            for (const auto& [index, list]: lists_)
            {
                if (!list.empty())
                {
                    indices.push_back(index);
                }
            }
            std::sort(indices.begin(), indices.end());
            for (const auto index: indices)
            {
                const auto& list = lists_.at(index);
                const literal old_lit {index};
                const auto new_lit = remap(old_lit);
                auto& target = rebuilt[new_lit.index_in_watch_bank()];
                for (const auto& entry: list)
                {
                    watch remapped_entry {remap(entry.blocking_literal()), entry.clause_ref(), entry.is_binary()};
                    if (entry.is_binary())
                    {
                        remapped_entry.set_binary_literal(remap(entry.binary_literal()));
                    }
                    const auto duplicate = std::find_if(target.begin(), target.end(),
                                                        [&](const watch& existing) noexcept
                                                        { return existing.clause_ref() == remapped_entry.clause_ref(); });
                    if (duplicate == target.end())
                    {
                        target.push_back(remapped_entry);
                    }
                }
            }
            lists_.swap(rebuilt);
        }

        /// @brief Prunes watch entries referring to clauses already marked garbage, shortening future list scans.
        /// @param is_garbage Predicate returning true for a clause `ref_t` that should be pruned from every list.
        /// @throws None (noexcept).
        template <typename predicate_t>
        void flush_large_watches(predicate_t&& is_garbage) noexcept
        {
            for (auto it = lists_.begin(); it != lists_.end();)
            {
                auto& list = it->second;
                list.erase(
                    std::remove_if(list.begin(), list.end(), [&](const watch& entry) noexcept { return is_garbage(entry.clause_ref()); }),
                    list.end());

                if (list.empty())
                {
                    it = lists_.erase(it);
                    continue;
                }

                ++it;
            }
        }

    private:
        std::vector<watch>& ensure_list(const literal lit) noexcept { return lists_[lit.index_in_watch_bank()]; }

        std::unordered_map<literal::raw_t, std::vector<watch>> lists_ {};
        mutable std::size_t iterate_call_count_ {};
    };
}
