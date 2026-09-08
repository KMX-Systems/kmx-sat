/// @file library/src/kmx/sat/cdcl/store/clause_cold.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/store/clause_cold.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/store/clause_cold.hpp>

namespace kmx::sat::cdcl::store
{
    void clause_cold::set_enabled(const bool enabled) noexcept
    {
        enabled_ = enabled;
        if (!enabled_)
            reset();
    }

    void clause_cold::reset() noexcept
    {
        cold_refs_.clear();
        payloads_.clear();
        cold_footprint_ = 0u;
        promotion_count_ = 0u;
        access_count_ = 0u;
    }

    void clause_cold::demote_to_cold(const clause::ref_t ref) noexcept
    {
        if (!enabled_ || !ref.valid())
            return;
        store_payload(ref, {});
    }

    void clause_cold::demote_to_cold(const clause::ref_t ref, const std::span<const literal> literals) noexcept
    {
        if (!enabled_ || !ref.valid())
            return;
        store_payload(ref, literals);
    }

    clause::ref_t clause_cold::promote_from_cold(const clause::ref_t ref) noexcept
    {
        if (enabled_ && (cold_refs_.erase(ref.offset()) != 0u))
        {
            erase_payload(ref);
            ++promotion_count_;
        }
        return ref;
    }

    std::vector<literal> clause_cold::decode_literals(const clause::ref_t ref) const noexcept
    {
        std::vector<literal> result {};
        const auto it = payloads_.find(ref.offset());
        if (!enabled_ || (it == payloads_.end()))
            return result;

        std::int64_t previous {};
        for (std::size_t index {}; index < it->second.size();)
        {
            std::uint64_t encoded {};
            std::uint32_t shift {};
            for (;;)
            {
                if (index >= it->second.size())
                    return {};
                const auto byte = it->second[index++];
                if ((shift == 28u) && ((byte & 0x7fu) > 0x1fu))
                    return {};
                encoded |= static_cast<std::uint64_t>(byte & 0x7fu) << shift;
                if ((byte & 0x80u) == 0u)
                    break;
                shift += 7u;
                if (shift >= 32u)
                    return {};
            }
            const auto delta = static_cast<std::int64_t>(encoded >> 1u) ^ -static_cast<std::int64_t>(encoded & 1u);
            previous += delta;
            if ((previous < 0L) || (previous > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())))
                return {};
            result.emplace_back(literal {static_cast<std::uint32_t>(previous)});
        }
        return result;
    }

    void clause_cold::rewrite_ref_after_gc(const clause::ref_t old_ref, const clause::ref_t new_ref) noexcept
    {
        if (!enabled_ || !old_ref.valid() || !new_ref.valid() || (old_ref == new_ref))
            return;
        if (cold_refs_.erase(old_ref.offset()) != 0u)
            cold_refs_.insert(new_ref.offset());
        if (const auto it = payloads_.find(old_ref.offset()); it != payloads_.end())
        {
            auto payload = std::move(it->second);
            payloads_.erase(it);
            payloads_.emplace(new_ref.offset(), std::move(payload));
        }
    }

    void clause_cold::rewrite_literals_after_compaction(const clause::ref_t ref, const std::span<const literal> literals) noexcept
    {
        if (!enabled_ || !is_cold(ref))
            return;
        erase_payload(ref);
        cold_refs_.erase(ref.offset());
        store_payload(ref, literals);
    }

    void clause_cold::store_payload(const clause::ref_t ref, const std::span<const literal> literals) noexcept
    {
        if (!cold_refs_.insert(ref.offset()).second)
            return;

        std::vector<std::uint8_t> encoded_payload {};
        std::int64_t previous {};
        for (const auto lit: literals)
        {
            const auto current = static_cast<std::int64_t>(lit.raw());
            const auto delta = current - previous;
            previous = current;
            auto encoded = (static_cast<std::uint64_t>(delta) << 1u) ^ static_cast<std::uint64_t>(delta >> 63u);
            do
            {
                auto byte = static_cast<std::uint8_t>(encoded & 0x7fu);
                encoded >>= 7u;
                if (encoded != 0u)
                    byte |= 0x80u;
                encoded_payload.push_back(byte);
            } while (encoded != 0u);
        }
        cold_footprint_ += encoded_payload.empty() ? estimated_entry_bytes_ : encoded_payload.size();
        payloads_.emplace(ref.offset(), std::move(encoded_payload));
    }

    void clause_cold::erase_payload(const clause::ref_t ref) noexcept
    {
        const auto it = payloads_.find(ref.offset());
        const auto bytes = ((it == payloads_.end()) || it->second.empty()) ? estimated_entry_bytes_ : it->second.size();
        cold_footprint_ = (cold_footprint_ >= bytes) ? cold_footprint_ - bytes : 0u;
        if (it != payloads_.end())
            payloads_.erase(it);
    }
}
