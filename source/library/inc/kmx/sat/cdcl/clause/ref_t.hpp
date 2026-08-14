/// @file inc/kmx/sat/cdcl/clause/ref_t.hpp
/// @brief Compressed reference, invalid state, comparisons, offset conversion; physical clause identifier in the
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <compare>
    #include <cstdint>
#endif

namespace kmx::sat::cdcl::clause
{
    /// @brief Compressed reference, invalid state, comparisons, offset conversion; physical clause identifier in the
    /// current arena.
    /// @details
    /// `ref_t` deliberately encodes only a byte/word offset into the currently active `bank::arena`, never a pointer,
    /// so that a moving `garbage_collector` cycle or `compaction_service` pass can relocate clause storage and simply
    /// rewrite every stored `ref_t` (in `bank::watch_list`, decision reasons, the proof clause-id table) without
    /// leaving any dangling pointer in the system; this is the mechanism that lets the architecture avoid pointers or
    /// views that could unsafely survive GC, compaction, or clause shrinking. `ref_t` is distinct from
    /// `proof::clause::id`: this type identifies where a clause currently lives, while `proof::clause::id` identifies
    /// what a clause logically is across relocations, deletions, and proof events.
    /// @warning A `ref_t` is only meaningful relative to the arena generation that produced it; holding one across a
    /// `garbage_collector::collect` or `compaction_service` cycle without going through the corresponding rewrite
    /// step is undefined behavior at the architectural level.
    class ref_t final
    {
    public:
        using offset_t = std::uint32_t;
        static constexpr offset_t invalid_offset {static_cast<offset_t>(-1)};

        /// @brief Constructs an invalid clause reference.
        /// @throws None (noexcept).
        ref_t() noexcept = default;
        /// @brief Constructs a clause reference from a raw arena offset.
        /// @param offset Byte/word offset into the currently active arena.
        /// @throws None (noexcept).
        explicit constexpr ref_t(const offset_t offset) noexcept: offset_ {offset} {}

        /// @brief Checks whether this reference points to a real arena offset rather than the invalid sentinel.
        /// @return True if this reference is valid.
        /// @throws None (noexcept).
        constexpr bool valid() const noexcept { return offset_ != invalid_offset; }

        /// @brief Checks whether this reference is the invalid sentinel.
        /// @return True if this reference is invalid.
        /// @throws None (noexcept).
        constexpr bool invalid() const noexcept { return !valid(); }

        /// @brief Returns the raw arena offset for direct access by `bank::arena`/`clause::storage`.
        /// @return Arena offset value.
        /// @throws None (noexcept).
        constexpr offset_t offset() const noexcept { return offset_; }

        /// @brief Compares two clause references by their raw offset.
        /// @return Ordering/equality result following the underlying offset value.
        /// @throws None (noexcept).
        constexpr auto operator<=>(const ref_t&) const noexcept = default;

        /// @brief Returns a reference to the invalid sentinel value.
        /// @return Invalid clause reference.
        /// @throws None (noexcept).
        static constexpr ref_t invalid_reference() noexcept { return ref_t {invalid_offset}; }

    private:
        offset_t offset_ {invalid_offset};
    };
}
