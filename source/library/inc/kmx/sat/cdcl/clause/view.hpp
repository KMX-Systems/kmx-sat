/// @file inc/kmx/sat/cdcl/clause/view.hpp
/// @brief Safe clause view with strictly controlled lifetime.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/header.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::clause
{
    /// @brief Safe clause view with strictly controlled lifetime.
    /// @details
    /// `view` provides convenient read-only queries over one clause's literals and header snapshot.
    /// It supports common predicates (`contains`, `is_binary`, `is_unit`) and keeps value-like semantics for
    /// simple transport between CDCL components.
    class view final
    {
    public:
        view() noexcept = default;

        view(std::span<const literal> literals, header header_data) noexcept : literals_ {literals.begin(), literals.end()}, header_ {header_data}
        {
        }

        std::span<const literal> literals() const noexcept
        {
            return literals_;
        }

        [[nodiscard]] header header_data() const noexcept
        {
            return header_;
        }

        bool contains(const literal lit) const noexcept
        {
            for (const auto& entry : literals_)
            {
                if (entry == lit)
                {
                    return true;
                }
            }
            return false;
        }

        bool is_binary() const noexcept
        {
            return literals_.size() == 2;
        }

        bool is_unit() const noexcept
        {
            return literals_.size() == 1;
        }

        bool is_redundant() const noexcept
        {
            return header_.is_redundant();
        }

        bool is_active_reason() const noexcept
        {
            return header_.is_active_reason();
        }

        bool is_satisfied_by_shrink() const noexcept
        {
            return header_.is_satisfied_by_shrink();
        }

        std::uint32_t size() const noexcept
        {
            return static_cast<std::uint32_t>(literals_.size());
        }

    private:
        std::vector<literal> literals_ {};
        header header_ {};
    };
}
