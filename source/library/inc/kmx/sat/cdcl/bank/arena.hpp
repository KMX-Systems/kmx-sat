/// @file inc/kmx/sat/cdcl/bank/arena.hpp
/// @brief Manages the active and survivor arenas through PMR.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstring>
    #include <memory_resource>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::bank
{
    /// @brief Manages the active and survivor arenas through PMR.
    /// @details
    /// `arena` owns low-level clause bytes and supports simple evacuation-style garbage-collection staging.
    /// `allocate_clause` reserves storage in the active region, `write_literals`/`read_literals` give byte-level
    /// access to a clause's literal payload (a `std::uint32_t` count prefix followed by packed `literal::raw_t`
    /// values), `materialize_clause` copies selected payloads into the survivor region, and `swap_survivor` promotes
    /// survivors as the new active storage.
    class arena final
    {
    public:
        arena() noexcept = default;

        explicit arena(std::pmr::memory_resource* resource) noexcept : resource_ {resource}
        {
        }

        clause::ref_t allocate_clause(const std::size_t literal_count) noexcept
        {
            const auto bytes = sizeof(std::uint32_t) + literal_count * sizeof(literal::raw_t);
            const auto offset = active_.size();
            active_.resize(active_.size() + bytes, 0u);
            const auto count_prefix = static_cast<std::uint32_t>(literal_count);
            std::memcpy(active_.data() + offset, &count_prefix, sizeof(count_prefix));
            return clause::ref_t {static_cast<clause::ref_t::offset_t>(offset)};
        }

        /// @brief Writes a clause's literal payload at its already-allocated offset.
        /// @param ref Reference returned by `allocate_clause`.
        /// @param literals Literals to store; must not exceed the count `ref` was allocated with.
        /// @throws None (noexcept).
        void write_literals(const clause::ref_t ref, const std::span<const literal> literals) noexcept
        {
            if (!ref.valid())
            {
                return;
            }
            const auto offset = static_cast<std::size_t>(ref.offset());
            if (offset + sizeof(std::uint32_t) > active_.size())
            {
                return;
            }
            const auto payload_offset = offset + sizeof(std::uint32_t);
            const auto payload_bytes = literals.size() * sizeof(literal::raw_t);
            if (payload_offset + payload_bytes > active_.size())
            {
                return;
            }
            for (std::size_t index {0}; index < literals.size(); ++index)
            {
                const auto raw = literals[index].raw();
                std::memcpy(active_.data() + payload_offset + index * sizeof(literal::raw_t), &raw, sizeof(raw));
            }
        }

        /// @brief Returns the stored literal count for a clause, as written by `allocate_clause`.
        /// @param ref Reference to query.
        /// @return Literal count, or zero if `ref` is invalid or out of range.
        /// @throws None (noexcept).
        [[nodiscard]] std::uint32_t literal_count(const clause::ref_t ref) const noexcept
        {
            if (!ref.valid())
            {
                return 0u;
            }
            const auto offset = static_cast<std::size_t>(ref.offset());
            if (offset + sizeof(std::uint32_t) > active_.size())
            {
                return 0u;
            }
            std::uint32_t count {0u};
            std::memcpy(&count, active_.data() + offset, sizeof(count));
            return count;
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
            {
                return result;
            }
            const auto payload_offset = static_cast<std::size_t>(ref.offset()) + sizeof(std::uint32_t);
            const auto payload_bytes = static_cast<std::size_t>(count) * sizeof(literal::raw_t);
            if (payload_offset + payload_bytes > active_.size())
            {
                return result;
            }
            result.reserve(count);
            for (std::uint32_t index {0}; index < count; ++index)
            {
                literal::raw_t raw {0u};
                std::memcpy(&raw, active_.data() + payload_offset + index * sizeof(literal::raw_t), sizeof(raw));
                result.push_back(literal {raw});
            }
            return result;
        }

        /// @brief Truncates a clause's stored literal count in place, without moving or reallocating its bytes.
        /// @param ref Reference to the clause to shrink.
        /// @param new_count New literal count, which must not exceed the clause's current stored count.
        /// @throws None (noexcept).
        void truncate_literals(const clause::ref_t ref, const std::uint32_t new_count) noexcept
        {
            if (!ref.valid() || new_count > literal_count(ref))
            {
                return;
            }
            const auto offset = static_cast<std::size_t>(ref.offset());
            std::memcpy(active_.data() + offset, &new_count, sizeof(new_count));
        }

        void materialize_clause(const clause::ref_t ref) noexcept
        {
            if (!ref.valid())
            {
                return;
            }
            const auto offset = static_cast<std::size_t>(ref.offset());
            if (offset >= active_.size())
            {
                return;
            }
            survivor_.insert(survivor_.end(), active_.begin() + offset, active_.begin() + offset + 32u);
        }

        bool contains(const clause::ref_t ref) const noexcept
        {
            if (!ref.valid())
            {
                return false;
            }
            return static_cast<std::size_t>(ref.offset()) < active_.size();
        }

        void prepare_gc() noexcept
        {
            survivor_.clear();
        }

        void swap_survivor() noexcept
        {
            active_.swap(survivor_);
            survivor_.clear();
        }

        void release_inactive() noexcept
        {
            survivor_.clear();
        }

    private:
        std::pmr::memory_resource* resource_ {std::pmr::get_default_resource()};
        std::pmr::vector<std::uint8_t> active_ {resource_};
        std::pmr::vector<std::uint8_t> survivor_ {resource_};
    };
}

