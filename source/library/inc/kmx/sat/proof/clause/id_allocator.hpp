/// @file inc/kmx/sat/proof/clause/id_allocator.hpp
/// @brief Stabilizes logical clause identity across GC and compaction.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <unordered_map>
    #include <unordered_set>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/proof/clause/id.hpp>

namespace kmx::sat::proof::clause
{
    /// @brief Stabilizes logical clause identity across GC and compaction.
    /// @details
    /// `id_allocator` is the sole issuer and retirer of `proof::clause::id` values, called from
    /// `clause::storage::assign_proof_id` whenever a clause is created: `allocate_for_new_clause` mints a fresh,
    /// never-reused id for a given `clause::ref_t`; `preserve_on_relocation` re-associates an existing id with a
    /// clause's new `clause::ref_t` after `garbage_collector`/`compaction_service` moves it, so LRAT/FRAT antecedent
    /// chains that reference the id remain valid even though the physical reference changed; `retire_on_delete`
    /// releases an id once its clause is permanently deleted, ensuring it is never reissued to a different clause
    /// within the same proof.
    class id_allocator final
    {
    public:
        /// @brief Constructs an id allocator with no issued identities.
        /// @throws None (noexcept).
        id_allocator() noexcept = default;

        /// @brief Mints a fresh, never-reused proof clause identity for a newly created clause.
        /// @param ref Reference to the newly created clause.
        /// @return Newly allocated proof clause identity.
        /// @throws None (noexcept).
        id allocate_for_new_clause(const cdcl::clause::ref_t ref) noexcept
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

        /// @brief Returns the active proof identity currently associated with a clause reference.
        /// @param ref Clause reference to query.
        /// @return Associated identity, or invalid if no active mapping exists.
        /// @throws None (noexcept).
        [[nodiscard]] id id_for_clause_ref(const cdcl::clause::ref_t ref) const noexcept
        {
            if (!ref.valid())
                return {};

            const auto key = static_cast<std::uint64_t>(ref.offset());
            if (const auto it = ref_to_id_.find(key); it != ref_to_id_.end())
                return it->second;
            return {};
        }

        /// @brief Re-associates a clause's existing proof identity with its new physical reference after relocation.
        /// @param ref Clause's new reference after relocation.
        /// @throws None (noexcept).
        void preserve_on_relocation(const cdcl::clause::ref_t ref) noexcept
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

        /// @brief Re-associates a clause's existing proof identity from an old reference to a new one.
        /// @param old_ref Clause reference before relocation.
        /// @param new_ref Clause reference after relocation.
        /// @throws None (noexcept).
        void preserve_on_relocation(const cdcl::clause::ref_t old_ref, const cdcl::clause::ref_t new_ref) noexcept
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
            if (const auto new_it = ref_to_id_.find(new_key); new_it != ref_to_id_.end() && !new_it->second.equals(moved_id))
                return;

            ref_to_id_.erase(old_it);
            ref_to_id_[new_key] = moved_id;
            id_to_ref_[moved_id.value()] = new_key;
            active_ids_.insert(moved_id.value());
        }

        /// @brief Retires a proof clause identity once its clause has been permanently deleted.
        /// @param id_value Identity to retire.
        /// @throws None (noexcept).
        void retire_on_delete(const id id_value) noexcept
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

        /// @brief Retires the identity associated with a clause reference.
        /// @param ref Clause reference whose identity should be retired.
        /// @throws None (noexcept).
        void retire_on_delete(const cdcl::clause::ref_t ref) noexcept
        {
            const auto id_value = id_for_clause_ref(ref);
            retire_on_delete(id_value);
        }

        /// @brief Checks whether an id is still active.
        /// @param id_value Identity to query.
        /// @return True if the id is still active.
        /// @throws None (noexcept).
        [[nodiscard]] bool has_active_id(const id id_value) const noexcept
        {
            return id_value.valid() && active_ids_.find(id_value.value()) != active_ids_.end();
        }

    private:
        std::uint64_t next_id_ {1};
        std::unordered_map<std::uint64_t, id> ref_to_id_ {};
        std::unordered_map<id::value_t, std::uint64_t> id_to_ref_ {};
        std::unordered_set<id::value_t> active_ids_ {};
        id last_allocated_ {};
    };
}
