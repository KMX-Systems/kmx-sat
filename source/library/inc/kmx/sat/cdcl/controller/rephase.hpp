/// @file inc/kmx/sat/cdcl/controller/rephase.hpp
/// @brief All rephasing strategies and lucky phase management.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif
#include <kmx/sat/cdcl/store/phase.hpp>

namespace kmx::sat::cdcl::controller
{
    /// @brief All rephasing strategies and lucky phase management.
    ///
    /// @details
    /// `controller::rephase` periodically overwrites `store::phase`'s saved polarities with a different source to
    /// help the search escape a locally unproductive polarity assignment, following the CaDiCaL/Kissat rephasing
    /// schedule (typically cycling through strategies rather than using one exclusively): `apply_best` restores the
    /// polarity snapshot from the best search state seen so far; `apply_inverted` flips every saved polarity
    /// (`store::phase::flip_all`); `apply_random` randomizes a subset (`store::phase::randomize_subset`); and
    /// `apply_walk_seed` seeds phases from a local-search-style ("lucky"/random-walk) probe rather than from CDCL
    /// search history. `should_rephase` decides, based on conflict count and schedule, when the next strategy in the
    /// cycle should be applied.
    class rephase final
    {
    public:
        /// @brief Constructs a rephase controller with an empty schedule and default phase store binding.
        /// @throws None (noexcept).
        rephase() noexcept = default;

        void attach_phase_store(store::phase& phases) noexcept { phase_store_ = &phases; }

        /// @brief Checks whether a rephasing step should be performed now.
        /// @return True if the rephasing schedule has triggered.
        /// @throws None (noexcept).
        bool should_rephase() const noexcept { return rephase_pending_; }

        /// @brief Returns whether a rephase action is pending for the next coordination step.
        /// @return True if a rephase trigger has fired.
        [[nodiscard]] bool has_pending_rephase() const noexcept { return rephase_pending_; }

        /// @brief Restores saved phases from the best search state seen so far.
        /// @throws None (noexcept).
        void apply_best() noexcept
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

        /// @brief Inverts every saved phase.
        /// @throws None (noexcept).
        void apply_inverted() noexcept
        {
            if (phase_store_ == nullptr)
                return;
            phase_store_->flip_all();
            current_snapshot_ = capture_snapshot();
            rephase_pending_ = false;
        }

        /// @brief Randomizes a subset of saved phases.
        /// @throws None (noexcept).
        void apply_random() noexcept
        {
            if (phase_store_ == nullptr)
                return;
            phase_store_->randomize_subset();
            current_snapshot_ = capture_snapshot();
            rephase_pending_ = false;
        }

        /// @brief Seeds phases from a local-search-style (lucky/random-walk) probe.
        /// @throws None (noexcept).
        void apply_walk_seed() noexcept
        {
            if (phase_store_ == nullptr)
                return;

            const auto slot_count = phase_store_->saved_phase_count();
            for (std::size_t index = 1; index < slot_count; ++index)
            {
                const auto var_index = static_cast<std::uint32_t>(index);
                const auto seeded_value = ((index + observed_opportunity_count()) & 1u) == 0u;
                phase_store_->set_saved_phase(variable {var_index}, seeded_value);
            }

            current_snapshot_ = capture_snapshot();
            rephase_pending_ = false;
        }

        void record_conflict() noexcept
        {
            ++conflict_count_;
            if (conflict_count_ >= 1u && decision_count_ >= 1u)
                rephase_pending_ = true;
        }

        void record_decision() noexcept
        {
            ++decision_count_;
            if (conflict_count_ >= 1u && decision_count_ >= 1u)
                rephase_pending_ = true;
        }

        /// @brief Returns how many rephase opportunities have been observed by this controller.
        /// @return Number of recorded conflicts and decisions that have contributed to the schedule.
        std::uint32_t observed_opportunity_count() const noexcept { return conflict_count_ + decision_count_; }

    private:
        std::uint32_t conflict_count_ {};
        std::uint32_t decision_count_ {};
        bool rephase_pending_ {};
        store::phase* phase_store_ {};
        std::uint8_t current_snapshot_ {};
        std::uint8_t best_snapshot_ {};
        bool has_best_snapshot_ {};

        std::uint8_t capture_snapshot() const noexcept
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

        void restore_snapshot(const std::uint8_t snapshot) noexcept
        {
            current_snapshot_ = snapshot;
            if (phase_store_ != nullptr)
            {
                phase_store_->set_saved_phase(variable {1u}, (snapshot & 0x01u) != 0u);
                phase_store_->set_saved_phase(variable {2u}, (snapshot & 0x02u) != 0u);
            }
        }
    };
}
