/// @file library/src/kmx/sat/cdcl/bank/arena.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/bank/arena.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/bank/arena.hpp>

namespace kmx::sat::cdcl::bank
{
    byte_storage::byte_storage(std::pmr::memory_resource* resource) noexcept: resource_ {resource}
    {
#if defined(__linux__)
        page_size_ = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
        if (page_size_ == 0u)
            page_size_ = 4096u;
        data_ =
            static_cast<std::uint8_t*>(::mmap(nullptr, reservation_size_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0L));
        if (data_ == MAP_FAILED)
            data_ = nullptr;
        else
            mapped_ = true;
#endif
    }

    byte_storage::~byte_storage() noexcept
    {
#if defined(__linux__)
        if ((data_ != nullptr) && mapped_)
            ::munmap(data_, reservation_size_);
#endif
    }

    byte_storage& byte_storage::operator=(byte_storage&& other) noexcept
    {
        if (this != &other)
        {
            release();
            resource_ = other.resource_;
            swap(other);
        }
        return *this;
    }

    [[nodiscard]] bool byte_storage::resize(const std::size_t new_size) noexcept
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
                if ((required_commit > reservation_size_) ||
                    (::mprotect(data_ + committed_size_, required_commit - committed_size_, PROT_READ | PROT_WRITE) != 0))
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

    void byte_storage::clear() noexcept
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

    void byte_storage::swap(byte_storage& other) noexcept
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

    void byte_storage::release() noexcept
    {
#if defined(__linux__)
        if (mapped_ && (data_ != nullptr))
            ::munmap(data_, reservation_size_);
        mapped_ = false;
        committed_size_ = 0u;
#endif
        vector_.clear();
        data_ = nullptr;
        size_ = 0u;
    }

    clause::ref_t arena::allocate_clause(const std::size_t literal_count) noexcept
    {
        if (literal_count > std::numeric_limits<std::uint32_t>::max())
            return {};
        if (literal_count > (std::numeric_limits<std::size_t>::max() - sizeof(clause_header)) / sizeof(literal::raw_t))
            return {};
        const auto bytes = sizeof(clause_header) + literal_count * sizeof(literal::raw_t);
        const auto offset = active_.size();
        if ((bytes > std::numeric_limits<clause::ref_t::offset_t>::max() - offset) || !active_.resize(offset + bytes))
            return {};
        auto* const header = std::start_lifetime_as<clause_header>(active_.data() + offset);
        *header = clause_header {};
        header->size = static_cast<std::uint32_t>(literal_count);
        if (literal_count != 0u)
            (void)std::start_lifetime_as_array<literal>(active_.data() + offset + sizeof(clause_header), literal_count);
        return clause::ref_t {static_cast<clause::ref_t::offset_t>(offset)};
    }

    [[nodiscard]] clause_header arena::header_of(const clause::ref_t ref) const noexcept
    {
        clause_header header {};
        if (!ref.valid())
            return header;
        const auto offset = static_cast<std::size_t>(ref.offset());
        if (offset + sizeof(header) <= active_.size())
            std::memcpy(&header, active_.data() + offset, sizeof(header));
        return header;
    }

    void arena::set_header(const clause::ref_t ref, const clause_header header) noexcept
    {
        if (!ref.valid()) [[unlikely]]
            return;
        const auto offset = static_cast<std::size_t>(ref.offset());
        if (offset + sizeof(header) <= active_.size())
            std::memcpy(active_.data() + offset, &header, sizeof(header));
    }

    [[nodiscard]] clause::ref_t arena::copy_clause(const clause::ref_t ref) noexcept
    {
        if (!ref.valid()) [[unlikely]]
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

    void arena::write_literals(const clause::ref_t ref, const std::span<const literal> literals) noexcept
    {
        if (!ref.valid()) [[unlikely]]
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

    [[nodiscard]] std::uint32_t arena::literal_count(const clause::ref_t ref) const noexcept
    {
        if (!ref.valid()) [[unlikely]]
            return 0u;
        const auto offset = static_cast<std::size_t>(ref.offset());
        if (offset + sizeof(clause_header) > active_.size())
            return 0u;
        return header_at(ref).size;
    }

    [[nodiscard]] std::vector<literal> arena::read_literals(const clause::ref_t ref) const noexcept
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

    [[nodiscard]] std::span<const literal> arena::view_literals(const clause::ref_t ref) const noexcept
    {
        const auto count = literal_count(ref);
        if (count == 0u)
            return {};
        return {literal_data(ref), count};
    }

    [[nodiscard]] std::span<literal> arena::mutable_literals(const clause::ref_t ref) noexcept
    {
        const auto count = literal_count(ref);
        if (count == 0u)
            return {};
        return {literal_data(ref), count};
    }

    void arena::truncate_literals(const clause::ref_t ref, const std::uint32_t new_count) noexcept
    {
        if (!ref.valid() || (new_count > literal_count(ref)))
            return;
        auto& header = header_at(ref);
        header.size = new_count;
        header.flags |= shrunken_flag;
    }

    void arena::materialize_clause(const clause::ref_t ref) noexcept
    {
        if (!ref.valid()) [[unlikely]]
            return;
        const auto offset = static_cast<std::size_t>(ref.offset());
        const auto byte_count = clause_byte_count(ref);
        if ((byte_count == 0u) || (offset + byte_count > active_.size()))
            return;

        if (survivor_.size() < offset + byte_count)
        {
            if (!survivor_.resize(offset + byte_count))
                return;
        }

        std::memcpy(survivor_.data() + offset, active_.data() + offset, byte_count);
    }

    [[nodiscard]] bool arena::contains(const clause::ref_t ref) const noexcept
    {
        if (!ref.valid()) [[unlikely]]
            return false;
        return static_cast<std::size_t>(ref.offset()) < active_.size();
    }
}
