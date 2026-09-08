/// @file inc/kmx/sat/simplify/eliminator/variable/bounded.hpp
/// @brief Full BVE with cost limits and model reconstruction support.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <functional>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/stack/extension.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify::eliminator::variable
{
    /// @brief Full BVE with cost limits and model reconstruction support.
    /// @details
    /// Bounded Variable Elimination removes a variable `v` by resolving every clause containing `v` against every
    /// clause containing `\lnot v` and replacing them all with the (bounded) set of resolvents, provided the
    /// resulting clause count/size growth stays within cost limits ("bounded", following MiniSat/CaDiCaL/Kissat
    /// convention, as opposed to unrestricted DP-style elimination). `score_variable` estimates elimination cost
    /// (roughly, resolvent count versus removed clause count) to prioritize cheap eliminations; `can_eliminate`
    /// confirms a variable stays within cost bounds; `build_resolvents` constructs the replacement clause set;
    /// `apply_elimination` commits it to `clause::database`, removing `v` from active search; `emit_extension_record`
    /// pushes an `extension_record::bve_elimination_t` entry so `model_reconstructor` can re-derive `v`'s value from
    /// its original defining clauses afterward.
    /// @note This is one of the risk points requiring explicit handling: interactions between BVE and model
    /// reconstruction, and the corresponding compaction/reindexing of watch lists, reasons, and external mapping once
    /// enough variables have been eliminated (see `compaction_service`).
    /// @warning Not sound as it stands, and not reachable from the default pass set for that reason. Two
    /// separate defects are known:
    ///   - Incremental use. Eliminating a variable discards the clauses constraining it, so a clause added over
    ///     that variable after a solve has nothing left to contradict it and the next solve reports satisfiable
    ///     on an unsatisfiable formula. Making this safe needs the eliminated clauses restored when a later
    ///     clause mentions the variable, which is not implemented.
    ///   - A remaining loss of constraints on larger formulas, reproducible with this pass as the *only* enabled
    ///     pass on an 11-pigeon/10-hole pigeonhole instance (110 variables, 561 clauses): the reduced formula
    ///     becomes satisfiable, and the model verifier rejects the resulting witness. The same family passes at
    ///     9 holes and below, so it is size- or count-dependent rather than structural. Assumption freezing,
    ///     one-sided witnesses and the problem-variable range were each found and fixed while narrowing this
    ///     down; whatever remains is not one of those.
    /// Enable it only through `solve_request::enabled_pass_mask`, and only with model verification on.
    class bounded final
    {
    public:
        /// @brief Constructs a bounded variable eliminator with default cost limits.
        /// @throws None (noexcept).
        bounded() noexcept = default;

        /// @brief Attaches the clause database the pass reads and rewrites.
        /// @param database Clause database to simplify.
        /// @throws None (noexcept).
        void attach_clause_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        /// @brief Attaches the extension stack that records eliminations for model reconstruction.
        /// @param extension_stack Journal receiving one record per eliminated variable.
        /// @throws None (noexcept).
        void attach_extension_stack(cdcl::stack::extension& extension_stack) noexcept { extension_stack_ = &extension_stack; }

        /// @brief Attaches the proof manager that logs added resolvents and deleted clauses.
        /// @param proof_manager Proof manager to log through.
        /// @throws None (noexcept).
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        /// @brief Registers a sink invoked for every resolvent added to the database.
        /// @param sink Callable receiving each new clause reference so propagation state can be updated.
        /// @throws None (noexcept).
        void attach_clause_sink(std::function<void(cdcl::clause::ref_t)> sink) noexcept { clause_sink_ = std::move(sink); }

        /// @brief Marks variables that must never be eliminated.
        /// @details Eliminating a variable discards every clause constraining it, keeping only the resolvents. That
        /// is sound for the formula as it stands, but not for anything said about the variable afterwards: an
        /// assumption over an eliminated variable has no clauses left to contradict it, so the solver answers
        /// satisfiable and returns a "model" that violates the assumption. Any variable the caller can still refer
        /// to therefore has to be off limits.
        /// @param variables Variables to protect from elimination.
        /// @throws None (noexcept).
        void set_frozen_variables(const std::span<const kmx::sat::variable> variables) noexcept
        {
            frozen_.assign(variables.begin(), variables.end());
        }

        /// @brief Clears the frozen set, allowing every variable to be considered again.
        void clear_frozen_variables() noexcept { frozen_.clear(); }

        /// @brief Returns whether a variable is protected from elimination.
        [[nodiscard]] bool is_frozen(const kmx::sat::variable var) const noexcept;

        /// @brief Sets the largest allowed increase in clause count per eliminated variable.
        /// @param growth Maximum resolvents minus removed clauses; zero permits only non-increasing eliminations.
        /// @throws None (noexcept).
        void set_clause_growth_limit(const std::int64_t growth) noexcept { clause_growth_limit_ = growth; }

        /// @brief Sets the largest resolvent width the pass will produce.
        /// @param width Maximum literal count of an accepted resolvent.
        /// @throws None (noexcept).
        void set_resolvent_width_limit(const std::size_t width) noexcept { resolvent_width_limit_ = width; }

        /// @brief Rebuilds the literal-to-clause occurrence index from the attached database.
        /// @details `run` does this itself. Call it explicitly before `score_variable` or `can_eliminate` when
        /// inspecting candidates outside a pass, since both read the index rather than the database directly.
        /// @throws None (noexcept).
        void refresh_occurrence_index() noexcept
        {
            if (database_ != nullptr)
                build_occurrences();
        }

        /// @brief Runs a full bounded-variable-elimination pass over eligible variables.
        /// @details Each candidate variable is resolved only when the replacement clause set does not grow the
        /// formula beyond the configured limits. Every removed clause is recorded on the extension stack before it
        /// is deleted, because a satisfying assignment of the reduced formula says nothing about the eliminated
        /// variable and `model_reconstructor` has to re-derive it from exactly those clauses.
        /// @throws None (noexcept).
        void run() noexcept;

        /// @brief Estimates the elimination cost of a variable (resolvent count versus removed clause count).
        /// @param var Variable to score.
        /// @return Estimated growth in clause count; non-positive values indicate a favorable elimination.
        /// @throws None (noexcept).
        std::int64_t score_variable(const kmx::sat::variable var) const noexcept;

        /// @brief Checks whether eliminating a variable stays within the configured cost limits.
        /// @param var Variable to check.
        /// @return True if elimination is permitted under current cost limits.
        /// @throws None (noexcept).
        bool can_eliminate(const kmx::sat::variable var) const noexcept;

        /// @brief Constructs the resolvent clause set that would replace a variable's occurrences.
        /// @param var Variable to build resolvents for.
        /// @return True if a bounded, non-tautological resolvent set was produced.
        /// @throws None (noexcept).
        bool build_resolvents(const kmx::sat::variable var) noexcept;

        /// @brief Commits the elimination of a variable, replacing its clauses with the built resolvents.
        /// @param var Variable to eliminate.
        /// @throws None (noexcept).
        void apply_elimination(const kmx::sat::variable var) noexcept;

        /// @brief Records the eliminated variable and its clauses on the extension stack.
        /// @param var Variable being eliminated.
        /// @throws None (noexcept).
        void emit_extension_record(const kmx::sat::variable var) noexcept;

        /// @brief Returns the number of elimination rounds committed.
        std::uint64_t elimination_count() const noexcept { return elimination_count_; }

        /// @brief Returns the eliminated variables recorded by the last run.
        const std::vector<kmx::sat::variable>& eliminated_variables() const noexcept { return eliminated_variables_; }

    private:
        struct occurrence_entry final
        {
            std::vector<cdcl::clause::ref_t> positive {};
            std::vector<cdcl::clause::ref_t> negative {};
        };

        /// @brief Resolves two clauses on `var`, writing the result into `resolvent_scratch_`.
        /// @return False when the resolvent is tautological and can be discarded.
        /// @throws None (noexcept).
        bool resolve(const std::span<const literal> positive, const std::span<const literal> negative,
                     const kmx::sat::variable var) noexcept;

        /// @brief Marks a clause deleted and logs the deletion to the proof.
        /// @param ref Clause to delete.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept;

        /// @brief Adds one clause's occurrences to the index.
        /// @throws None (noexcept).
        void register_occurrence(const cdcl::clause::ref_t ref, const std::span<const literal> literals) noexcept;

        /// @brief Builds the literal-to-clause occurrence index over the irredundant clause set.
        /// @details Only irredundant clauses take part: a learned clause is implied by them, so resolving it away
        /// would be redundant work, and keeping one that mentions an eliminated variable would reintroduce it.
        /// @throws None (noexcept).
        void build_occurrences() noexcept;

        std::vector<kmx::sat::variable> frozen_ {};
        cdcl::clause::database* database_ {};
        cdcl::stack::extension* extension_stack_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::function<void(cdcl::clause::ref_t)> clause_sink_ {};
        std::int64_t clause_growth_limit_ {};
        std::size_t resolvent_width_limit_ {16u};
        std::vector<occurrence_entry> occurrences_ {};
        clause_list_t resolvents_ {};
        std::vector<literal> resolvent_scratch_ {};
        std::vector<kmx::sat::variable> eliminated_variables_ {};
        std::uint64_t elimination_count_ {};
    };
}
