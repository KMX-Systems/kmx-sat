/// @file inc/kmx/sat/cdcl/clause/storage.hpp
/// @brief The real owner of physical clauses and of their relation to proof ids.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <span>
#endif
#include <kmx/sat/cdcl/bank/arena.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/clause/id.hpp>

namespace kmx::sat::cdcl::clause
{
    /// @brief The real owner of physical clauses and of their relation to proof ids.
    ///
    /// `clause::storage` sits directly on top of `bank::arena` and is the only subsystem allowed to create, destroy,
    /// relocate, or shrink clauses at the byte level; `clause::database` (logical tiers/watch relationship) and every
    /// higher-level consumer go through this type rather than touching `bank::arena` directly. `assign_proof_id`
    /// binds a newly created clause to a `proof::clause::id` so that `proof::proof_manager` and its tracers can refer
    /// to the clause by a stable logical identity independent of `relocate_clause`/`shrink_clause` rewriting its
    /// physical `ref_t`. `resolve_ref` re-validates a reference against the current arena generation, returning an
    /// updated reference after a relocation.
    /// @warning `relocate_clause` and `shrink_clause` invalidate any previously obtained `clause::view` for the
    /// affected clause; callers must re-resolve through `resolve_ref` before further access.
    class storage final
    {
    public:
        /// @brief Constructs storage with an empty arena.
        /// @throws None (noexcept).
        storage() noexcept = default;

        /// @brief Creates a new original (non-redundant) problem clause from the given literals.
        /// @param literals Literals composing the clause, already validated and normalized.
        /// @return Reference to the newly created clause.
        /// @throws None (noexcept).
        ref_t create_original_clause(const std::span<const literal> literals) noexcept
        {
            return {};
        }

        /// @brief Creates a new learned (redundant) clause derived from conflict analysis.
        /// @param literals Literals composing the learned clause.
        /// @return Reference to the newly created clause.
        /// @throws None (noexcept).
        ref_t create_learned_clause(const std::span<const literal> literals) noexcept
        {
            return {};
        }

        /// @brief Physically destroys a clause, retiring its `proof::clause::id` if one was assigned.
        /// @param ref Reference to the clause to destroy; must not currently be a reason clause.
        /// @throws None (noexcept).
        void destroy_clause(const ref_t ref) noexcept
        {
        }

        /// @brief Relocates a clause to a new arena position, for example during garbage collection.
        /// @param ref Reference to the clause to relocate.
        /// @return Updated reference valid in the current arena generation.
        /// @throws None (noexcept).
        ref_t relocate_clause(const ref_t ref) noexcept
        {
            return ref;
        }

        /// @brief Shrinks a clause in place to a smaller literal count, for example after strengthening.
        /// @param ref Reference to the clause to shrink.
        /// @param new_size New literal count, which must not exceed the clause's current size.
        /// @throws None (noexcept).
        void shrink_clause(const ref_t ref, const std::uint32_t new_size) noexcept
        {
        }

        /// @brief Re-validates a reference against the current arena generation, updating it if relocated.
        /// @param ref Reference to resolve.
        /// @return Reference valid in the current arena generation.
        /// @throws None (noexcept).
        ref_t resolve_ref(const ref_t ref) const noexcept
        {
            return ref;
        }

        /// @brief Allocates and binds a stable `proof::clause::id` to a clause for proof/checking purposes.
        /// @param ref Reference to the clause receiving a proof identity.
        /// @return Newly assigned proof clause identity.
        /// @throws None (noexcept).
        proof::clause::id assign_proof_id(const ref_t ref) noexcept
        {
            return {};
        }

    private:
        bank::arena arena_ {};
    };
}
