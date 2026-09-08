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
    /// @brief The watches registered for one literal.
    using literal_watches_t = std::vector<watch>;

    /// @brief Dense watch table indexed directly by literal, for the low literal range.
    using direct_watch_table_t = std::vector<literal_watches_t>;

    /// @brief Sparse watch table for literals beyond the dense range.
    using overflow_watch_map_t = std::unordered_map<literal::raw_t, literal_watches_t>;

    /// @brief All watch lists, partitioned by literal.
    /// @details
    /// `bank::watch_list` holds one list of `watch` entries per literal (positive and negative occurrence of every
    /// variable get independent lists), the core data structure unit propagation scans on every trail advance.
    /// `list_at` is the unchecked accessor the propagation loop uses once `reserve` has sized the table;
    /// `watch_literal`/`push_watch`/`unwatch_literal` add/remove entries. `rewrite_refs` re-targets every entry
    /// after the arena has been compacted and drops the entries of clauses that did not survive;
    /// `replace_clause_ref_after_gc`, `reindex_after_compaction` and `flush_large_watches` serve the
    /// evacuation-style collector and the simplification passes.
    /// @warning A `watch_list` left un-rewritten after garbage collection or compaction contains dangling
    /// `clause::ref_t` values; the rewrite is mandatory, not optional, cleanup after those operations.
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
            if ((literal_count < direct_index_limit) && (literal_count > direct_lists_.size()))
                direct_lists_.resize(literal_count);
        }

        /// @brief Returns the watch list of a literal whose index is below the reserved bound; no range check.
        /// @param literal_index Dense literal index (`literal::index_in_watch_bank`).
        [[nodiscard]] [[gnu::always_inline]] inline literal_watches_t& list_at(const std::size_t literal_index) noexcept
        {
            return direct_lists_[literal_index];
        }

        /// @brief Returns direct mutable reference to the watch list for the given literal.
        /// @param lit Literal whose watch list is accessed.
        /// @return Mutable reference to the watch list.
        /// @throws None (noexcept).
        [[nodiscard]] literal_watches_t& watches_of(const literal lit) noexcept
        {
            ++iterate_call_count_;
            return ensure_list(lit);
        }

        /// @brief Adds one watch entry to the list for the given literal, replacing an existing entry for the
        /// same clause.
        /// @param lit Literal whose list receives the new entry.
        /// @param entry Watch entry to add.
        /// @throws None (noexcept).
        void watch_literal(const literal lit, const watch entry) noexcept;

        /// @brief Appends a watch entry without checking whether the clause is already watched on this literal.
        /// @details `watch_literal` scans the whole target list to deduplicate. Watch replacement during propagation
        /// and the rebuild after garbage collection both know the clause is not yet in the target list, so they use
        /// this instead; keep `watch_literal` where a clause may genuinely be re-attached.
        /// @param lit Literal whose list receives the new entry.
        /// @param entry Watch entry to append.
        /// @throws None (noexcept).
        void push_watch(const literal lit, const watch entry) noexcept;

        [[gnu::noinline, gnu::cold]] static void push_growing(literal_watches_t& list, const watch entry) noexcept
        {
            list.push_back(entry);
        }

        /// @brief Appends to the list of a literal whose index is below the reserved bound (see `list_at`).
        [[gnu::always_inline]] inline void push_watch_at(const std::size_t literal_index, const watch entry) noexcept
        {
            auto& list = list_at(literal_index);
            if (list.size() == list.capacity()) [[unlikely]]
                push_growing(list, entry);
            else
                list.push_back(entry);
        }

        /// @brief Removes one watch entry from the list for the given literal.
        /// @param lit Literal whose list loses the entry.
        /// @param entry Watch entry to remove.
        /// @throws None (noexcept).
        void unwatch_literal(const literal lit, const watch entry) noexcept;

        /// @brief Checks whether the given watch entry is currently registered for the given literal.
        [[nodiscard]] bool contains(const literal lit, const watch entry) const noexcept;

        /// @brief Visits every watch entry currently registered for the given literal.
        template <typename Visitor>
        void iterate(const literal lit, Visitor&& visitor) const noexcept
        {
            ++iterate_call_count_;
            const auto* list = list_of(lit);
            if (list == nullptr)
                return;
            for (const auto& entry: *list)
                visitor(entry);
        }

        /// @brief Returns how many watch entries are currently registered for the given literal.
        [[nodiscard]] std::size_t size_of(const literal lit) const noexcept
        {
            const auto* list = list_of(lit);
            return (list != nullptr) ? list->size() : 0u;
        }

        /// @brief Returns how many watch partitions have been iterated since construction/reset.
        [[nodiscard]] std::size_t iterate_call_count() const noexcept { return iterate_call_count_; }

        /// @brief Adds to the partition-iteration diagnostic counter; the propagation loop reports in bulk.
        void add_iterate_count(const std::size_t count) noexcept { iterate_call_count_ += count; }

        /// @brief Clears watch-list diagnostic counters without changing stored watches.
        void reset_diagnostics() noexcept { iterate_call_count_ = 0u; }

        /// @brief Drops every watch entry while keeping the per-literal partition allocated.
        void clear_entries() noexcept;

        /// @brief Re-targets every entry after an arena compaction, dropping entries of clauses that did not survive.
        /// @param forward Callable `(clause::ref_t old_ref) -> clause::ref_t` returning the clause's new reference,
        /// or an invalid reference when the clause is gone.
        /// @throws None (noexcept).
        template <typename Forward>
        void rewrite_refs(Forward&& forward) noexcept;

        /// @brief Rewrites every stored clause reference after a garbage-collection cycle relocates one clause.
        void replace_clause_ref_after_gc(const clause::ref_t old_ref, const clause::ref_t new_ref) noexcept;

        /// @brief Rebuilds the per-literal partition after a pass renumbers variables.
        /// @param remap Callable mapping an old literal to its new, post-compaction literal.
        template <typename RemapFn>
        void reindex_after_compaction(RemapFn&& remap) noexcept;

        /// @brief Prunes watch entries referring to clauses already marked garbage, shortening future list scans.
        template <typename Predicate>
        void flush_large_watches(Predicate&& is_garbage) noexcept;

    private:
        static constexpr std::size_t direct_index_limit {std::size_t {1u} << 24};

        /// @brief The list of a literal, created if it does not exist yet.
        /// @details The common case, an existing direct list, is the only code that lands in the propagation
        /// loop; growing the table and the overflow map live in a cold routine that is never inlined. Inlined,
        /// their allocation and termination paths sat inside `propagate` and cost it a sixth more instructions
        /// per call through the register pressure they added, and whether the compiler split them out varied
        /// from build to build.
        [[gnu::always_inline]] inline literal_watches_t& ensure_list(const literal lit) noexcept
        {
            const auto index = static_cast<std::size_t>(lit.index_in_watch_bank());
            if (index < direct_lists_.size()) [[likely]]
                return direct_lists_[index];
            return grow_list(index);
        }

        [[gnu::noinline, gnu::cold]] literal_watches_t& grow_list(const std::size_t index) noexcept;

        const literal_watches_t* list_of(const literal lit) const noexcept;

        direct_watch_table_t direct_lists_ {};
        overflow_watch_map_t overflow_lists_ {};
        mutable std::size_t iterate_call_count_ {};
    };

    template <typename Forward>
    void watch_list::rewrite_refs(Forward&& forward) noexcept
    {
        const auto rewrite_list = [&](literal_watches_t& list) noexcept
        {
            std::size_t write {};
            for (const auto entry: list)
            {
                const auto new_ref = forward(entry.clause_ref());
                if (!new_ref.valid())
                    continue;
                list[write++] = watch {entry.blocking_literal(), new_ref, entry.is_binary()};
            }
            list.resize(write);
        };
        for (auto& list: direct_lists_)
            rewrite_list(list);
        for (auto& [index, list]: overflow_lists_)
            rewrite_list(list);
    }

    template <typename RemapFn>
    void watch_list::reindex_after_compaction(RemapFn&& remap) noexcept
    {
        direct_watch_table_t rebuilt_direct {};
        overflow_watch_map_t rebuilt_overflow {};

        auto insert_remapped = [&](const literal old_lit, const literal_watches_t& list) noexcept
        {
            if (list.empty())
                return;
            const auto new_lit = remap(old_lit);
            const auto new_index = static_cast<std::size_t>(new_lit.index_in_watch_bank());
            auto& target = (new_index < direct_index_limit) ? (
                                                                  [&]() noexcept -> literal_watches_t&
                                                                  {
                                                                      if (new_index >= rebuilt_direct.size())
                                                                          rebuilt_direct.resize(new_index + 1u);
                                                                      return rebuilt_direct[new_index];
                                                                  })() :
                                                              rebuilt_overflow[static_cast<literal::raw_t>(new_index)];

            for (const auto& entry: list)
            {
                const watch remapped_entry {remap(entry.blocking_literal()), entry.clause_ref(), entry.is_binary()};
                if (std::find(target.begin(), target.end(), remapped_entry) == target.end())
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

    template <typename Predicate>
    void watch_list::flush_large_watches(Predicate&& is_garbage) noexcept
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
}
