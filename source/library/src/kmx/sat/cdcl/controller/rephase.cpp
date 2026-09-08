/// @file library/src/kmx/sat/cdcl/controller/rephase.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/controller/rephase.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/controller/rephase.hpp>

namespace kmx::sat::cdcl::controller
{
    void rephase::apply_best() noexcept
    {
        if (phase_store_ == nullptr)
            return;
        if (!has_best_snapshot_)
        {
            best_snapshot_ = capture_snapshot();
            has_best_snapshot_ = true;
        }
        restore_snapshot(best_snapshot_);
        rephase_pending_ = false;
    }

    void rephase::apply_inverted() noexcept
    {
        if (phase_store_ == nullptr)
            return;
        phase_store_->flip_all();
        current_snapshot_ = capture_snapshot();
        rephase_pending_ = false;
    }

    void rephase::apply_random() noexcept
    {
        if (phase_store_ == nullptr)
            return;
        phase_store_->randomize_subset();
        current_snapshot_ = capture_snapshot();
        rephase_pending_ = false;
    }

    void rephase::apply_walk_seed() noexcept
    {
        if (phase_store_ == nullptr)
            return;

        const auto slot_count = phase_store_->saved_phase_count();
        for (std::size_t index = 1u; index < slot_count; ++index)
        {
            const auto var_index = static_cast<std::uint32_t>(index);
            const auto seeded_value = ((index + observed_opportunity_count()) & 1u) == 0u;
            phase_store_->set_saved_phase(variable {var_index}, seeded_value);
        }

        current_snapshot_ = capture_snapshot();
        rephase_pending_ = false;
    }

    void rephase::record_conflict() noexcept
    {
        ++conflict_count_;
        if ((conflict_count_ >= 1u) && (decision_count_ >= 1u))
            rephase_pending_ = true;
    }

    void rephase::record_decision() noexcept
    {
        ++decision_count_;
        if ((conflict_count_ >= 1u) && (decision_count_ >= 1u))
            rephase_pending_ = true;
    }

    std::uint8_t rephase::capture_snapshot() const noexcept
    {
        if (phase_store_ == nullptr)
            return current_snapshot_;

        std::uint8_t snapshot {};
        if (phase_store_->saved_phase(variable {1u}))
            snapshot |= 0x01u;
        if (phase_store_->saved_phase(variable {2u}))
            snapshot |= 0x02u;
        return snapshot;
    }

    void rephase::restore_snapshot(const std::uint8_t snapshot) noexcept
    {
        current_snapshot_ = snapshot;
        if (phase_store_ != nullptr)
        {
            phase_store_->set_saved_phase(variable {1u}, (snapshot & 0x01u) != 0u);
            phase_store_->set_saved_phase(variable {2u}, (snapshot & 0x02u) != 0u);
        }
    }
}
