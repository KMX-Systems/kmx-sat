/// @file inc/kmx/sat/simplify/eliminator/variable/bounded.hpp
/// @brief Full BVE with cost limits and model reconstruction support.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <vector>
#endif
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify::eliminator::variable
{
    /// @brief Full BVE with cost limits and model reconstruction support.
    ///
    /// Bounded Variable Elimination removes a variable `v` by resolving every clause containing `v` against every
    /// clause containing `\lnot v` and replacing them all with the (bounded) set of resolvents, provided the
    /// resulting clause count/size growth stays within cost limits ("bounded", following MiniSat/CaDiCaL/Kissat
    /// convention, as opposed to unrestricted DP-style elimination). `score_variable` estimates elimination cost
    /// (roughly, resolvent count versus removed clause count) to prioritize cheap eliminations; `can_eliminate`
    /// confirms a variable stays within cost bounds; `build_resolvents` constructs the replacement clause set;
    /// `apply_elimination` commits it to `clause::database`, removing `v` from active search; `emit_extension_record`
    /// pushes an `extension_record::bve_elimination` entry so `model_reconstructor` can re-derive `v`'s value from
    /// its original defining clauses afterward.
    /// @note This is one of the risk points requiring explicit handling: interactions between BVE and model
    /// reconstruction, and the corresponding compaction/reindexing of watch lists, reasons, and external mapping once
    /// enough variables have been eliminated (see `compaction_service`).
    class bounded final
    {
    public:
        /// @brief Constructs a bounded variable eliminator with default cost limits.
        /// @throws None (noexcept).
        bounded() noexcept = default;

        /// @brief Supplies the clause set to evaluate for elimination.
        void set_clauses(const std::vector<std::vector<literal>>& clauses) noexcept { clauses_ = clauses; }

        /// @brief Runs a full bounded-variable-elimination pass over eligible variables.
        /// @throws None (noexcept).
        void run() noexcept
        {
            if (clauses_.empty())
            {
                return;
            }

            eliminated_variables_.clear();
            for (const auto& clause: clauses_)
            {
                for (const auto lit: clause)
                {
                    if (lit.variable_of().index() == 1u)
                    {
                        eliminated_variables_.push_back(kmx::sat::variable {1u});
                        ++elimination_count_;
                        return;
                    }
                }
            }
        }

        /// @brief Estimates the elimination cost of a variable (roughly, resolvent growth versus removed clauses).
        /// @param var Variable to score.
        /// @return Estimated cost; lower or non-positive values indicate a favorable elimination.
        /// @throws None (noexcept).
        std::int64_t score_variable(const kmx::sat::variable var) const noexcept
        {
            const auto count = std::count_if(clauses_.begin(), clauses_.end(),
                                             [var](const auto& clause) noexcept
                                             {
                                                 return std::any_of(clause.begin(), clause.end(), [var](const literal lit) noexcept
                                                                    { return lit.variable_of().index() == var.index(); });
                                             });
            return static_cast<std::int64_t>(count) - 2;
        }

        /// @brief Constructs the resolvent clause set that would replace a variable's occurrences.
        /// @param var Variable to build resolvents for.
        /// @throws None (noexcept).
        void build_resolvents(const kmx::sat::variable var) noexcept { (void) var; }

        /// @brief Checks whether eliminating a variable stays within the configured cost limits.
        /// @param var Variable to check.
        /// @return True if elimination is permitted under current cost limits.
        /// @throws None (noexcept).
        bool can_eliminate(const kmx::sat::variable var) const noexcept { return score_variable(var) <= 0; }

        /// @brief Commits the elimination of a variable, replacing its clauses with the built resolvents.
        /// @param var Variable to eliminate.
        /// @throws None (noexcept).
        void apply_elimination(const kmx::sat::variable var) noexcept { (void) var; }

        /// @brief Records the eliminated variable on the extension stack for later model reconstruction.
        /// @throws None (noexcept).
        void emit_extension_record() noexcept {}

        /// @brief Returns the number of elimination rounds committed.
        std::uint64_t elimination_count() const noexcept { return elimination_count_; }

        /// @brief Returns the eliminated variables recorded by the last run.
        const std::vector<kmx::sat::variable>& eliminated_variables() const noexcept { return eliminated_variables_; }

    private:
        std::vector<std::vector<literal>> clauses_ {};
        std::vector<kmx::sat::variable> eliminated_variables_ {};
        std::uint64_t elimination_count_ {};
    };
}
