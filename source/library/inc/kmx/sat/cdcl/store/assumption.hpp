/// @file inc/kmx/sat/cdcl/store/assumption.hpp
/// @brief Stores assumptions separately from the decision trail.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::store
{
    /// @brief Stores assumptions separately from the decision trail.
    ///
    /// Assumption literals pushed by `external_frontend::push_assumption` are staged here rather than merged into the
    /// permanent clause database (`store::constraint`), so that clearing or replacing assumptions between episodes
    /// never touches original or learned clauses. `trail_level_base` reports the decision-level offset at which
    /// assumption-forced decisions begin on the CDCL `trail`, letting `backtrack_engine` distinguish assumption
    /// decisions from ordinary search decisions. `capture_failed_assumptions` exposes the subset relevant to
    /// `failed_core_extractor` after an assumptions-level conflict.
    class assumption final
    {
    public:
        /// @brief Constructs an empty assumption store.
        /// @throws None (noexcept).
        assumption() noexcept = default;

        /// @brief Appends one assumption literal to the staged list for the next episode.
        /// @param lit Assumption literal to stage.
        /// @throws None (noexcept).
        void push(const literal lit) noexcept
        {
        }

        /// @brief Clears all currently staged assumption literals.
        /// @throws None (noexcept).
        void clear() noexcept
        {
        }

        /// @brief Exposes the currently staged assumptions in push order.
        /// @return Read-only span over the staged assumption literals.
        /// @throws None (noexcept).
        std::span<const literal> iterate() const noexcept
        {
            return literals_;
        }

        /// @brief Returns the number of currently staged assumptions.
        /// @return Count of staged assumption literals.
        /// @throws None (noexcept).
        std::size_t size() const noexcept
        {
            return literals_.size();
        }

        /// @brief Returns the trail position at which assumption-forced decisions begin, letting the backtrack engine
        /// distinguish assumption decisions from ordinary search decisions.
        /// @return Base trail level reserved for assumptions.
        /// @throws None (noexcept).
        std::uint32_t trail_level_base() const noexcept
        {
            return {};
        }

        /// @brief Returns the subset of staged assumptions determined to be part of the failed core after an
        /// unsatisfiable episode.
        /// @return Read-only span over the failed assumption literals.
        /// @throws None (noexcept).
        std::span<const literal> capture_failed_assumptions() const noexcept
        {
            return literals_;
        }

    private:
        std::vector<literal> literals_ {};
    };
}
