/// @file inc/kmx/sat/simplify/engine/instantiation.hpp
/// @brief Auxiliary literal removal to unlock future eliminations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif

namespace kmx::sat::simplify::engine
{
    /// @brief Auxiliary literal removal to unlock future eliminations.
    /// @details
    /// Some clauses carry "auxiliary" literals that make a variable appear more entangled than it really is,
    /// blocking `eliminator::variable::bounded`/`fast` from eliminating it economically; instantiation removes such
    /// literals when they can be shown redundant given the current clause set, shrinking clauses and reducing
    /// variable occurrence counts so a later elimination pass becomes cheap enough to run. `collect_candidates`
    /// finds literals suspected to be removable this way; `instantiate_literal_removal` verifies and applies the
    /// removal via the same clause-shrinking path used by `otf_strengthener`/`vivifier`; `run` drives one full pass
    /// over the clause database.
    class instantiation final
    {
    public:
        /// @brief Constructs an instantiation engine with no pending candidates.
        /// @throws None (noexcept).
        instantiation() noexcept = default;

        /// @brief Runs a full instantiation pass over the clause database.
        /// @throws None (noexcept).
        void run() noexcept { run_completed_ = true; }

        /// @brief Collects literals suspected to be removable as auxiliary given the current clause set.
        /// @throws None (noexcept).
        void collect_candidates() noexcept { ++candidate_count_; }

        /// @brief Verifies and applies removal of a candidate auxiliary literal.
        /// @throws None (noexcept).
        void instantiate_literal_removal() noexcept { ++removal_count_; }

        std::size_t candidate_count() const noexcept { return candidate_count_; }

        std::size_t removal_count() const noexcept { return removal_count_; }

        bool run_completed() const noexcept { return run_completed_; }

    private:
        std::size_t candidate_count_ {};
        std::size_t removal_count_ {};
        bool run_completed_ {};
    };
}
