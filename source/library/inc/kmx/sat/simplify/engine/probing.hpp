/// @file inc/kmx/sat/simplify/engine/probing.hpp
/// @brief Failed literal probing and its useful implications.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
#endif
#include <kmx/sat/literal.hpp>

namespace kmx::sat::simplify::engine
{
    /// @brief Failed literal probing and its useful implications.
    ///
    /// Failed-literal probing tentatively assumes one literal, propagates it, and observes the consequences without
    /// committing to a real search decision: if propagating a literal leads to a conflict, its negation is a unit
    /// fact (the literal is "failed"); if propagating it forces another literal true regardless of which polarity
    /// was tried, a hyper-binary implication or backbone candidate has been found. `probe_literal` runs one such
    /// trial propagation; `run_failed_literal_probing` sweeps candidate literals under a budget;
    /// `learn_hyper_binary` records a shortcut binary implication discovered during probing (reducing future
    /// propagation chain length); `record_backbone_candidate` forwards a literal implied identically under both
    /// polarities to `backbone_extractor` for confirmation.
    /// @note Every unit fact or hyper-binary clause this pass derives must be reported to `proof::proof_manager` like
    /// any other derived clause.
    class probing final
    {
    public:
        /// @brief Constructs a probing engine with no in-progress probe.
        /// @throws None (noexcept).
        probing() noexcept = default;

        /// @brief Tentatively assumes and propagates one literal to observe its consequences.
        /// @param lit Literal to probe.
        /// @throws None (noexcept).
        void probe_literal(const literal lit) noexcept
        {
        }

        /// @brief Sweeps candidate literals for failed-literal probing under the current pass budget.
        /// @throws None (noexcept).
        void run_failed_literal_probing() noexcept
        {
        }

        /// @brief Records a hyper-binary implication shortcut discovered while probing.
        /// @throws None (noexcept).
        void learn_hyper_binary() noexcept
        {
        }

        /// @brief Forwards a literal implied identically under both polarities as a backbone candidate.
        /// @param lit Candidate backbone literal.
        /// @throws None (noexcept).
        void record_backbone_candidate(const literal lit) noexcept
        {
        }
    };
}
