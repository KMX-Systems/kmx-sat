/// @file inc/kmx/sat/cdcl/bank/trail.hpp
/// @brief The assignment trail: a fixed-capacity sequence of literals.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cassert>
    #include <cstddef>
    #include <vector>
#endif
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::bank
{
    /// @brief The trail of assigned literals, sized once per episode for every variable.
    /// @details A `std::vector` served here before, and its growth path came with it into every inlined
    /// assignment: dead code, since the trail is reserved for all variables before the search starts, but code
    /// the compiler could not remove and, in one build, inlined into the propagation loop at a sixth more
    /// instructions per call through the register pressure it added. This container has no growth path: an
    /// append is a store and an increment, and the capacity is a precondition.
    class trail final
    {
    public:
        using value_type = literal;
        using iterator = literal*;
        using const_iterator = const literal*;

        /// @brief Makes room for `capacity` literals; the contents are kept.
        /// @throws None (noexcept).
        void reserve(const std::size_t capacity) noexcept
        {
            if (capacity > storage_.size())
                storage_.resize(capacity);
        }

        [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }
        [[nodiscard]] std::size_t size() const noexcept { return size_; }
        [[nodiscard]] bool empty() const noexcept { return size_ == 0u; }

        /// @brief Appends a literal; the caller guarantees the capacity.
        [[gnu::always_inline]] inline void push_back(const literal lit) noexcept
        {
            assert(size_ < storage_.size());
            storage_[size_++] = lit;
        }

        /// @brief Cuts the trail down to `count` literals; never grows it.
        void resize(const std::size_t count) noexcept
        {
            assert(count <= size_);
            size_ = count;
        }

        void clear() noexcept { size_ = 0u; }

        [[nodiscard]] literal& operator[](const std::size_t index) noexcept { return storage_[index]; }
        [[nodiscard]] const literal& operator[](const std::size_t index) const noexcept { return storage_[index]; }
        [[nodiscard]] literal back() const noexcept { return storage_[size_ - 1u]; }
        [[nodiscard]] literal* data() noexcept { return storage_.data(); }
        [[nodiscard]] const literal* data() const noexcept { return storage_.data(); }
        [[nodiscard]] iterator begin() noexcept { return storage_.data(); }
        [[nodiscard]] iterator end() noexcept { return storage_.data() + size_; }
        [[nodiscard]] const_iterator begin() const noexcept { return storage_.data(); }
        [[nodiscard]] const_iterator end() const noexcept { return storage_.data() + size_; }

    private:
        std::vector<literal> storage_ {};
        std::size_t size_ {};
    };
}
