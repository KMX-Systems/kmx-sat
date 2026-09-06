/// @file inc/kmx/sat/cdcl/clause/storage.hpp
/// @brief The real owner of physical clauses and of their relation to proof ids.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <span>
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
    /// @details
    /// `clause::storage` sits directly on top of `bank::arena` and is the only subsystem allowed to create, destroy,
    /// relocate, or shrink clauses at the byte level; `clause::database` (logical tiers/watch relationship) and every
    /// higher-level consumer go through this type rather than touching `bank::arena` directly. Liveness is a flag in
    /// the clause header itself, so no side table has to be consulted or kept in sync.
    ///
    /// Proof identities are allocated at clause creation while tracking is on (the default); the solver core turns
    /// tracking off until a proof consumer attaches, so a solve with no proof consumer performs no hash-table work
    /// per learned clause.
    /// `resolve_ref` re-validates a reference against the current arena generation after `relocate_clause`;
    /// `compact` is the in-place collection used by the search, which rewrites references through the caller's
    /// callback and leaves nothing to resolve afterwards.
    /// @warning `relocate_clause`, `shrink_clause` and `compact` invalidate any previously obtained literal view for
    /// the affected clauses; callers must re-acquire views afterwards.
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
            if (!arena_.contains(resolved))
                return;
            if (proof_ids_enabled_)
                id_allocator_.retire_on_delete(resolved);
            auto& header = arena_.header_at(resolved);
            header.flags &= bank::redundant_flag;
        }

        /// @brief Relocates a clause to a new arena position by copying, for example during evacuation.
        /// @param ref Reference to the clause to relocate.
        /// @return Updated reference valid in the current arena generation.
        /// @throws None (noexcept).
        ref_t relocate_clause(const ref_t ref) noexcept
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

        /// @brief Shrinks a clause in place to a smaller literal count, for example after strengthening.
        /// @param ref Reference to the clause to shrink.
        /// @param new_size New literal count, which must not exceed the clause's current size.
        /// @throws None (noexcept).
        void shrink_clause(const ref_t ref, const std::uint32_t new_size) noexcept { arena_.truncate_literals(resolve_ref(ref), new_size); }

        /// @brief Re-validates a reference against the current arena generation, updating it if relocated.
        /// @param ref Reference to resolve.
        /// @return Reference valid in the current arena generation.
        /// @throws None (noexcept).
        [[gnu::always_inline]] inline ref_t resolve_ref(const ref_t ref) const noexcept
        {
            if (!has_relocations_) [[likely]]
                return ref;
            return resolve_relocated_ref(ref);
        }

        /// @brief Switches per-clause proof identity tracking on or off.
        /// @details Tracking is on by default so that a storage used on its own behaves as a proof-capable store.
        /// The solver core switches it off until a proof consumer attaches, because allocating an identity is
        /// hash-table work per learned clause that nothing reads in a proof-free solve.
        void set_proof_id_tracking(const bool enabled) noexcept { proof_ids_enabled_ = enabled; }

        /// @brief Starts allocating proof identities, assigning one to every clause in `existing` first.
        /// @details Called when a proof consumer attaches. Clauses created afterwards get an identity at creation.
        void enable_proof_ids(const std::span<const ref_t> existing) noexcept
        {
            if (proof_ids_enabled_)
                return;
            proof_ids_enabled_ = true;
            for (const auto ref: existing)
                if (is_alive(ref))
                    (void) id_allocator_.allocate_for_new_clause(resolve_ref(ref));
        }

        [[nodiscard]] bool proof_ids_enabled() const noexcept { return proof_ids_enabled_; }

        /// @brief Allocates and binds a stable `proof::clause::id` to a clause for proof/checking purposes.
        /// @param ref Reference to the clause receiving a proof identity.
        /// @return Newly assigned proof clause identity.
        /// @throws None (noexcept).
        proof::clause::id assign_proof_id(const ref_t ref) noexcept
        {
            proof_ids_enabled_ = true;
            return id_allocator_.allocate_for_new_clause(resolve_ref(ref));
        }

        /// @brief Returns the stable proof identity currently bound to a clause, if any.
        /// @param ref Reference to the clause to query.
        /// @return Bound proof identity, or invalid if none is assigned.
        /// @throws None (noexcept).
        [[nodiscard]] proof::clause::id proof_id_of(const ref_t ref) const noexcept
        {
            if (!proof_ids_enabled_)
                return {};
            return id_allocator_.id_for_clause_ref(resolve_ref(ref));
        }

        /// @brief Reads back a clause's current literal payload as a copy.
        [[nodiscard]] std::vector<literal> literals_of(const ref_t ref) const noexcept { return arena_.read_literals(resolve_ref(ref)); }

        [[nodiscard]] std::span<const literal> view_literals(const ref_t ref) const noexcept
        {
            return arena_.view_literals(resolve_ref(ref));
        }

        [[nodiscard]] std::span<literal> mutable_literals(const ref_t ref) noexcept
        {
            return arena_.mutable_literals(resolve_ref(ref));
        }

        /// @brief Unchecked in-place header access; `ref` must be a valid, current, in-range reference.
        [[nodiscard]] [[gnu::always_inline]] inline bank::clause_header& header_at(const ref_t ref) noexcept
        {
            return arena_.header_at(resolve_ref(ref));
        }

        [[nodiscard]] [[gnu::always_inline]] inline const bank::clause_header& header_at(const ref_t ref) const noexcept
        {
            return arena_.header_at(resolve_ref(ref));
        }

        /// @brief Unchecked in-place literal access; `ref` must be a valid, current, in-range reference.
        [[nodiscard]] [[gnu::always_inline]] inline literal* literal_data(const ref_t ref) noexcept
        {
            return arena_.literal_data(resolve_ref(ref));
        }

        [[nodiscard]] [[gnu::always_inline]] inline const literal* literal_data(const ref_t ref) const noexcept
        {
            return arena_.literal_data(resolve_ref(ref));
        }

        /// @brief Returns the number of literals currently stored for a clause.
        /// @param ref Reference to the clause to query.
        /// @return Literal count, or zero for an invalid reference.
        /// @throws None (noexcept).
        [[nodiscard]] std::uint32_t literal_count(const ref_t ref) const noexcept { return arena_.literal_count(resolve_ref(ref)); }

        [[nodiscard]] bank::clause_header header_of(const ref_t ref) const noexcept { return arena_.header_of(resolve_ref(ref)); }

        void set_header(const ref_t ref, const bank::clause_header header) noexcept { arena_.set_header(resolve_ref(ref), header); }

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
        [[nodiscard]] bool is_redundant(const ref_t ref) const noexcept
        {
            return (arena_.header_of(resolve_ref(ref)).flags & bank::redundant_flag) != 0u;
        }

        /// @brief Returns whether a clause is currently known to be alive in storage.
        /// @param ref Reference to the clause to query.
        /// @return True if the clause has been created and not yet destroyed.
        [[nodiscard]] bool is_alive(const ref_t ref) const noexcept
        {
            const auto resolved = resolve_ref(ref);
            return arena_.contains(resolved) && (arena_.header_of(resolved).flags & bank::alive_flag) != 0u;
        }

        /// @brief Returns the number of arena bytes currently in use, live and dead clauses included.
        [[nodiscard]] std::size_t arena_bytes() const noexcept { return arena_.active_size(); }

        /// @brief Slides every listed clause towards the arena base, in offset order, and cuts the arena after the
        /// last one.
        /// @details `ordered_slots` must point at every live clause reference the caller holds, sorted by offset;
        /// each slot is rewritten to its new reference. `forward(old_offset, new_offset)` is invoked for every
        /// listed clause, moved or not, so the caller can rewrite the references it keeps elsewhere (watch lists,
        /// reasons). Any relocation redirects from `relocate_clause` are dropped, since nothing they pointed at
        /// survives.
        template <typename forward_t>
        void compact(const std::span<ref_t* const> ordered_slots, forward_t&& forward) noexcept
        {
            std::size_t write {};
            for (auto* const slot: ordered_slots)
            {
                const auto old_ref = resolve_ref(*slot);
                const auto old_offset = static_cast<std::size_t>(old_ref.offset());
                const auto byte_count = arena_.clause_byte_count(old_ref);
                arena_.move_clause_bytes(old_offset, write, byte_count);
                const ref_t new_ref {static_cast<ref_t::offset_t>(write)};
                if (proof_ids_enabled_ && write != old_offset)
                    id_allocator_.preserve_on_relocation(old_ref, new_ref);
                forward(old_ref, new_ref);
                *slot = new_ref;
                write += byte_count;
            }
            arena_.shrink_active(write);
            relocated_refs_.clear();
            has_relocations_ = false;
        }

    private:
        ref_t create_clause(const std::span<const literal> literals, const bool redundant) noexcept
        {
            const auto ref = arena_.allocate_clause(literals.size());
            if (!ref.valid())
                return ref;
            arena_.write_literals(ref, literals);
            auto& header = arena_.header_at(ref);
            header.flags = static_cast<std::uint8_t>(bank::alive_flag | (redundant ? bank::redundant_flag : 0u));
            if (proof_ids_enabled_)
                (void) id_allocator_.allocate_for_new_clause(ref);
            return ref;
        }

        /// @brief Follows a relocation chain to the reference's current home.
        /// @details Kept out of line: it only runs once `relocate_clause` has moved a clause, and inlining it would
        /// push the common case -- no relocations at all -- out of the caller.
        [[gnu::noinline]] ref_t resolve_relocated_ref(const ref_t ref) const noexcept
        {
            if (!ref.valid())
                return ref;
            const auto original = ref.offset();
            const auto slot = redirect_slot_of(original);
            if (slot >= relocated_refs_.size() || relocated_refs_[slot] == no_redirect)
                return ref;

            auto resolved = ref;
            while (resolved.valid())
            {
                const auto current = redirect_slot_of(resolved.offset());
                if (current >= relocated_refs_.size())
                    break;
                const auto target = relocated_refs_[current];
                if (target == no_redirect || target == resolved.offset())
                    break;
                resolved = ref_t {target};
            }
            // Path compression: a chain of relocations is collapsed to its endpoint so the next lookup of the
            // same reference is a single step regardless of how many collection cycles it has survived.
            if (resolved.offset() != original)
                relocated_refs_[slot] = resolved.offset();
            return resolved;
        }

        /// @brief Records that a clause has moved, so later references to the old offset resolve to the new one.
        void set_redirect(const ref_t::offset_t from, const ref_t::offset_t to) noexcept
        {
            const auto slot = redirect_slot_of(from);
            if (slot >= relocated_refs_.size())
                relocated_refs_.resize(slot + 1u, no_redirect);
            relocated_refs_[slot] = to;
            has_relocations_ = true;
        }

        /// Arena offsets are 4-byte aligned, so `offset / 4` indexes the redirection table densely.
        static std::size_t redirect_slot_of(const ref_t::offset_t offset) noexcept
        {
            return static_cast<std::size_t>(offset) / sizeof(std::uint32_t);
        }

        static constexpr ref_t::offset_t no_redirect {ref_t::invalid_offset};

        bank::arena arena_ {};
        proof::clause::id_allocator id_allocator_ {};
        /// @brief Old-offset to new-offset redirection for `relocate_clause`, indexed by arena offset.
        mutable std::vector<ref_t::offset_t> relocated_refs_ {};
        bool has_relocations_ {};
        bool proof_ids_enabled_ {true};
    };
}
