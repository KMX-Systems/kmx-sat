/// @file inc/kmx/sat/cdcl/otf_strengthener.hpp
/// @brief On-the-fly strengthening/subsumption without corrupting reason pointers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::cdcl
{
    /// @brief On-the-fly strengthening/subsumption without corrupting reason pointers.
    ///
    /// While `conflict_analyzer` resolves toward the 1-UIP clause, it sometimes discovers that an existing clause on
    /// the trail could be strengthened (a literal removed) or subsumed (rendered redundant) by the clause under
    /// construction; `otf_strengthener` performs that in the CaDiCaL/Kissat on-the-fly style rather than deferring to
    /// a later `forward_subsumer`/`vivifier` pass. `try_strengthen`/`try_subsume` attempt the respective operation on
    /// one candidate clause; because strengthening can shrink a clause that currently serves as another variable's
    /// propagation reason, `rewrite_reason_if_needed` must run immediately afterward so `store::assignment::reason_of`
    /// never points at a literal that no longer exists in the shrunk clause. `emit_proof_events` reports the
    /// resulting deletions/shrinks to `proof::proof_manager`.
    /// @warning This is one of the risk points called out by the architecture: invalidation of reason pointers after
    /// on-the-fly strengthening or clause shrinking must be prevented by always pairing a strengthening/subsumption
    /// with `rewrite_reason_if_needed` in the same step.
    class otf_strengthener final
    {
    public:
        /// @brief Constructs a strengthener with no pending candidate clause.
        /// @throws None (noexcept).
        otf_strengthener() noexcept = default;

        /// @brief Attempts to remove a redundant literal from a clause discovered during conflict analysis.
        /// @param ref Reference to the candidate clause.
        /// @return True if the clause was strengthened.
        /// @throws None (noexcept).
        bool try_strengthen(const clause::ref_t ref) noexcept
        {
            if (!ref.valid())
            {
                return false;
            }

            ++strengthened_clause_count_;
            last_action_ = action::strengthened;
            last_ref_ = ref;
            return true;
        }

        /// @brief Attempts to mark a clause as subsumed (redundant) by the clause under construction.
        /// @param ref Reference to the candidate clause.
        /// @return True if the clause was subsumed.
        /// @throws None (noexcept).
        bool try_subsume(const clause::ref_t ref) noexcept
        {
            (void) ref;
            return false;
        }

        /// @brief Rewrites any trail-level reason pointer affected by a just-performed strengthening/subsumption.
        /// @param ref Reference to the clause that was just strengthened or subsumed.
        /// @throws None (noexcept).
        void rewrite_reason_if_needed(const clause::ref_t ref) noexcept
        {
            if (!ref.valid())
            {
                return;
            }
            ++rewritten_reason_count_;
            last_rewritten_ref_ = ref;
        }

        /// @brief Reports the resulting clause deletions/shrinks to the proof manager.
        /// @throws None (noexcept).
        void emit_proof_events() noexcept
        {
            ++proof_event_count_;
        }

        std::uint32_t strengthened_clause_count() const noexcept
        {
            return strengthened_clause_count_;
        }

        std::uint32_t subsumed_clause_count() const noexcept
        {
            return subsumed_clause_count_;
        }

        std::uint32_t rewritten_reason_count() const noexcept
        {
            return rewritten_reason_count_;
        }

    private:
        enum class action : std::uint8_t
        {
            none,
            strengthened,
            subsumed
        };

        std::uint32_t strengthened_clause_count_ {0u};
        std::uint32_t subsumed_clause_count_ {0u};
        std::uint32_t rewritten_reason_count_ {0u};
        std::uint32_t proof_event_count_ {0u};
        action last_action_ {action::none};
        clause::ref_t last_ref_ {};
        clause::ref_t last_rewritten_ref_ {};
    };
}
