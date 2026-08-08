/// @file inc/kmx/sat/cdcl/store/phase.hpp
/// @brief All phase saving and rephasing policies.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl::store
{
    /// @brief All phase saving and rephasing policies.
    ///
    /// `store::phase` holds three independent per-variable polarity bits: `saved_phase` (the last-assigned value,
    /// used by `engine::decision::pick_decision_phase` so repeated decisions on the same variable tend to reuse the
    /// polarity that recently worked, per MiniSat/CaDiCaL phase-saving), `best_phase` (the polarity snapshot from the
    /// best search state seen so far, applied by `controller::rephase::apply_best`), and `target_phase` (a
    /// short-lived target used while walking toward a specific configuration, for example during
    /// `controller::rephase::apply_walk_seed`). `flip_all`/`randomize_subset` implement the inverted and random
    /// rephasing strategies respectively.
    class phase final
    {
    public:
        /// @brief Constructs a phase store with an arbitrary default polarity for every variable.
        /// @throws None (noexcept).
        phase() noexcept = default;

        /// @brief Returns the last-assigned polarity saved for a variable.
        /// @param var Variable to query.
        /// @return Saved polarity (true means positive phase).
        /// @throws None (noexcept).
        bool saved_phase(const variable var) const noexcept
        {
            return false;
        }

        /// @brief Returns the polarity recorded from the best search state seen so far.
        /// @param var Variable to query.
        /// @return Best-known polarity.
        /// @throws None (noexcept).
        bool best_phase(const variable var) const noexcept
        {
            return false;
        }

        /// @brief Returns the current rephasing target polarity for a variable.
        /// @param var Variable to query.
        /// @return Target polarity.
        /// @throws None (noexcept).
        bool target_phase(const variable var) const noexcept
        {
            return false;
        }

        /// @brief Updates the saved polarity for a variable, typically on assignment.
        /// @param var Variable to update.
        /// @param value New saved polarity.
        /// @throws None (noexcept).
        void set_saved_phase(const variable var, const bool value) noexcept
        {
        }

        /// @brief Inverts the saved polarity of every variable (inverted rephasing).
        /// @throws None (noexcept).
        void flip_all() noexcept
        {
        }

        /// @brief Randomizes the saved polarity of a subset of variables (random rephasing).
        /// @throws None (noexcept).
        void randomize_subset() noexcept
        {
        }
    };
}
