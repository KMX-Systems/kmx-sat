/// @file inc/kmx/sat/cdcl/clause/storage.hpp
/// @brief The real owner of physical clauses and of their relation to proof ids.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <span>
    #include <unordered_map>
    #include <unordered_set>
    #include <vector>
#endif
#include <kmx/sat/cdcl/bank/arena.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/clause/id.hpp>
#include <kmx/sat/proof/clause/id_allocator.hpp>

namespace kmx::sat::cdcl::clause
{
    /// @brief The real owner of physical clauses and of their relation to proof ids.
    ///
    /// @details
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
        ref_t create_original_clause(const std::span<const literal> literals) noexcept { return create_clause(literals, false); }

        /// @brief Creates a new learned (redundant) clause derived from conflict analysis.
        /// @param literals Literals composing the learned clause.
        /// @return Reference to the newly created clause.
        /// @throws None (noexcept).
        ref_t create_learned_clause(const std::span<const literal> literals) noexcept { return create_clause(literals, true); }

        /// @brief Physically destroys a clause, retiring its `proof::clause::id` if one was assigned.
        /// @param ref Reference to the clause to destroy; must not currently be a reason clause.
        /// @throws None (noexcept).
        void destroy_clause(const ref_t ref) noexcept
        {
            const auto resolved = resolve_ref(ref);
            id_allocator_.retire_on_delete(resolved);
            redundant_.erase(resolved.offset());
            alive_.erase(resolved.offset());
        }

        /// @brief Relocates a clause to a new arena position, for example during garbage collection.
        /// @param ref Reference to the clause to relocate.
        /// @return Updated reference valid in the current arena generation.
        /// @throws None (noexcept).
        ref_t relocate_clause(const ref_t ref) noexcept
        {
            const auto resolved = resolve_ref(ref);
            if (!is_alive(resolved))
                return {};

            const auto literal_count = arena_.literal_count(resolved);
            const auto literals = arena_.read_literals(resolved);
            const auto relocated = arena_.allocate_clause(literal_count);
            arena_.write_literals(relocated, literals);

            if (is_redundant(resolved))
            {
                redundant_.erase(resolved.offset());
                redundant_.insert(relocated.offset());
            }

            alive_.erase(resolved.offset());
            alive_.insert(relocated.offset());
            relocated_refs_[resolved.offset()] = relocated.offset();
            id_allocator_.preserve_on_relocation(resolved, relocated);
            return relocated;
        }

        /// @brief Shrinks a clause in place to a smaller literal count, for example after strengthening.
        /// @param ref Reference to the clause to shrink.
        /// @param new_size New literal count, which must not exceed the clause's current size.
        /// @throws None (noexcept).
        void shrink_clause(const ref_t ref, const std::uint32_t new_size) noexcept { arena_.truncate_literals(resolve_ref(ref), new_size); }

        /// @brief Re-validates a reference against the current arena generation, updating it if relocated.
        /// @param ref Reference to resolve.
        /// @return Reference valid in the current arena generation.
        /// @throws None (noexcept).
        ref_t resolve_ref(const ref_t ref) const noexcept
        {
            if (relocated_refs_.empty())
                return ref;

            auto resolved = ref;
            const auto original = ref.offset();
            while (resolved.valid())
            {
                const auto it = relocated_refs_.find(resolved.offset());
                if (it == relocated_refs_.end() || it->second == resolved.offset())
                    break;
                resolved = ref_t {it->second};
            }
            if (resolved.offset() != original)
                relocated_refs_[original] = resolved.offset();
            return resolved;
        }

        /// @brief Allocates and binds a stable `proof::clause::id` to a clause for proof/checking purposes.
        /// @param ref Reference to the clause receiving a proof identity.
        /// @return Newly assigned proof clause identity.
        /// @throws None (noexcept).
        proof::clause::id assign_proof_id(const ref_t ref) noexcept { return id_allocator_.allocate_for_new_clause(ref); }

        /// @brief Returns the stable proof identity currently bound to a clause, if any.
        /// @param ref Reference to the clause to query.
        /// @return Bound proof identity, or invalid if none is assigned.
        /// @throws None (noexcept).
        [[nodiscard]] proof::clause::id proof_id_of(const ref_t ref) const noexcept
        {
            return id_allocator_.id_for_clause_ref(resolve_ref(ref));
        }

        /// @brief Reads back a clause's current literal payload.
        /// @param ref Reference to the clause to query.
        /// @return Literals currently stored for `ref`.
        /// @throws None (noexcept).
        [[nodiscard]] std::vector<literal> literals_of(const ref_t ref) const noexcept { return arena_.read_literals(resolve_ref(ref)); }

        [[nodiscard]] std::span<const literal> view_literals(const ref_t ref) const noexcept
        {
            return arena_.view_literals(resolve_ref(ref));
        }

        /// @brief Returns the number of literals currently stored for a clause.
        /// @param ref Reference to the clause to query.
        /// @return Literal count, or zero for an invalid reference.
        /// @throws None (noexcept).
        [[nodiscard]] std::uint32_t literal_count(const ref_t ref) const noexcept { return arena_.literal_count(resolve_ref(ref)); }

        /// @brief Rewrites a clause's stored literal payload in place after compaction or substitution.
        /// @param ref Reference to the clause whose literal payload should be replaced.
        /// @param literals New literals to store; the replacement may preserve or reduce the current size.
        /// @throws None (noexcept).
        void rewrite_clause_literals(const ref_t ref, const std::span<const literal> literals) noexcept
        {
            const auto resolved = resolve_ref(ref);
            if (!resolved.valid() || literals.size() > arena_.literal_count(resolved))
                return;
            arena_.write_literals(resolved, literals);
            arena_.truncate_literals(resolved, static_cast<std::uint32_t>(literals.size()));
        }

        /// @brief Checks whether a clause was created as a learned (redundant) clause.
        /// @param ref Reference to the clause to query.
        /// @return True if `ref` was created via `create_learned_clause`.
        /// @throws None (noexcept).
        [[nodiscard]] bool is_redundant(const ref_t ref) const noexcept
        {
            const auto resolved = resolve_ref(ref);
            return redundant_.find(resolved.offset()) != redundant_.end();
        }

        /// @brief Returns whether a clause is currently known to be alive in storage.
        /// @param ref Reference to the clause to query.
        /// @return True if the clause has been created and not yet destroyed.
        [[nodiscard]] bool is_alive(const ref_t ref) const noexcept
        {
            const auto resolved = resolve_ref(ref);
            return resolved.valid() && alive_.find(resolved.offset()) != alive_.end();
        }

    private:
        ref_t create_clause(const std::span<const literal> literals, const bool redundant) noexcept
        {
            const auto ref = arena_.allocate_clause(literals.size());
            arena_.write_literals(ref, literals);
            if (redundant)
                redundant_.insert(ref.offset());
            alive_.insert(ref.offset());
            assign_proof_id(ref);
            return ref;
        }

        bank::arena arena_ {};
        proof::clause::id_allocator id_allocator_ {};
        std::unordered_set<ref_t::offset_t> redundant_ {};
        std::unordered_set<ref_t::offset_t> alive_ {};
        mutable std::unordered_map<ref_t::offset_t, ref_t::offset_t> relocated_refs_ {};
    };
}
