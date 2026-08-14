/// @file inc/kmx/sat/cdcl/clause/learner.hpp
/// @brief Single entry point for all clauses derived from conflict analysis.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::cdcl::clause
{
    /// @brief Single entry point for all clauses derived from conflict analysis.
    ///
    /// @details
    /// `clause::learner` is the funnel every clause produced by `conflict_analyzer`/`clause::minimizer` passes
    /// through before it becomes part of `clause::database`: `learn_clause` handles the general case, while
    /// `register_unit`/`register_binary`/`register_large` provide specialized fast paths matching CaDiCaL/Kissat's
    /// convention of treating unit and binary clauses without full arena-allocated storage where possible.
    /// `assign_asserting_literal` performs the implied assignment of the derived clause's asserting (1-UIP) literal
    /// at the computed backjump level immediately after `engine::backtrack` unwinds, completing the conflict-driven
    /// learning cycle before propagation resumes.
    class learner final
    {
    public:
        /// @brief Constructs a clause learner with no pending clause.
        /// @throws None (noexcept).
        learner() noexcept = default;

        /// @brief Registers a general learned clause with the clause database.
        /// @param literals Literals composing the learned clause.
        /// @return Reference to the newly registered clause.
        /// @throws None (noexcept).
        ref_t learn_clause(const std::span<const literal> literals) noexcept
        {
            if (literals.empty())
                return {};

            const auto normalized_clause = normalize_clause_literals(literals);
            if (normalized_clause.empty())
                return {};

            if (normalized_clause.size() == 1)
            {
                register_unit(normalized_clause.front());
                return last_learned_ref_;
            }

            if (normalized_clause.size() == 2)
            {
                register_binary(normalized_clause[0], normalized_clause[1]);
                return last_learned_ref_;
            }

            return register_large(std::span<const literal> {normalized_clause});
        }

        /// @brief Registers a learned unit clause using the specialized unit fast path.
        /// @param lit The single literal of the unit clause.
        /// @throws None (noexcept).
        void register_unit(const literal lit) noexcept
        {
            const literal unit_clause[] {lit};
            last_learned_ref_ = append_clause(std::span<const literal> {unit_clause});
        }

        /// @brief Registers a learned binary clause using the specialized binary fast path.
        /// @param first First literal of the binary clause.
        /// @param second Second literal of the binary clause.
        /// @throws None (noexcept).
        void register_binary(const literal first, const literal second) noexcept
        {
            const literal binary_clause[] {first, second};
            last_learned_ref_ = append_clause(std::span<const literal> {binary_clause});
        }

        /// @brief Registers a learned clause with three or more literals.
        /// @param literals Literals composing the learned clause.
        /// @return Reference to the newly registered clause.
        /// @throws None (noexcept).
        ref_t register_large(const std::span<const literal> literals) noexcept
        {
            last_learned_ref_ = append_clause(literals);
            return last_learned_ref_;
        }

        /// @brief Assigns the derived clause's asserting literal at the post-backjump decision level.
        /// @param lit Asserting literal to assign.
        /// @throws None (noexcept).
        void assign_asserting_literal(const literal lit) noexcept { last_asserting_literal_ = lit; }

        /// @brief Returns the number of clauses recorded by this learner.
        /// @return Number of learned clauses.
        /// @throws None (noexcept).
        std::size_t learned_clause_count() const noexcept { return learned_clauses_.size(); }

        /// @brief Returns the most recently recorded learned clause.
        /// @return Read-only span over the latest learned clause, or empty if none exists.
        /// @throws None (noexcept).
        std::span<const literal> last_learned_clause() const noexcept
        {
            if (learned_clauses_.empty())
                return {};
            return learned_clauses_.back();
        }

        /// @brief Returns the most recently assigned asserting literal.
        /// @return Last asserting literal assigned via `assign_asserting_literal`.
        /// @throws None (noexcept).
        literal last_asserting_literal() const noexcept { return last_asserting_literal_; }

        /// @brief Returns whether this learner has recorded at least one learned clause.
        /// @return True if `learn_clause` or a specialized registration path has created a clause.
        [[nodiscard]] bool has_pending_clause() const noexcept { return !learned_clauses_.empty(); }

        /// @brief Returns how many learned clauses of the requested size were registered.
        /// @param size Clause size to count.
        /// @return Number of learned clauses with the given size.
        std::size_t clause_count_for_size(const std::size_t size) const noexcept
        {
            std::size_t count {};
            for (const auto& clause: learned_clauses_)
                if (clause.size() == size)
                    ++count;
            return count;
        }

    private:
        static std::vector<literal> normalize_clause_literals(const std::span<const literal> literals) noexcept
        {
            std::vector<literal> normalized {};
            normalized.reserve(literals.size());

            for (const auto lit: literals)
            {
                const auto duplicate_it = std::find_if(normalized.begin(), normalized.end(),
                                                       [lit](const literal existing) noexcept { return existing.raw() == lit.raw(); });
                if (duplicate_it != normalized.end())
                    continue;

                const auto opposite_it = std::find_if(
                    normalized.begin(), normalized.end(), [lit](const literal existing) noexcept
                    { return existing.variable_of().index() == lit.variable_of().index() && existing.is_negated() != lit.is_negated(); });
                if (opposite_it != normalized.end())
                    return {};

                normalized.push_back(lit);
            }

            return normalized;
        }

        ref_t append_clause(const std::span<const literal> literals) noexcept
        {
            if (literals.empty())
                return {};

            learned_clauses_.emplace_back(literals.begin(), literals.end());
            return ref_t {next_clause_offset_++};
        }

        std::vector<std::vector<literal>> learned_clauses_ {};
        ref_t last_learned_ref_ {};
        literal last_asserting_literal_ {};
        ref_t::offset_t next_clause_offset_ {1};
    };
}
