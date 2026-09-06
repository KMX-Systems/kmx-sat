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
    /// pushes an `extension_record::bve_elimination` entry so `model_reconstructor` can re-derive `v`'s value from
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
        [[nodiscard]] bool is_frozen(const kmx::sat::variable var) const noexcept
        {
            for (const auto frozen: frozen_)
                if (frozen.index() == var.index())
                    return true;
            return false;
        }

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
        void run() noexcept
        {
            eliminated_variables_.clear();
            if (database_ == nullptr)
                return;

            build_occurrences();

            for (std::uint32_t index = 1u; index < occurrences_.size(); ++index)
            {
                const kmx::sat::variable candidate {index};
                if (!can_eliminate(candidate))
                    continue;
                if (!build_resolvents(candidate))
                    continue;
                apply_elimination(candidate);
            }

            // Compaction is deferred to the end of the pass: `flush_satisfied` relocates clauses, which would
            // dangle the references still held in the occurrence lists of variables not yet considered.
            if (elimination_count_ != 0u)
                database_->flush_satisfied([this](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
        }

        /// @brief Estimates the elimination cost of a variable (resolvent count versus removed clause count).
        /// @param var Variable to score.
        /// @return Estimated growth in clause count; non-positive values indicate a favorable elimination.
        /// @throws None (noexcept).
        std::int64_t score_variable(const kmx::sat::variable var) const noexcept
        {
            if (database_ == nullptr)
                return 1;

            const auto index = static_cast<std::size_t>(var.index());
            if (index >= occurrences_.size())
                return 1;

            const auto& entry = occurrences_[index];
            const auto removed = static_cast<std::int64_t>(entry.positive.size() + entry.negative.size());
            const auto produced = static_cast<std::int64_t>(entry.positive.size() * entry.negative.size());
            return produced - removed;
        }

        /// @brief Checks whether eliminating a variable stays within the configured cost limits.
        /// @param var Variable to check.
        /// @return True if elimination is permitted under current cost limits.
        /// @throws None (noexcept).
        bool can_eliminate(const kmx::sat::variable var) const noexcept
        {
            if (database_ == nullptr)
                return false;

            const auto index = static_cast<std::size_t>(var.index());
            if (index >= occurrences_.size())
                return false;

            if (is_frozen(var))
                return false;

            const auto& entry = occurrences_[index];
            if (entry.positive.empty() && entry.negative.empty())
                return false;

            return score_variable(var) <= clause_growth_limit_;
        }

        /// @brief Constructs the resolvent clause set that would replace a variable's occurrences.
        /// @param var Variable to build resolvents for.
        /// @return True if a bounded, non-tautological resolvent set was produced.
        /// @throws None (noexcept).
        bool build_resolvents(const kmx::sat::variable var) noexcept
        {
            resolvents_.clear();
            if (database_ == nullptr)
                return false;

            const auto index = static_cast<std::size_t>(var.index());
            if (index >= occurrences_.size())
                return false;

            const auto& entry = occurrences_[index];
            const auto& storage = database_->storage_of();

            for (const auto positive_ref: entry.positive)
            {
                if (database_->is_garbage(positive_ref))
                    continue;
                for (const auto negative_ref: entry.negative)
                {
                    if (database_->is_garbage(negative_ref))
                        continue;

                    resolvent_scratch_.clear();
                    if (!resolve(storage.view_literals(positive_ref), storage.view_literals(negative_ref), var))
                        continue;
                    if (resolvent_scratch_.size() > resolvent_width_limit_)
                        return false;

                    resolvents_.push_back(resolvent_scratch_);
                    if (static_cast<std::int64_t>(resolvents_.size()) >
                        static_cast<std::int64_t>(entry.positive.size() + entry.negative.size()) + clause_growth_limit_)
                        return false;
                }
            }

            // An empty resolvent means the formula is unsatisfiable; that is a conclusion for the search to
            // reach through normal propagation, not something this pass should encode by deleting clauses.
            return std::none_of(resolvents_.begin(), resolvents_.end(),
                                [](const std::vector<literal>& clause) noexcept { return clause.empty(); });
        }

        /// @brief Commits the elimination of a variable, replacing its clauses with the built resolvents.
        /// @param var Variable to eliminate.
        /// @throws None (noexcept).
        void apply_elimination(const kmx::sat::variable var) noexcept
        {
            if (database_ == nullptr)
                return;

            const auto index = static_cast<std::size_t>(var.index());
            if (index >= occurrences_.size())
                return;

            emit_extension_record(var);

            auto& entry = occurrences_[index];
            for (const auto ref: entry.positive)
                delete_clause(ref);
            for (const auto ref: entry.negative)
                delete_clause(ref);

            for (const auto& resolvent: resolvents_)
            {
                const auto ref = database_->add_clause(std::span<const literal> {resolvent}, false);
                if (proof_manager_ != nullptr)
                    proof_manager_->on_add_original(ref, std::span<const literal> {resolvent});
                if (clause_sink_)
                    clause_sink_(ref);
                register_occurrence(ref, resolvent);
            }

            entry.positive.clear();
            entry.negative.clear();
            eliminated_variables_.push_back(var);
            ++elimination_count_;
        }

        /// @brief Records the eliminated variable and its clauses on the extension stack.
        /// @param var Variable being eliminated.
        /// @throws None (noexcept).
        void emit_extension_record(const kmx::sat::variable var) noexcept
        {
            if (extension_stack_ == nullptr || database_ == nullptr)
                return;

            const auto index = static_cast<std::size_t>(var.index());
            if (index >= occurrences_.size())
                return;

            const auto& entry = occurrences_[index];
            const auto& storage = database_->storage_of();
            const auto mark = extension_stack_->witness_mark();
            // Only the clauses containing the variable positively are stored, and that is not an economy: it is
            // what makes reconstruction correct. Setting the variable false satisfies every clause containing it
            // negatively outright, so those need no witness; the only question left is whether some clause
            // containing it positively is otherwise unsatisfied, in which case setting it true rescues that clause
            // without endangering the negative ones -- the resolvents added in its place guarantee a positive and
            // a negative clause cannot both be otherwise unsatisfied. Storing both sides instead leaves the replay
            // flipping the variable back and forth per clause and settling on whichever came last, which is how a
            // reconstructed "model" ends up falsifying a clause of the original formula.
            for (const auto ref: entry.positive)
                if (!database_->is_garbage(ref))
                    extension_stack_->append_witness_clause(storage.view_literals(ref));
            extension_stack_->push_bve_elimination(var, mark);
        }

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
                     const kmx::sat::variable var) noexcept
        {
            const auto append = [this, var](const std::span<const literal> source) noexcept
            {
                for (const auto lit: source)
                {
                    if (lit.variable_of().index() == var.index())
                        continue;
                    const auto duplicate = std::find_if(resolvent_scratch_.begin(), resolvent_scratch_.end(),
                                                        [lit](const literal existing) noexcept
                                                        { return existing.raw() == lit.raw(); });
                    if (duplicate == resolvent_scratch_.end())
                        resolvent_scratch_.push_back(lit);
                }
            };

            append(positive);
            const auto boundary = resolvent_scratch_.size();
            append(negative);

            for (std::size_t left = 0u; left < boundary; ++left)
                for (std::size_t right = boundary; right < resolvent_scratch_.size(); ++right)
                    if (resolvent_scratch_[left].raw() == resolvent_scratch_[right].negated().raw())
                        return false;

            return true;
        }

        /// @brief Marks a clause deleted and logs the deletion to the proof.
        /// @param ref Clause to delete.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
            if (database_->is_garbage(ref))
                return;
            if (proof_manager_ != nullptr)
                proof_manager_->on_delete_clause(ref);
            database_->mark_garbage(ref);
        }

        /// @brief Adds one clause's occurrences to the index.
        /// @throws None (noexcept).
        void register_occurrence(const cdcl::clause::ref_t ref, const std::span<const literal> literals) noexcept
        {
            for (const auto lit: literals)
            {
                const auto index = static_cast<std::size_t>(lit.variable_of().index());
                if (index >= occurrences_.size())
                    occurrences_.resize(index + 1u);
                if (lit.is_negated())
                    occurrences_[index].negative.push_back(ref);
                else
                    occurrences_[index].positive.push_back(ref);
            }
        }

        /// @brief Builds the literal-to-clause occurrence index over the irredundant clause set.
        /// @details Only irredundant clauses take part: a learned clause is implied by them, so resolving it away
        /// would be redundant work, and keeping one that mentions an eliminated variable would reintroduce it.
        /// @throws None (noexcept).
        void build_occurrences() noexcept
        {
            occurrences_.clear();
            const auto& storage = database_->storage_of();
            database_->iterate_irredundant(
                [this, &storage](const cdcl::clause::ref_t ref) noexcept
                {
                    if (!database_->is_garbage(ref))
                        register_occurrence(ref, storage.view_literals(ref));
                });
        }

        std::vector<kmx::sat::variable> frozen_ {};
        cdcl::clause::database* database_ {};
        cdcl::stack::extension* extension_stack_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::function<void(cdcl::clause::ref_t)> clause_sink_ {};
        std::int64_t clause_growth_limit_ {};
        std::size_t resolvent_width_limit_ {16u};
        std::vector<occurrence_entry> occurrences_ {};
        std::vector<std::vector<literal>> resolvents_ {};
        std::vector<literal> resolvent_scratch_ {};
        std::vector<kmx::sat::variable> eliminated_variables_ {};
        std::uint64_t elimination_count_ {};
    };
}
