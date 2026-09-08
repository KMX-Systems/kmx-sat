/// @file library/src/kmx/sat/proof/tracer/view.cpp
/// @brief Out-of-line definitions declared by kmx/sat/proof/tracer/view.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/proof/tracer/view.hpp>

namespace kmx::sat::proof::tracer
{
    void view::add_original(const cdcl::clause::ref_t ref) noexcept
    {
        if (!variant_)
            return;
        std::visit([&](auto& tracer) noexcept { tracer.add_original(ref); }, *variant_);
    }

    void view::add_derived(const cdcl::clause::ref_t ref) noexcept
    {
        if (!variant_)
            return;
        std::visit([&](auto& tracer) noexcept { tracer.add_derived(ref); }, *variant_);
    }

    void view::delete_clause(const cdcl::clause::ref_t ref) noexcept
    {
        if (!variant_)
            return;
        std::visit([&](auto& tracer) noexcept { tracer.delete_clause(ref); }, *variant_);
    }

    void view::shrink_clause(const cdcl::clause::ref_t ref) noexcept
    {
        if (!variant_)
            return;
        std::visit([&](auto& tracer) noexcept { tracer.shrink_clause(ref); }, *variant_);
    }

    void view::on_event(const proof::proof_event& event) noexcept
    {
        if (!variant_)
            return;
        std::visit([&](auto& tracer) noexcept { tracer.on_event(event); }, *variant_);
    }

    void view::finalize() noexcept
    {
        if (!variant_)
            return;
        std::visit([](auto& tracer) noexcept { tracer.finalize(); }, *variant_);
    }

    static_assert(std::same_as<std::variant_alternative_t<static_cast<std::size_t>(format_id::drat), variant_t>, drat>);
    static_assert(std::same_as<std::variant_alternative_t<static_cast<std::size_t>(format_id::lrat), variant_t>, lrat>);
    static_assert(std::same_as<std::variant_alternative_t<static_cast<std::size_t>(format_id::frat), variant_t>, frat>);
    static_assert(std::same_as<std::variant_alternative_t<static_cast<std::size_t>(format_id::idrup), variant_t>, idrup>);
    static_assert(std::same_as<std::variant_alternative_t<static_cast<std::size_t>(format_id::lidrup), variant_t>, lidrup>);
    static_assert(std::same_as<std::variant_alternative_t<static_cast<std::size_t>(format_id::veripb), variant_t>, veripb>);

    std::optional<format_id> view::format() const noexcept
    {
        if (!variant_)
            return {};

        // The static assertions above pin `format_id`'s enumerator order to `variant_t`'s alternative order, so the
        // active alternative's index is the format itself.
        return static_cast<format_id>(variant_->index());
    }
}
