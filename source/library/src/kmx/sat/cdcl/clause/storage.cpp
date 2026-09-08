/// @file library/src/kmx/sat/cdcl/clause/storage.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/clause/storage.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/clause/storage.hpp>

namespace kmx::sat::cdcl::clause
{
    void storage::destroy_clause(const ref_t ref) noexcept
    {
        const auto resolved = resolve_ref(ref);
        if (!arena_.contains(resolved))
            return;
        if (proof_ids_enabled_)
            id_allocator_.retire_on_delete(resolved);
        auto& header = arena_.header_at(resolved);
        header.flags &= bank::redundant_flag;
    }

    ref_t storage::relocate_clause(const ref_t ref) noexcept
    {
        const auto resolved = resolve_ref(ref);
        if (!is_alive(resolved))
            return {};

        const auto relocated = arena_.copy_clause(resolved);
        if (!relocated.valid())
            return {};

        arena_.header_at(resolved).flags &= static_cast<std::uint8_t>(~bank::alive_flag);
        set_redirect(resolved.offset(), relocated.offset());
        if (proof_ids_enabled_)
            id_allocator_.preserve_on_relocation(resolved, relocated);
        return relocated;
    }

    void storage::shrink_clause(const ref_t ref, const std::uint32_t new_size) noexcept
    {
        const auto resolved = resolve_ref(ref);
        arena_.truncate_literals(resolved, new_size);
        arena_.header_at(resolved).flags &= static_cast<std::uint8_t>(~bank::subsumption_checked_flag);
    }

    void storage::enable_proof_ids(const std::span<const ref_t> existing) noexcept
    {
        if (proof_ids_enabled_)
            return;
        proof_ids_enabled_ = true;
        for (const auto ref: existing)
            if (is_alive(ref))
                (void)id_allocator_.allocate_for_new_clause(resolve_ref(ref));
    }

    [[nodiscard]] proof::clause::id storage::proof_id_of(const ref_t ref) const noexcept
    {
        if (!proof_ids_enabled_)
            return {};
        return id_allocator_.id_for_clause_ref(resolve_ref(ref));
    }

    void storage::rewrite_clause_literals(const ref_t ref, const std::span<const literal> literals) noexcept
    {
        const auto resolved = resolve_ref(ref);
        if (!resolved.valid() || (literals.size() > arena_.literal_count(resolved)))
            return;
        arena_.write_literals(resolved, literals);
        arena_.truncate_literals(resolved, static_cast<std::uint32_t>(literals.size()));
        arena_.header_at(resolved).flags &= static_cast<std::uint8_t>(~bank::subsumption_checked_flag);
    }

    ref_t storage::create_clause(const std::span<const literal> literals, const bool redundant) noexcept
    {
        const auto ref = arena_.allocate_clause(literals.size());
        if (!ref.valid())
            return ref;
        arena_.write_literals(ref, literals);
        auto& header = arena_.header_at(ref);
        header.flags = static_cast<std::uint8_t>(bank::alive_flag | (redundant ? bank::redundant_flag : 0u));
        if (proof_ids_enabled_)
            (void)id_allocator_.allocate_for_new_clause(ref);
        return ref;
    }

    [[gnu::noinline]] ref_t storage::resolve_relocated_ref(const ref_t ref) const noexcept
    {
        if (!ref.valid()) [[unlikely]]
            return ref;
        const auto original = ref.offset();
        const auto slot = redirect_slot_of(original);
        if ((slot >= relocated_refs_.size()) || (relocated_refs_[slot] == no_redirect))
            return ref;

        auto resolved = ref;
        while (resolved.valid())
        {
            const auto current = redirect_slot_of(resolved.offset());
            if (current >= relocated_refs_.size())
                break;
            const auto target = relocated_refs_[current];
            if ((target == no_redirect) || (target == resolved.offset()))
                break;
            resolved = ref_t {target};
        }
        // Path compression: a chain of relocations is collapsed to its endpoint so the next lookup of the
        // same reference is a single step regardless of how many collection cycles it has survived.
        if (resolved.offset() != original)
            relocated_refs_[slot] = resolved.offset();
        return resolved;
    }

    void storage::set_redirect(const ref_t::offset_t from, const ref_t::offset_t to) noexcept
    {
        const auto slot = redirect_slot_of(from);
        if (slot >= relocated_refs_.size())
            relocated_refs_.resize(slot + 1u, no_redirect);
        relocated_refs_[slot] = to;
        has_relocations_ = true;
    }
}
