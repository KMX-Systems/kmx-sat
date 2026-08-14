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

        /// @brief Pre-reserves capacity for literal watch lists.
        /// @param literal_count Upper bound on literal index.
        /// @throws None (noexcept).
        void reserve(const std::size_t literal_count) noexcept
        {
            if (literal_count < direct_index_limit && literal_count > direct_lists_.size())
                direct_lists_.resize(literal_count);
        }

        /// @brief Returns direct mutable reference to the watch list for the given literal.
        /// @param lit Literal whose watch list is accessed.
        /// @return Mutable reference to the watch list.
        /// @throws None (noexcept).
        [[nodiscard]] std::vector<watch>& watches_of(const literal lit) noexcept
        {
            ++iterate_call_count_;
            return ensure_list(lit);
        }

        /// @brief Adds one watch entry to the list for the given literal.
        /// @param lit Literal whose list receives the new entry.
        /// @param entry Watch entry to add.
        /// @throws None (noexcept).
        void watch_literal(const literal lit, const watch entry) noexcept
        {
            auto& list = ensure_list(lit);
            for (auto& existing: list)
            {
                if (existing.clause_ref() == entry.clause_ref())
                {
                    existing = entry;
                    return;
                }
            }
            list.push_back(entry);
        }

        /// @brief Removes one watch entry from the list for the given literal.
        /// @param lit Literal whose list loses the entry.
        /// @param entry Watch entry to remove.
        /// @throws None (noexcept).
        void unwatch_literal(const literal lit, const watch entry) noexcept
        {
            const auto index = static_cast<std::size_t>(lit.index_in_watch_bank());
            if (index < direct_index_limit)
            {
                if (index >= direct_lists_.size())
                    return;
                auto& list = direct_lists_[index];
                list.erase(std::remove_if(list.begin(), list.end(),
                                          [&](const auto& current) noexcept { return current.clause_ref() == entry.clause_ref(); }),
                           list.end());
                return;
            }
            const auto it = overflow_lists_.find(static_cast<literal::raw_t>(index));
            if (it == overflow_lists_.end())
                return;
            auto& list = it->second;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&](const auto& current) noexcept { return current.clause_ref() == entry.clause_ref(); }),
                       list.end());
            if (list.empty())
                overflow_lists_.erase(it);
        }

        /// @brief Checks whether the given watch entry is currently registered for the given literal.
        /// @param lit Literal whose watch list is inspected.
        /// @param entry Watch entry to look for.
        /// @return True if an identical watch entry exists in the list for `lit`.
        [[nodiscard]] bool contains(const literal lit, const watch entry) const noexcept
        {
            const auto* list = list_of(lit);
            if (list == nullptr)
                return false;
            return std::find(list->begin(), list->end(), entry) != list->end();
        }

        /// @brief Visits every watch entry currently registered for the given literal.
        /// @param lit Literal whose watch list is visited.
        /// @param visitor Callable invoked with each `watch` entry currently registered for `lit`.
        /// @throws None (noexcept).
        template <typename visitor_t>
        void iterate(const literal lit, visitor_t&& visitor) const noexcept
        {
            ++iterate_call_count_;
            const auto* list = list_of(lit);
            if (list == nullptr)
                return;
            for (const auto& entry: *list)
                visitor(entry);
        }

        /// @brief Returns how many watch entries are currently registered for the given literal.
        /// @param lit Literal whose watch list size is queried.
        /// @return Number of watch entries currently registered for `lit`.
        /// @throws None (noexcept).
        [[nodiscard]] std::size_t size_of(const literal lit) const noexcept
        {
            const auto* list = list_of(lit);
            return list != nullptr ? list->size() : 0u;
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
                return;
            auto rewrite_list = [&](std::vector<watch>& list) noexcept
            {
                for (auto& entry: list)
                {
                    if (entry.clause_ref() == old_ref)
                    {
                        watch rewritten_entry {entry.blocking_literal(), new_ref, entry.is_binary()};
                        if (entry.is_binary())
                            rewritten_entry.set_binary_literal(entry.binary_literal());
                        entry = rewritten_entry;
                    }
                }
                std::vector<watch> unique_entries {};
                unique_entries.reserve(list.size());
                for (const auto& entry: list)
                {
                    const auto duplicate = std::find_if(unique_entries.begin(), unique_entries.end(), [&](const watch& existing) noexcept
                                                        { return existing.clause_ref() == entry.clause_ref(); });
                    if (duplicate == unique_entries.end())
                        unique_entries.push_back(entry);
                }
                list.swap(unique_entries);
            };
            for (auto& list: direct_lists_)
                rewrite_list(list);
            for (auto& [index, list]: overflow_lists_)
                rewrite_list(list);
        }

        /// @brief Rebuilds the per-literal partition after `compaction_service` renumbers variables.
        /// @param remap Callable mapping an old literal to its new, post-compaction literal.
        /// @throws None (noexcept).
        template <typename remap_fn>
        void reindex_after_compaction(remap_fn&& remap) noexcept
        {
            std::vector<std::vector<watch>> rebuilt_direct {};
            std::unordered_map<literal::raw_t, std::vector<watch>> rebuilt_overflow {};

            auto insert_remapped = [&](const literal old_lit, const std::vector<watch>& list) noexcept
            {
                if (list.empty())
                    return;
                const auto new_lit = remap(old_lit);
                const auto new_index = static_cast<std::size_t>(new_lit.index_in_watch_bank());
                auto& target = (new_index < direct_index_limit) ?
                                   ([&]() noexcept -> std::vector<watch>& {
                                       if (new_index >= rebuilt_direct.size())
                                           rebuilt_direct.resize(new_index + 1u);
                                       return rebuilt_direct[new_index];
                                   })() :
                                   rebuilt_overflow[static_cast<literal::raw_t>(new_index)];

                for (const auto& entry: list)
                {
                    watch remapped_entry {remap(entry.blocking_literal()), entry.clause_ref(), entry.is_binary()};
                    if (entry.is_binary())
                        remapped_entry.set_binary_literal(remap(entry.binary_literal()));
                    const auto duplicate = std::find_if(target.begin(), target.end(), [&](const watch& existing) noexcept
                                                        { return existing.clause_ref() == remapped_entry.clause_ref(); });
                    if (duplicate == target.end())
                        target.push_back(remapped_entry);
                }
            };

            for (std::size_t index {}; index < direct_lists_.size(); ++index)
                insert_remapped(literal {static_cast<literal::raw_t>(index)}, direct_lists_[index]);

            std::vector<literal::raw_t> overflow_indices {};
            overflow_indices.reserve(overflow_lists_.size());
            for (const auto& [index, list]: overflow_lists_)
                if (!list.empty())
                    overflow_indices.push_back(index);
            std::sort(overflow_indices.begin(), overflow_indices.end());
            for (const auto index: overflow_indices)
                insert_remapped(literal {index}, overflow_lists_.at(index));

            direct_lists_.swap(rebuilt_direct);
            overflow_lists_.swap(rebuilt_overflow);
        }

        /// @brief Prunes watch entries referring to clauses already marked garbage, shortening future list scans.
        /// @param is_garbage Predicate returning true for a clause `ref_t` that should be pruned from every list.
        /// @throws None (noexcept).
        template <typename predicate_t>
        void flush_large_watches(predicate_t&& is_garbage) noexcept
        {
            for (auto& list: direct_lists_)
            {
                list.erase(
                    std::remove_if(list.begin(), list.end(), [&](const watch& entry) noexcept { return is_garbage(entry.clause_ref()); }),
                    list.end());
            }
            for (auto it = overflow_lists_.begin(); it != overflow_lists_.end();)
            {
                auto& list = it->second;
                list.erase(
                    std::remove_if(list.begin(), list.end(), [&](const watch& entry) noexcept { return is_garbage(entry.clause_ref()); }),
                    list.end());

                if (list.empty())
                {
                    it = overflow_lists_.erase(it);
                    continue;
                }

                ++it;
            }
        }

    private:
        static constexpr std::size_t direct_index_limit {std::size_t {1} << 24};

        std::vector<watch>& ensure_list(const literal lit) noexcept
        {
            const auto index = static_cast<std::size_t>(lit.index_in_watch_bank());
            if (index < direct_index_limit)
            {
                if (index >= direct_lists_.size())
                    direct_lists_.resize(index + 1u);
                return direct_lists_[index];
            }
            return overflow_lists_[static_cast<literal::raw_t>(index)];
        }

        const std::vector<watch>* list_of(const literal lit) const noexcept
        {
            const auto index = static_cast<std::size_t>(lit.index_in_watch_bank());
            if (index < direct_index_limit)
                return index < direct_lists_.size() ? &direct_lists_[index] : nullptr;
            const auto it = overflow_lists_.find(static_cast<literal::raw_t>(index));
            return it != overflow_lists_.end() ? &it->second : nullptr;
        }

        std::vector<std::vector<watch>> direct_lists_ {};
        std::unordered_map<literal::raw_t, std::vector<watch>> overflow_lists_ {};
        mutable std::size_t iterate_call_count_ {};
    };
}
