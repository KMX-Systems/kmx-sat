/// @file inc/kmx/sat/telemetry/ema_tracker.hpp
/// @brief Moving averages for restart, reduce, and focused/stable modes.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

namespace kmx::sat::telemetry
{
    /// @brief Moving averages for restart, reduce, and focused/stable modes.
    /// @details
    /// `ema_tracker` maintains the fast/slow exponential-moving-average pairs that drive Glucose/Kissat-style
    /// adaptive search-mode switching: `update_glue_fast`/`update_glue_slow` track short- and long-window averages of
    /// learned-clause glue, `update_decision_rate` and `update_trail` track how quickly decisions accumulate and how
    /// full the trail runs; `fast_vs_slow_margin` reports the gap between the fast and slow glue averages, which
    /// `controller::restart` uses as one of its restart triggers (a widening gap signals the search has drifted into
    /// unproductive territory and should restart) and which `search_coordinator`/`decision_engine` can use to switch
    /// between "focused" and "stable" search modes.
    class ema_tracker final
    {
    public:
        /// @brief Constructs an EMA tracker with all averages at their initial baseline.
        /// @throws None (noexcept).
        ema_tracker() noexcept = default;

        /// @brief Updates the fast (short-window) glue moving average with a new sample.
        /// @param value Newly observed glue value.
        /// @throws None (noexcept).
        void update_glue_fast(const double value) noexcept { glue_fast_ = (glue_fast_ * 0.75) + (value * 0.25); }

        /// @brief Updates the slow (long-window) glue moving average with a new sample.
        /// @param value Newly observed glue value.
        /// @throws None (noexcept).
        void update_glue_slow(const double value) noexcept { glue_slow_ = (glue_slow_ * 0.9) + (value * 0.1); }

        /// @brief Updates the decision-rate moving average with a new sample.
        /// @param value Newly observed decision-rate sample.
        /// @throws None (noexcept).
        void update_decision_rate(const double value) noexcept { decision_rate_ = (decision_rate_ * 0.8) + (value * 0.2); }

        /// @brief Updates the trail-fullness moving average with a new sample.
        /// @param value Newly observed trail-fullness sample.
        /// @throws None (noexcept).
        void update_trail(const double value) noexcept { trail_ = (trail_ * 0.8) + (value * 0.2); }

        /// @brief Returns the current gap between the fast and slow glue moving averages.
        /// @return Fast-versus-slow margin, used as a restart trigger signal.
        /// @throws None (noexcept).
        double fast_vs_slow_margin() const noexcept { return glue_fast_ - glue_slow_; }

    private:
        double glue_fast_ {};
        double glue_slow_ {};
        double decision_rate_ {};
        double trail_ {};
    };
}
