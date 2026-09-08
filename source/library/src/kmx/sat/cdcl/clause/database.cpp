/// @file library/src/kmx/sat/cdcl/clause/database.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/clause/database.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/clause/database.hpp>

namespace kmx::sat::cdcl::clause
{
    ref_t database::add_clause(const std::span<const literal> literals, const bool redundant) noexcept
    {
        const auto ref = redundant ? storage_.create_learned_clause(literals) : storage_.create_original_clause(literals);
        if (!ref.valid())
            return ref;

        if (redundant)
            redundant_refs_.push_back(ref);
        else
            irredundant_refs_.push_back(ref);
        auto& header = storage_.header_at(ref);
        header.glue = static_cast<std::uint32_t>(literals.size());
        header.tier = static_cast<std::uint8_t>(redundant ? default_tier : tier_t {});
        header.flags |= bank::tracked_flag;
        header.used = 0u;
        header.activity = 0.0f;
        return ref;
    }

    void database::set_glue(const ref_t ref, const std::uint32_t glue) noexcept
    {
        if (!storage_.is_alive(ref))
            return;
        auto& header = storage_.header_at(ref);
        header.glue = glue;
        header.flags |= bank::tracked_flag;
    }

    void database::increment_used_count(const ref_t ref) noexcept
    {
        if (!storage_.is_alive(ref))
            return;
        auto& used = storage_.header_at(ref).used;
        if (used != std::numeric_limits<std::uint8_t>::max())
            ++used;
    }

    void database::note_clause_used(const ref_t ref) noexcept
    {
        if (!storage_.is_alive(ref))
            return;
        auto& used = storage_.header_at(ref).used;
        used = static_cast<std::uint8_t>(std::min<unsigned>(255u, static_cast<unsigned>(used) + 2u));
    }

    void database::increment_activity(const ref_t ref, const double amount) noexcept
    {
        if (!storage_.is_alive(ref))
            return;
        storage_.header_at(ref).activity += static_cast<float>(amount);
    }

    void database::decay_quality(const double factor) noexcept
    {
        const auto bounded_factor = (factor < 0.0) ? 0.0 : ((factor > 1.0) ? 1.0 : factor);
        const auto age = [&](const ref_t ref) noexcept
        {
            if (!storage_.is_alive(ref))
                return;
            auto& header = storage_.header_at(ref);
            header.activity = static_cast<float>(static_cast<double>(header.activity) * bounded_factor);
            header.used = static_cast<std::uint8_t>(static_cast<double>(header.used) * bounded_factor);
        };
        for (const auto ref: irredundant_refs_)
            age(ref);
        for (const auto ref: redundant_refs_)
            age(ref);
    }

    void database::mark_garbage(const ref_t ref) noexcept
    {
        if (!storage_.is_alive(ref))
            return;
        auto& header = storage_.header_at(ref);
        if ((header.flags & bank::garbage_flag) != 0u)
            return;
        header.flags |= bank::garbage_flag;
        ++garbage_count_;
    }

    void database::clear_reason_clauses() noexcept
    {
        for (const auto ref: irredundant_refs_)
            unmark_reason_clause(ref);
        for (const auto ref: redundant_refs_)
            unmark_reason_clause(ref);
    }

    bool database::make_irredundant(const ref_t ref) noexcept
    {
        if (!storage_.is_alive(ref))
            return false;
        const auto it = std::find(redundant_refs_.begin(), redundant_refs_.end(), ref);
        if (it == redundant_refs_.end())
            return false;
        redundant_refs_.erase(it);
        irredundant_refs_.push_back(ref);
        auto& header = storage_.header_at(ref);
        header.flags &= static_cast<std::uint8_t>(~bank::redundant_flag);
        header.tier = 0u;
        return true;
    }

    void database::rewrite_ref_after_gc(const ref_t old_ref, const ref_t new_ref) noexcept
    {
        if (!old_ref.valid() || !new_ref.valid() || (old_ref == new_ref)) [[unlikely]]
            return;
        rewrite_ref_in_vector(irredundant_refs_, old_ref, new_ref);
        rewrite_ref_in_vector(redundant_refs_, old_ref, new_ref);
    }

    void database::set_tier(const ref_t ref, const tier_t tier) noexcept
    {
        if (!storage_.is_alive(ref))
            return;
        auto& header = storage_.header_at(ref);
        header.flags |= bank::tracked_flag;
        header.tier = static_cast<std::uint8_t>((tier > lowest_tier) ? lowest_tier : tier);
    }

    void database::promote_clause(const ref_t ref) noexcept
    {
        if (!storage_.is_alive(ref))
            return;
        auto& header = storage_.header_at(ref);
        header.flags |= bank::tracked_flag;
        if (header.tier > 0u)
            --header.tier;
    }

    void database::demote_clause(const ref_t ref) noexcept
    {
        if (!storage_.is_alive(ref))
            return;
        auto& header = storage_.header_at(ref);
        header.flags |= bank::tracked_flag;
        if (header.tier < lowest_tier)
            ++header.tier;
    }

    void database::rewrite_ref_in_vector(std::vector<ref_t>& refs, const ref_t old_ref, const ref_t new_ref) noexcept
    {
        for (auto& ref: refs)
            if (ref == old_ref)
                ref = new_ref;
    }
}
