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
        void destroy_clause(const ref_t ref) noexcept;

        /// @brief Relocates a clause to a new arena position by copying, for example during evacuation.
        /// @param ref Reference to the clause to relocate.
        /// @return Updated reference valid in the current arena generation.
        /// @throws None (noexcept).
        ref_t relocate_clause(const ref_t ref) noexcept;

        /// @brief Shrinks a clause in place to a smaller literal count, for example after strengthening.
        /// @param ref Reference to the clause to shrink.
        /// @param new_size New literal count, which must not exceed the clause's current size.
        /// @throws None (noexcept).
        void shrink_clause(const ref_t ref, const std::uint32_t new_size) noexcept;

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
        void enable_proof_ids(const std::span<const ref_t> existing) noexcept;

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
        [[nodiscard]] proof::clause::id proof_id_of(const ref_t ref) const noexcept;

        /// @brief Reads back a clause's current literal payload as a copy.
        [[nodiscard]] std::vector<literal> literals_of(const ref_t ref) const noexcept { return arena_.read_literals(resolve_ref(ref)); }

        [[nodiscard]] [[gnu::always_inline]] inline std::span<const literal> view_literals(const ref_t ref) const noexcept
        {
            return arena_.view_literals(resolve_ref(ref));
        }

        [[nodiscard]] std::span<literal> mutable_literals(const ref_t ref) noexcept { return arena_.mutable_literals(resolve_ref(ref)); }

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

        /// @brief Header of a clause whose reference the caller has already passed through `resolve_ref`.
        [[nodiscard]] [[gnu::always_inline]] inline bank::clause_header& header_at_home(const ref_t resolved) noexcept
        {
            return arena_.header_at(resolved);
        }

        /// @brief Literals of a clause whose reference the caller has already passed through `resolve_ref`.
        [[nodiscard]] [[gnu::always_inline]] inline literal* literal_data_at_home(const ref_t resolved) noexcept
        {
            return arena_.literal_data(resolved);
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

        [[nodiscard]] [[gnu::always_inline]] inline bank::clause_header header_of(const ref_t ref) const noexcept
        {
            return arena_.header_of(resolve_ref(ref));
        }

        void set_header(const ref_t ref, const bank::clause_header header) noexcept { arena_.set_header(resolve_ref(ref), header); }

        /// @brief Rewrites a clause's stored literal payload in place after compaction or substitution.
        /// @param ref Reference to the clause whose literal payload should be replaced.
        /// @param literals New literals to store; the replacement may preserve or reduce the current size.
        /// @throws None (noexcept).
        void rewrite_clause_literals(const ref_t ref, const std::span<const literal> literals) noexcept;

        /// @brief Checks whether a clause was created as a learned (redundant) clause.
        [[nodiscard]] bool is_redundant(const ref_t ref) const noexcept
        {
            return (arena_.header_of(resolve_ref(ref)).flags & bank::redundant_flag) != 0u;
        }

        /// @brief Returns whether a clause is currently known to be alive in storage.
        /// @param ref Reference to the clause to query.
        /// @return True if the clause has been created and not yet destroyed.
        // Inlined by attribute: these three are a few instructions each and are called from every loop that
        // walks the database (the passes, the watch rebuild, the reduction), where a call per clause showed up.
        [[nodiscard]] [[gnu::always_inline]] inline bool is_alive(const ref_t ref) const noexcept
        {
            const auto resolved = resolve_ref(ref);
            return arena_.contains(resolved) && ((arena_.header_of(resolved).flags & bank::alive_flag) != 0u);
        }

        /// @brief Returns the number of arena bytes currently in use, live and dead clauses included.
        [[nodiscard]] std::size_t arena_bytes() const noexcept { return arena_.active_size(); }

        /// @brief Drops every clause created since the arena held `active_bytes`, byte for byte.
        /// @details Only valid when every clause created since then has been dropped from the lists that name it
        /// and no relocation has happened in between: the search's raw-formula probe uses it to leave the
        /// database exactly as it found it.
        void rollback_arena(const std::size_t active_bytes) noexcept { arena_.shrink_active(active_bytes); }

        /// @brief Copies the arena's active bytes into `image` (see `restore_arena`).
        void snapshot_arena(std::vector<std::uint8_t>& image) const noexcept { arena_.snapshot_active(image); }

        /// @brief Restores an image taken by `snapshot_arena`: clauses created since vanish and every clause's
        /// literal order is as it was, which a caller needs when propagation ran in between.
        void restore_arena(const std::vector<std::uint8_t>& image) noexcept { arena_.restore_active(image); }

        /// @brief Slides every listed clause towards the arena base, in offset order, and cuts the arena after the
        /// last one.
        /// @details `ordered_slots` must point at every live clause reference the caller holds, sorted by offset;
        /// each slot is rewritten to its new reference. `forward(old_offset, new_offset)` is invoked for every
        /// listed clause, moved or not, so the caller can rewrite the references it keeps elsewhere (watch lists,
        /// reasons). Any relocation redirects from `relocate_clause` are dropped, since nothing they pointed at
        /// survives.
        template <typename Forward>
        void compact(const std::span<ref_t* const> ordered_slots, Forward&& forward) noexcept;

    private:
        ref_t create_clause(const std::span<const literal> literals, const bool redundant) noexcept;

        /// @brief Follows a relocation chain to the reference's current home.
        /// @details Kept out of line: it only runs once `relocate_clause` has moved a clause, and inlining it would
        /// push the common case -- no relocations at all -- out of the caller.
        [[gnu::noinline]] ref_t resolve_relocated_ref(const ref_t ref) const noexcept;

        /// @brief Records that a clause has moved, so later references to the old offset resolve to the new one.
        void set_redirect(const ref_t::offset_t from, const ref_t::offset_t to) noexcept;

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

    template <typename Forward>
    void storage::compact(const std::span<ref_t* const> ordered_slots, Forward&& forward) noexcept
    {
        std::size_t write {};
        for (auto* const slot: ordered_slots)
        {
            const auto old_ref = resolve_ref(*slot);
            const auto old_offset = static_cast<std::size_t>(old_ref.offset());
            const auto byte_count = arena_.clause_byte_count(old_ref);
            arena_.move_clause_bytes(old_offset, write, byte_count);
            const ref_t new_ref {static_cast<ref_t::offset_t>(write)};
            if (proof_ids_enabled_ && (write != old_offset))
                id_allocator_.preserve_on_relocation(old_ref, new_ref);
            forward(old_ref, new_ref);
            *slot = new_ref;
            write += byte_count;
        }
        arena_.shrink_active(write);
        relocated_refs_.clear();
        has_relocations_ = false;
    }
}
