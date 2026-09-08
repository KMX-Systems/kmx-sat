/// @file library/src/kmx/sat/proof/clause/id_allocator.cpp
/// @brief Out-of-line definitions declared by kmx/sat/proof/clause/id_allocator.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/proof/clause/id_allocator.hpp>

namespace kmx::sat::proof::clause
{
    id id_allocator::allocate_for_new_clause(const cdcl::clause::ref_t ref) noexcept
    {
        if (!ref.valid())
            return {};

        const auto key = static_cast<std::uint64_t>(ref.offset());
        if (const auto it = ref_to_id_.find(key); it != ref_to_id_.end())
            return it->second;

        const id allocated {next_id_++};
        ref_to_id_[key] = allocated;
        id_to_ref_[allocated.value()] = key;
        active_ids_.insert(allocated.value());
        last_allocated_ = allocated;
        return allocated;
    }

    [[nodiscard]] id id_allocator::id_for_clause_ref(const cdcl::clause::ref_t ref) const noexcept
    {
        if (!ref.valid())
            return {};

        const auto key = static_cast<std::uint64_t>(ref.offset());
        if (const auto it = ref_to_id_.find(key); it != ref_to_id_.end())
            return it->second;
        return {};
    }

    void id_allocator::preserve_on_relocation(const cdcl::clause::ref_t ref) noexcept
    {
        if (!ref.valid() || !last_allocated_.valid())
            return;

        const auto key = static_cast<std::uint64_t>(ref.offset());
        if (ref_to_id_.find(key) != ref_to_id_.end())
            return;

        ref_to_id_[key] = last_allocated_;
        id_to_ref_[last_allocated_.value()] = key;
        active_ids_.insert(last_allocated_.value());
    }

    void id_allocator::preserve_on_relocation(const cdcl::clause::ref_t old_ref, const cdcl::clause::ref_t new_ref) noexcept
    {
        if (!old_ref.valid() || !new_ref.valid())
            return;

        const auto old_key = static_cast<std::uint64_t>(old_ref.offset());
        const auto new_key = static_cast<std::uint64_t>(new_ref.offset());
        if (old_key == new_key)
            return;

        const auto old_it = ref_to_id_.find(old_key);
        if (old_it == ref_to_id_.end())
            return;

        const auto moved_id = old_it->second;
        if (const auto new_it = ref_to_id_.find(new_key); (new_it != ref_to_id_.end()) && !new_it->second.equals(moved_id))
            return;

        ref_to_id_.erase(old_it);
        ref_to_id_[new_key] = moved_id;
        id_to_ref_[moved_id.value()] = new_key;
        active_ids_.insert(moved_id.value());
    }

    void id_allocator::retire_on_delete(const id id_value) noexcept
    {
        if (!id_value.valid())
            return;

        if (const auto it = id_to_ref_.find(id_value.value()); it != id_to_ref_.end())
        {
            ref_to_id_.erase(it->second);
            id_to_ref_.erase(it);
        }
        active_ids_.erase(id_value.value());
    }
}
