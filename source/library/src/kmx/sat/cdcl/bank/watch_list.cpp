/// @file library/src/kmx/sat/cdcl/bank/watch_list.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/bank/watch_list.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/bank/watch_list.hpp>

namespace kmx::sat::cdcl::bank
{
    void watch_list::watch_literal(const literal lit, const watch entry) noexcept
    {
        auto& list = ensure_list(lit);
        for (auto& existing: list)
        {
            if (existing == entry)
            {
                existing = entry;
                return;
            }
        }
        list.push_back(entry);
    }

    void watch_list::push_watch(const literal lit, const watch entry) noexcept
    {
        auto& list = ensure_list(lit);
        // Growth is rare (a learned clause's first watches, a list a moved watch lands on for the first time);
        // kept in a cold routine so the reallocation never sits inside the propagation loop.
        if (list.size() == list.capacity()) [[unlikely]]
            push_growing(list, entry);
        else
            list.push_back(entry);
    }

    void watch_list::unwatch_literal(const literal lit, const watch entry) noexcept
    {
        const auto index = static_cast<std::size_t>(lit.index_in_watch_bank());
        if (index < direct_index_limit)
        {
            if (index >= direct_lists_.size())
                return;
            auto& list = direct_lists_[index];
            list.erase(std::remove(list.begin(), list.end(), entry), list.end());
            return;
        }
        const auto it = overflow_lists_.find(static_cast<literal::raw_t>(index));
        if (it == overflow_lists_.end())
            return;
        auto& list = it->second;
        list.erase(std::remove(list.begin(), list.end(), entry), list.end());
        if (list.empty())
            overflow_lists_.erase(it);
    }

    [[nodiscard]] bool watch_list::contains(const literal lit, const watch entry) const noexcept
    {
        const auto* list = list_of(lit);
        if (list == nullptr)
            return false;
        return std::find(list->begin(), list->end(), entry) != list->end();
    }

    void watch_list::clear_entries() noexcept
    {
        for (auto& list: direct_lists_)
            list.clear();
        overflow_lists_.clear();
    }

    void watch_list::replace_clause_ref_after_gc(const clause::ref_t old_ref, const clause::ref_t new_ref) noexcept
    {
        if (!old_ref.valid() || !new_ref.valid() || (old_ref == new_ref)) [[unlikely]]
            return;
        auto rewrite_list = [&](literal_watches_t& list) noexcept
        {
            for (auto& entry: list)
                if (entry.clause_ref() == old_ref)
                    entry.set_clause_ref(new_ref);
            literal_watches_t unique_entries {};
            unique_entries.reserve(list.size());
            for (const auto& entry: list)
                if (std::find(unique_entries.begin(), unique_entries.end(), entry) == unique_entries.end())
                    unique_entries.push_back(entry);
            list.swap(unique_entries);
        };
        for (auto& list: direct_lists_)
            rewrite_list(list);
        for (auto& [index, list]: overflow_lists_)
            rewrite_list(list);
    }

    [[gnu::noinline, gnu::cold]] literal_watches_t& watch_list::grow_list(const std::size_t index) noexcept
    {
        if (index < direct_index_limit)
        {
            direct_lists_.resize(index + 1u);
            return direct_lists_[index];
        }
        return overflow_lists_[static_cast<literal::raw_t>(index)];
    }

    const literal_watches_t* watch_list::list_of(const literal lit) const noexcept
    {
        const auto index = static_cast<std::size_t>(lit.index_in_watch_bank());
        if (index < direct_index_limit)
            return (index < direct_lists_.size()) ? &direct_lists_[index] : nullptr;
        const auto it = overflow_lists_.find(static_cast<literal::raw_t>(index));
        return (it != overflow_lists_.end()) ? &it->second : nullptr;
    }
}
