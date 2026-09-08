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
        explicit byte_storage(std::pmr::memory_resource* resource) noexcept: resource_ {resource}
        {
#if defined(__linux__)
            page_size_ = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
            if (page_size_ == 0u)
                page_size_ = 4096u;
            data_ = static_cast<std::uint8_t*>(::mmap(nullptr, reservation_size_, PROT_NONE,
                                                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
            if (data_ == MAP_FAILED)
                data_ = nullptr;
            else
                mapped_ = true;
#endif
        }

        ~byte_storage() noexcept
        {
#if defined(__linux__)
            if (data_ != nullptr && mapped_)
                ::munmap(data_, reservation_size_);
#endif
        }

        byte_storage(const byte_storage&) = delete;
        byte_storage& operator=(const byte_storage&) = delete;

        byte_storage(byte_storage&& other) noexcept: resource_ {other.resource_}
        {
            swap(other);
        }

        byte_storage& operator=(byte_storage&& other) noexcept
        {
            if (this != &other)
            {
                release();
                resource_ = other.resource_;
                swap(other);
            }
            return *this;
        }

        [[nodiscard]] std::size_t size() const noexcept { return size_; }
        [[nodiscard]] std::uint8_t* data() noexcept { return data_; }
        [[nodiscard]] const std::uint8_t* data() const noexcept { return data_; }

        [[nodiscard]] bool resize(const std::size_t new_size) noexcept
        {
            if (new_size < size_)
            {
                size_ = new_size;
                return true;
            }

#if defined(__linux__)
            if (mapped_)
            {
                // Commit in whole chunks and remember how much is committed: measuring from `size_` instead
                // issued one `mprotect` per page of growth, which was half the syscall time of a small solve.
                if (new_size > committed_size_)
                {
                    const auto required_commit = round_to_commit_chunk(new_size);
                    if (required_commit > reservation_size_ ||
                        ::mprotect(data_ + committed_size_, required_commit - committed_size_, PROT_READ | PROT_WRITE) != 0)
                        return false;
                    committed_size_ = required_commit;
                }
                size_ = new_size;
                return true;
            }
#endif
            if (new_size > vector_.max_size())
                return false;
            try
            {
                vector_.resize(new_size, 0u);
            }
            catch (...)
            {
                return false;
            }
            size_ = vector_.size();
            data_ = vector_.data();
            return true;
        }

        void clear() noexcept
        {
#if defined(__linux__)
            if (mapped_)
            {
                size_ = 0u;
                return;
            }
#endif
            vector_.clear();
            size_ = 0u;
            data_ = vector_.data();
        }

        void swap(byte_storage& other) noexcept
        {
            std::swap(size_, other.size_);
            std::swap(data_, other.data_);
#if defined(__linux__)
            std::swap(page_size_, other.page_size_);
            std::swap(committed_size_, other.committed_size_);
#endif
            std::swap(mapped_, other.mapped_);
            vector_.swap(other.vector_);
        }

    private:
        static constexpr std::size_t reservation_size_ {std::size_t {1} << 32u};
        static constexpr std::size_t commit_chunk_size_ {std::size_t {16} << 20u};

        [[nodiscard]] static std::size_t round_to_commit_chunk(const std::size_t value) noexcept
        {
            const auto remainder = value % commit_chunk_size_;
            return remainder == 0u ? value : value + (commit_chunk_size_ - remainder);
        }

        void release() noexcept
        {
#if defined(__linux__)
            if (mapped_ && data_ != nullptr)
                ::munmap(data_, reservation_size_);
            mapped_ = false;
            committed_size_ = 0u;
#endif
            vector_.clear();
            data_ = nullptr;
            size_ = 0u;
        }

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

        clause::ref_t allocate_clause(const std::size_t literal_count) noexcept
        {
            if (literal_count > std::numeric_limits<std::uint32_t>::max())
                return {};
            if (literal_count > (std::numeric_limits<std::size_t>::max() - sizeof(clause_header)) / sizeof(literal::raw_t))
                return {};
            const auto bytes = sizeof(clause_header) + literal_count * sizeof(literal::raw_t);
            const auto offset = active_.size();
            if (bytes > std::numeric_limits<clause::ref_t::offset_t>::max() - offset || !active_.resize(offset + bytes))
                return {};
            auto* const header = std::start_lifetime_as<clause_header>(active_.data() + offset);
            *header = clause_header {};
            header->size = static_cast<std::uint32_t>(literal_count);
            if (literal_count != 0u)
                (void) std::start_lifetime_as_array<literal>(active_.data() + offset + sizeof(clause_header), literal_count);
            return clause::ref_t {static_cast<clause::ref_t::offset_t>(offset)};
        }

        [[nodiscard]] clause_header header_of(const clause::ref_t ref) const noexcept
        {
            clause_header header {};
            if (!ref.valid())
                return header;
            const auto offset = static_cast<std::size_t>(ref.offset());
            if (offset + sizeof(header) <= active_.size())
                std::memcpy(&header, active_.data() + offset, sizeof(header));
            return header;
        }

        void set_header(const clause::ref_t ref, const clause_header header) noexcept
        {
            if (!ref.valid())
                return;
            const auto offset = static_cast<std::size_t>(ref.offset());
            if (offset + sizeof(header) <= active_.size())
                std::memcpy(active_.data() + offset, &header, sizeof(header));
        }

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
        [[nodiscard]] clause::ref_t copy_clause(const clause::ref_t ref) noexcept
        {
            if (!ref.valid())
                return {};
            const auto count = literal_count(ref);
            const auto source_offset = static_cast<std::size_t>(ref.offset());
            const auto byte_count = sizeof(clause_header) + static_cast<std::size_t>(count) * sizeof(literal::raw_t);
            if (source_offset + byte_count > active_.size())
                return {};

            const auto copied = allocate_clause(count);
            if (!copied.valid())
                return {};
            const auto destination_offset = static_cast<std::size_t>(copied.offset());
            std::memcpy(active_.data() + destination_offset, active_.data() + source_offset, byte_count);
            return copied;
        }

        /// @brief Writes a clause's literal payload at its already-allocated offset.
        /// @param ref Reference returned by `allocate_clause`.
        /// @param literals Literals to store; must not exceed the count `ref` was allocated with.
        /// @throws None (noexcept).
        void write_literals(const clause::ref_t ref, const std::span<const literal> literals) noexcept
        {
            if (!ref.valid())
                return;
            const auto offset = static_cast<std::size_t>(ref.offset());
            if (offset + sizeof(clause_header) > active_.size())
                return;
            const auto payload_offset = offset + sizeof(clause_header);
            const auto payload_bytes = literals.size() * sizeof(literal::raw_t);
            if (payload_offset + payload_bytes > active_.size())
                return;
            if (payload_bytes != 0u)
                std::memcpy(active_.data() + payload_offset, literals.data(), payload_bytes);
        }

        /// @brief Returns the stored literal count for a clause, as written by `allocate_clause`.
        /// @param ref Reference to query.
        /// @return Literal count, or zero if `ref` is invalid or out of range.
        /// @throws None (noexcept).
        [[nodiscard]] std::uint32_t literal_count(const clause::ref_t ref) const noexcept
        {
            if (!ref.valid())
                return 0u;
            const auto offset = static_cast<std::size_t>(ref.offset());
            if (offset + sizeof(clause_header) > active_.size())
                return 0u;
            return header_at(ref).size;
        }

        /// @brief Reads back a clause's stored literal payload.
        /// @param ref Reference to read.
        /// @return Literals stored for `ref`, or an empty vector if `ref` is invalid or out of range.
        /// @throws None (noexcept).
        [[nodiscard]] std::vector<literal> read_literals(const clause::ref_t ref) const noexcept
        {
            std::vector<literal> result {};
            const auto count = literal_count(ref);
            if (count == 0u)
                return result;
            const auto payload_offset = static_cast<std::size_t>(ref.offset()) + sizeof(clause_header);
            const auto payload_bytes = static_cast<std::size_t>(count) * sizeof(literal::raw_t);
            if (payload_offset + payload_bytes > active_.size())
                return result;
            result.resize(count);
            std::memcpy(result.data(), active_.data() + payload_offset, payload_bytes);
            return result;
        }

        [[nodiscard]] std::span<const literal> view_literals(const clause::ref_t ref) const noexcept
        {
            const auto count = literal_count(ref);
            if (count == 0u)
                return {};
            return {literal_data(ref), count};
        }

        [[nodiscard]] std::span<literal> mutable_literals(const clause::ref_t ref) noexcept
        {
            const auto count = literal_count(ref);
            if (count == 0u)
                return {};
            return {literal_data(ref), count};
        }

        /// @brief Truncates a clause's stored literal count in place, without moving or reallocating its bytes.
        /// @param ref Reference to the clause to shrink.
        /// @param new_count New literal count, which must not exceed the clause's current stored count.
        /// @throws None (noexcept).
        void truncate_literals(const clause::ref_t ref, const std::uint32_t new_count) noexcept
        {
            if (!ref.valid() || new_count > literal_count(ref))
                return;
            auto& header = header_at(ref);
            header.size = new_count;
            header.flags |= shrunken_flag;
        }

        void materialize_clause(const clause::ref_t ref) noexcept
        {
            if (!ref.valid())
                return;
            const auto offset = static_cast<std::size_t>(ref.offset());
            const auto byte_count = clause_byte_count(ref);
            if (byte_count == 0u || offset + byte_count > active_.size())
                return;

            if (survivor_.size() < offset + byte_count)
            {
                if (!survivor_.resize(offset + byte_count))
                    return;
            }

            std::memcpy(survivor_.data() + offset, active_.data() + offset, byte_count);
        }

        [[nodiscard]] bool contains(const clause::ref_t ref) const noexcept
        {
            if (!ref.valid())
                return false;
            return static_cast<std::size_t>(ref.offset()) < active_.size();
        }

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
                (void) active_.resize(new_size);
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
