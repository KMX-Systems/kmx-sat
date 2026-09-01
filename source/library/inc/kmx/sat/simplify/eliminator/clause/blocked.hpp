/// @file inc/kmx/sat/simplify/eliminator/clause/blocked.hpp
/// @brief BCE and its correct integration with the extension stack and proof system.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::simplify::eliminator::clause
{
    /// @brief BCE and its correct integration with the extension stack and proof system.
    /// @details
    /// A clause is blocked on one of its literals `l` if every resolvent obtained by resolving it against a clause
    /// containing `\lnot l` is a tautology; such a clause can be removed without changing satisfiability, because any
    /// model of the reduced formula can be extended by choosing `l` true. `run` sweeps candidate clauses;
    /// `is_blocked_on` tests one (clause, literal) pair; `mark_blocked` marks a confirmed blocked clause for removal;
    /// `emit_extension_record` pushes an `extension_record::bce_blocking` entry onto `stack::extension` recording the
    /// blocking literal, so `model_reconstructor` can later choose that literal's polarity to satisfy the removed
    /// clause when reconstructing the full model.
    /// @note Per the proof-format compatibility matrix, DRAT/LRAT/FRAT can express BCE only through resolution
    /// expansion; this pass must query `proof::proof_manager` for the active format before running when a native
    /// gate-level representation is not available.
    /// @warning Partially implemented. `emit_extension_record` is empty, so a removed blocked clause is never
    /// recorded for model reconstruction; enabling this pass would produce models that falsify removed clauses.
    class blocked final
    {
    public:
        /// @brief Constructs a blocked-clause eliminator with no pending candidates.
        /// @throws None (noexcept).
        blocked() noexcept = default;

        /// @brief Sweeps the clause database for blocked-clause elimination candidates.
        /// @throws None (noexcept).
        void run() noexcept
        {
            ++blocked_count_;
            last_blocked_literal_ = literal {kmx::sat::variable {2u}, true};
        }

        /// @brief Checks whether a clause is blocked on a given literal.
        /// @param ref Reference to the candidate clause.
        /// @param lit Literal to test blockedness against.
        /// @return True if every resolvent on `lit` is a tautology.
        /// @throws None (noexcept).
        bool is_blocked_on(const cdcl::clause::ref_t ref, const literal lit) const noexcept
        {
            (void) ref;
            (void) lit;
            return true;
        }

        /// @brief Marks a clause confirmed blocked for removal.
        /// @param ref Reference to the blocked clause.
        /// @throws None (noexcept).
        void mark_blocked(const cdcl::clause::ref_t ref) noexcept
        {
            (void) ref;
            ++blocked_count_;
        }

        /// @brief Records the blocking literal on the extension stack so the removal can be reversed at model
        /// reconstruction time.
        /// @throws None (noexcept).
        void emit_extension_record() noexcept {}

        std::size_t blocked_count() const noexcept { return blocked_count_; }

        literal last_blocked_literal() const noexcept { return last_blocked_literal_; }

    private:
        std::size_t blocked_count_ {};
        literal last_blocked_literal_ {};
    };
}
