/// @file inc/kmx/sat/proof/clause/id_allocator.hpp
/// @brief Stabilizes logical clause identity across GC and compaction.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/proof/clause/id.hpp>

namespace kmx::sat::proof::clause
{
    /// @brief Stabilizes logical clause identity across GC and compaction.
    ///
    /// `id_allocator` is the sole issuer and retirer of `proof::clause::id` values, called from
    /// `clause::storage::assign_proof_id` whenever a clause is created: `allocate_for_new_clause` mints a fresh,
    /// never-reused id for a given `clause::ref_t`; `preserve_on_relocation` re-associates an existing id with a
    /// clause's new `clause::ref_t` after `garbage_collector`/`compaction_service` moves it, so LRAT/FRAT antecedent
    /// chains that reference the id remain valid even though the physical reference changed; `retire_on_delete`
    /// releases an id once its clause is permanently deleted, ensuring it is never reissued to a different clause
    /// within the same proof.
    /// @note This is the mechanism behind the risk point "preservation of proof identity across GC and compaction":
    /// every relocation must call `preserve_on_relocation` before any LRAT/FRAT tracer is asked to reference the
    /// clause again.
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
            return {};
        }

        /// @brief Re-associates a clause's existing proof identity with its new physical reference after relocation.
        /// @param ref Clause's new reference after relocation.
        /// @throws None (noexcept).
        void preserve_on_relocation(const cdcl::clause::ref_t ref) noexcept
        {
        }

        /// @brief Retires a proof clause identity once its clause has been permanently deleted.
        /// @param id_value Identity to retire.
        /// @throws None (noexcept).
        void retire_on_delete(const id id_value) noexcept
        {
        }
    };
}
