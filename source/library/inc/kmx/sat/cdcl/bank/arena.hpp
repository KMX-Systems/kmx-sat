/// @file inc/kmx/sat/cdcl/bank/arena.hpp
/// @brief Manages the active and survivor arenas through PMR.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstring>
    #include <limits>
    #include <memory>
    #include <memory_resource>
    #include <span>
    #include <type_traits>
    #include <utility>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#if defined(__linux__)
    #include <sys/mman.h>
    #include <unistd.h>
#endif
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::bank
{
    /// @brief The physical sixteen-byte record stored in the arena directly ahead of a clause's literals.
    /// @details Everything reduction and analysis need about a clause lives here, so no side table has to be
    /// consulted: the literal count, the glue (LBD), a retention activity, the flag byte, a saturating usage
    /// counter and the retention tier. Header and both watched literals share one cache line.
    struct clause_header final
    {
        std::uint32_t size {};
        std::uint32_t glue {};
        float activity {};
        std::uint8_t flags {};
        std::uint8_t used {};
        std::uint8_t tier {};
        std::uint8_t reserved {};
    };

    static_assert(sizeof(clause_header) == 16u);
    static_assert(std::is_trivially_copyable_v<clause_header>);

    inline constexpr std::uint8_t redundant_flag {1u << 0u};
    inline constexpr std::uint8_t garbage_flag {1u << 1u};
    inline constexpr std::uint8_t reason_flag {1u << 2u};
    inline constexpr std::uint8_t shrunken_flag {1u << 3u};
    inline constexpr std::uint8_t tracked_flag {1u << 4u};
    inline constexpr std::uint8_t alive_flag {1u << 5u};
    /// Set by the forward subsumer on every clause it has checked; cleared whenever the clause body changes, so
    /// a later run knows which pairs of clauses it can skip (two unchanged clauses cannot newly subsume).
    inline constexpr std::uint8_t subsumption_checked_flag {1u << 6u};

    class byte_storage final
    {
    public:
        explicit byte_storage(std::pmr::memory_resource* resource) noexcept;

        ~byte_storage() noexcept;

        byte_storage(const byte_storage&) = delete;
        byte_storage& operator=(const byte_storage&) = delete;

        byte_storage(byte_storage&& other) noexcept: resource_ {other.resource_} { swap(other); }

        byte_storage& operator=(byte_storage&& other) noexcept;

        [[nodiscard]] std::size_t size() const noexcept { return size_; }
        [[nodiscard]] std::uint8_t* data() noexcept { return data_; }
        [[nodiscard]] const std::uint8_t* data() const noexcept { return data_; }

        [[nodiscard]] bool resize(const std::size_t new_size) noexcept;

        void clear() noexcept;

        void swap(byte_storage& other) noexcept;

    private:
        static constexpr std::size_t reservation_size_ {std::size_t {1u} << 32u};
        static constexpr std::size_t commit_chunk_size_ {std::size_t {16u} << 20u};

        [[nodiscard]] static std::size_t round_to_commit_chunk(const std::size_t value) noexcept
        {
            const auto remainder = value % commit_chunk_size_;
            return (remainder == 0u) ? value : value + (commit_chunk_size_ - remainder);
        }

        void release() noexcept;

        std::pmr::memory_resource* resource_ {std::pmr::get_default_resource()};
        std::size_t size_ {};
        std::uint8_t* data_ {};
#if defined(__linux__)
        std::size_t page_size_ {4096u};
        std::size_t committed_size_ {};
#endif
        bool mapped_ {};
        std::pmr::vector<std::uint8_t> vector_ {resource_};
    };

    /// @brief Manages the active and survivor arenas through PMR.
    /// @details
    /// `arena` owns low-level clause bytes. `allocate_clause` reserves storage in the active region,
    /// `write_literals`/`read_literals`/`view_literals` access a clause's literal payload, `header_at`/
    /// `literal_data` give the unchecked in-place access the propagation loop needs, and `compact_range` is the
    /// in-place move used by garbage collection. `materialize_clause`/`swap_survivor` support evacuation-style
    /// staging for callers that prefer copying collection.
    class arena final
    {
    public:
        static_assert(sizeof(literal) == sizeof(literal::raw_t));
        static_assert(std::is_trivially_copyable_v<literal>);

        arena() noexcept = default;

        explicit arena(std::pmr::memory_resource* resource) noexcept: resource_ {resource} {}

        clause::ref_t allocate_clause(const std::size_t literal_count) noexcept;

        [[nodiscard]] clause_header header_of(const clause::ref_t ref) const noexcept;

        void set_header(const clause::ref_t ref, const clause_header header) noexcept;

        void update_header(const clause::ref_t ref, const clause_header header) noexcept { set_header(ref, header); }

        /// @brief Unchecked in-place header access for a reference known to be valid and in range.
        [[nodiscard]] [[gnu::always_inline]] inline clause_header& header_at(const clause::ref_t ref) noexcept
        {
            return *std::launder(reinterpret_cast<clause_header*>(active_.data() + ref.offset()));
        }

        [[nodiscard]] [[gnu::always_inline]] inline const clause_header& header_at(const clause::ref_t ref) const noexcept
        {
            return *std::launder(reinterpret_cast<const clause_header*>(active_.data() + ref.offset()));
        }

        /// @brief Unchecked in-place literal access for a reference known to be valid and in range.
        [[nodiscard]] [[gnu::always_inline]] inline literal* literal_data(const clause::ref_t ref) noexcept
        {
            return std::launder(reinterpret_cast<literal*>(active_.data() + ref.offset() + sizeof(clause_header)));
        }

        [[nodiscard]] [[gnu::always_inline]] inline const literal* literal_data(const clause::ref_t ref) const noexcept
        {
            return std::launder(reinterpret_cast<const literal*>(active_.data() + ref.offset() + sizeof(clause_header)));
        }

        /// @brief Copies a clause payload directly inside the active arena.
        [[nodiscard]] clause::ref_t copy_clause(const clause::ref_t ref) noexcept;

        /// @brief Writes a clause's literal payload at its already-allocated offset.
        /// @param ref Reference returned by `allocate_clause`.
        /// @param literals Literals to store; must not exceed the count `ref` was allocated with.
        /// @throws None (noexcept).
        void write_literals(const clause::ref_t ref, const std::span<const literal> literals) noexcept;

        /// @brief Returns the stored literal count for a clause, as written by `allocate_clause`.
        /// @param ref Reference to query.
        /// @return Literal count, or zero if `ref` is invalid or out of range.
        /// @throws None (noexcept).
        [[nodiscard]] std::uint32_t literal_count(const clause::ref_t ref) const noexcept;

        /// @brief Reads back a clause's stored literal payload.
        /// @param ref Reference to read.
        /// @return Literals stored for `ref`, or an empty vector if `ref` is invalid or out of range.
        /// @throws None (noexcept).
        [[nodiscard]] std::vector<literal> read_literals(const clause::ref_t ref) const noexcept;

        [[nodiscard]] std::span<const literal> view_literals(const clause::ref_t ref) const noexcept;

        [[nodiscard]] std::span<literal> mutable_literals(const clause::ref_t ref) noexcept;

        /// @brief Truncates a clause's stored literal count in place, without moving or reallocating its bytes.
        /// @param ref Reference to the clause to shrink.
        /// @param new_count New literal count, which must not exceed the clause's current stored count.
        /// @throws None (noexcept).
        void truncate_literals(const clause::ref_t ref, const std::uint32_t new_count) noexcept;

        void materialize_clause(const clause::ref_t ref) noexcept;

        [[nodiscard]] bool contains(const clause::ref_t ref) const noexcept;

        /// @brief Returns the number of bytes currently in use by the active arena.
        [[nodiscard]] std::size_t active_size() const noexcept { return active_.size(); }

        /// @brief Returns the byte footprint of the clause stored at `ref`, header included.
        [[nodiscard]] std::size_t clause_byte_count(const clause::ref_t ref) const noexcept
        {
            const auto count = literal_count(ref);
            return sizeof(clause_header) + static_cast<std::size_t>(count) * sizeof(literal::raw_t);
        }

        /// @brief Moves `byte_count` bytes of clause storage from `from` down to `to` inside the active arena.
        /// @details The in-place primitive of garbage collection: `to` never exceeds `from`, so live clauses are
        /// slid towards the arena base in offset order and the arena is then cut at the end of the last one.
        void move_clause_bytes(const std::size_t from, const std::size_t to, const std::size_t byte_count) noexcept
        {
            if (from != to)
                std::memmove(active_.data() + to, active_.data() + from, byte_count);
        }

        /// @brief Lowers the active arena's used size after compaction; committed pages stay for reuse.
        /// @brief Copies the active region into `image`, for a caller that will put it back with `restore_active`.
        void snapshot_active(std::vector<std::uint8_t>& image) const noexcept
        {
            image.assign(active_.data(), active_.data() + active_.size());
        }

        /// @brief Puts back an image taken by `snapshot_active`: the active region shrinks to the image's size and
        /// its bytes are overwritten, so clauses created since vanish and clauses that were permuted by
        /// propagation regain their literal order.
        void restore_active(const std::vector<std::uint8_t>& image) noexcept
        {
            shrink_active(image.size());
            std::copy(image.begin(), image.end(), active_.data());
        }

        void shrink_active(const std::size_t new_size) noexcept
        {
            if (new_size <= active_.size())
                (void)active_.resize(new_size);
        }

        void prepare_gc() noexcept { survivor_.clear(); }

        void swap_survivor() noexcept
        {
            active_.swap(survivor_);
            survivor_.clear();
        }

        void release_inactive() noexcept { survivor_.clear(); }

    private:
        std::pmr::memory_resource* resource_ {std::pmr::get_default_resource()};
        byte_storage active_ {resource_};
        byte_storage survivor_ {resource_};
    };
}
