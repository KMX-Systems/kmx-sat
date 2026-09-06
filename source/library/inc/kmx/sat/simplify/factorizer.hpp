/// @file inc/kmx/sat/simplify/factorizer.hpp
/// @brief Bounded variable addition: trades a rectangle of clauses for one fresh variable's definition.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <limits>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/extension_record.hpp>
#include <kmx/sat/cdcl/stack/extension.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::simplify
{
    /// @brief Bounded variable addition (BVA, "factoring"): the inverse of variable elimination.
    /// @details
    /// A *rectangle* is a set of literals L and a set of clause bodies D such that the formula contains the clause
    /// (l ∨ d) for every l ∈ L and d ∈ D: |L|·|D| clauses that all say the same thing about the pair. A fresh
    /// variable x standing for "some literal of L holds" replaces them by (¬x ∨ l) for each l and (x ∨ d) for each
    /// d, |L| + |D| clauses in all. The saving is |L|·|D| − |L| − |D| clauses, and the proofs get shorter than the
    /// clauses: at-most-one constraints, which pigeonhole, colouring, scheduling and planning encodings all carry
    /// as quadratically many binary clauses, become a chain through x on which resolution is exponentially cheaper.
    /// Measured on `hole9`, Kissat needs 16,649 conflicts with factoring and 289,997 without.
    ///
    /// Search: for each literal l in decreasing occurrence order, L starts as {l} and D as the bodies of its
    /// clauses; each step finds the literal l' that most of the bodies pair with (a clause d ∪ {l'} exists), and
    /// adds it to L while the rectangle's saving keeps growing, dropping the bodies it does not match. A body is
    /// matched by scanning the occurrence list of its least frequent literal against a mark table. Effort is
    /// bounded by clause visits so a pass stays a small share of the formula's size.
    ///
    /// Soundness: F ≡ ∃x. F', so a model of F' projects to a model of F and the introduced variable is dropped from
    /// the external model through the extension stack; nothing has to be reconstructed. The added clauses are RAT
    /// (on ¬x while x is fresh, then on x against the definitions), so a DRAT proof records them as such with the
    /// pivot first, and the removed clauses are their resolvents. The pass is skipped while a proof consumer is
    /// attached, because the LRAT checkers cannot take RAT steps without hints.
    class factorizer final
    {
    public:
        using clause_id_t = std::uint32_t;

        factorizer() noexcept = default;

        void attach_clause_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        void attach_extension_stack(cdcl::stack::extension& extension_stack) noexcept { extension_stack_ = &extension_stack; }

        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        /// @brief Declares the variables the problem owns, so fresh variables are numbered above them.
        /// @details The database alone cannot say: a problem variable that occurs in no clause (declared in the
        /// header, mentioned only in assumptions, or freed by an earlier pass) is invisible there, and a fresh
        /// variable numbered from the database's highest occurrence would capture it. The model would then drop it
        /// as internal, and an assumption on it would constrain the definition instead of the problem.
        void set_problem_variable_count(const variable::index_t count) noexcept { problem_variable_count_ = count; }

        /// @brief Largest clause considered as a rectangle member; long clauses rarely pair up and cost the most to match.
        void set_maximum_clause_size(const std::size_t size) noexcept { maximum_clause_size_ = size; }

        /// @brief Clause visits allowed per pass, as a multiple of the indexed clause count.
        void set_effort_per_clause(const std::size_t effort) noexcept { effort_per_clause_ = effort; }

        /// @brief Runs one full factoring pass over the irredundant clauses.
        void run() noexcept
        {
            ++run_count_;
            if (database_ == nullptr)
                return;
            if (proof_manager_ != nullptr && proof_manager_->has_active_consumers())
                return;
            if (!build_index())
                return;

            for (std::size_t round = 0u; round < maximum_rounds && effort_ != 0u; ++round)
            {
                collect_candidates();
                bool progress = false;
                for (const auto candidate: candidates_)
                {
                    if (effort_ == 0u)
                        break;
                    progress |= factor_literal(candidate);
                }
                if (!progress)
                    break;
            }

            database_->flush_satisfied([this](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
            release_index();
        }

        std::uint64_t run_count() const noexcept { return run_count_; }

        std::uint64_t introduced_variable_count() const noexcept { return introduced_variable_count_; }

        std::uint64_t added_clause_count() const noexcept { return added_clause_count_; }

        std::uint64_t removed_clause_count() const noexcept { return removed_clause_count_; }

        variable last_introduced_variable() const noexcept { return last_introduced_variable_; }

    private:
        static constexpr clause_id_t invalid_clause {std::numeric_limits<clause_id_t>::max()};
        static constexpr std::size_t maximum_rounds {2u};
        static constexpr std::size_t minimum_effort {std::size_t {1u} << 16u};
        static constexpr std::size_t default_maximum_clause_size {5u};
        static constexpr std::size_t default_effort_per_clause {4u};

        struct match final
        {
            std::uint32_t row {};
            literal::raw_t lit {};
            clause_id_t clause {};
        };

        /// @brief Indexes the alive irredundant clauses of usable size into occurrence lists.
        /// @return False if there is nothing to factor.
        bool build_index() noexcept
        {
            release_index();
            const auto& storage = database_->storage_of();
            variable::index_t highest {};
            database_->iterate_irredundant(
                [&](const cdcl::clause::ref_t ref) noexcept
                {
                    if (!ref.valid() || !storage.is_alive(ref) || database_->is_garbage(ref))
                        return;
                    const auto literals = storage.view_literals(ref);
                    for (const auto lit: literals)
                        highest = std::max(highest, lit.variable_of().index());
                    if (literals.size() < 2u || literals.size() > maximum_clause_size_)
                        return;
                    register_clause(ref, literals);
                });
            highest_variable_ = std::max(highest, problem_variable_count_);
            ensure_literal_capacity(literal {variable {highest_variable_}, true}.raw());
            effort_ = refs_.size() * effort_per_clause_ + minimum_effort;
            return refs_.size() >= 3u;
        }

        void release_index() noexcept
        {
            refs_.clear();
            alive_.clear();
            sizes_.clear();
            for (auto& list: occurrences_)
                list.clear();
            candidates_.clear();
        }

        void register_clause(const cdcl::clause::ref_t ref, const std::span<const literal> literals) noexcept
        {
            const auto id = static_cast<clause_id_t>(refs_.size());
            refs_.push_back(ref);
            alive_.push_back(1u);
            sizes_.push_back(static_cast<std::uint32_t>(literals.size()));
            for (const auto lit: literals)
            {
                ensure_literal_capacity(lit.raw());
                occurrences_[lit.raw()].push_back(id);
            }
        }

        void ensure_literal_capacity(const literal::raw_t raw) noexcept
        {
            const auto needed = static_cast<std::size_t>(raw) + 2u;
            if (occurrences_.size() < needed)
            {
                occurrences_.resize(needed);
                marks_.resize(needed, 0u);
                selected_marks_.resize(needed, 0u);
                counts_.resize(needed, 0u);
            }
        }

        std::size_t alive_occurrences(const literal::raw_t raw) const noexcept
        {
            std::size_t count {};
            for (const auto id: occurrences_[raw])
                count += alive_[id];
            return count;
        }

        /// @brief Lists every literal with at least two live occurrences, most frequent first.
        void collect_candidates() noexcept
        {
            candidates_.clear();
            candidate_counts_.clear();
            for (literal::raw_t raw = 2u; raw < occurrences_.size(); ++raw)
            {
                const auto count = alive_occurrences(raw);
                if (count >= 2u)
                {
                    candidates_.push_back(literal {raw});
                    candidate_counts_.push_back(count);
                }
            }
            order_.resize(candidates_.size());
            for (std::size_t index = 0u; index < order_.size(); ++index)
                order_[index] = static_cast<std::uint32_t>(index);
            std::sort(order_.begin(), order_.end(),
                      [this](const std::uint32_t left, const std::uint32_t right) noexcept
                      {
                          if (candidate_counts_[left] != candidate_counts_[right])
                              return candidate_counts_[left] > candidate_counts_[right];
                          return candidates_[left].raw() < candidates_[right].raw();
                      });
            ordered_candidates_.resize(candidates_.size());
            for (std::size_t index = 0u; index < order_.size(); ++index)
                ordered_candidates_[index] = candidates_[order_[index]];
            candidates_.swap(ordered_candidates_);
        }

        /// @brief Grows the best rectangle anchored at `anchor` and applies it if it saves at least one clause.
        bool factor_literal(const literal anchor) noexcept
        {
            rows_.clear();
            for (const auto id: occurrences_[anchor.raw()])
                if (alive_[id] != 0u)
                    rows_.push_back(id);
            if (rows_.size() < 2u)
                return false;

            selected_.clear();
            selected_.push_back(anchor);
            selected_marks_[anchor.raw()] = 1u;
            matched_.assign(rows_.begin(), rows_.end()); // width 1: the anchor's own clause per row

            while (effort_ != 0u)
            {
                const auto best = best_partner(anchor);
                if (best.raw() == 0u)
                    break;
                const auto width = selected_.size();
                const auto matched_rows = static_cast<std::size_t>(counts_[best.raw()]);
                const auto saving_now = saving(width, rows_.size());
                const auto saving_next = saving(width + 1u, matched_rows);
                clear_counts();
                if (saving_next <= saving_now)
                    break;
                adopt_partner(best);
            }

            for (const auto lit: selected_)
                selected_marks_[lit.raw()] = 0u;
            if (selected_.size() < 2u || saving(selected_.size(), rows_.size()) < 1)
                return false;
            apply(anchor);
            return true;
        }

        static std::int64_t saving(const std::size_t literals, const std::size_t bodies) noexcept
        {
            const auto l = static_cast<std::int64_t>(literals);
            const auto b = static_cast<std::int64_t>(bodies);
            return l * b - l - b;
        }

        /// @brief Counts, for every literal, the rows whose body it pairs with; returns the most frequent one.
        /// @details Leaves the counts in `counts_` (cleared by the caller) and the pairings in `matches_`.
        literal best_partner(const literal anchor) noexcept
        {
            matches_.clear();
            touched_.clear();
            const auto& storage = database_->storage_of();
            for (std::uint32_t row = 0u; row < rows_.size() && effort_ != 0u; ++row)
            {
                const auto clause = rows_[row];
                const auto literals = storage.view_literals(refs_[clause]);
                literal pivot {};
                std::size_t pivot_occurrences = std::numeric_limits<std::size_t>::max();
                for (const auto lit: literals)
                {
                    if (lit.raw() == anchor.raw())
                        continue;
                    marks_[lit.raw()] = 1u;
                    if (occurrences_[lit.raw()].size() < pivot_occurrences)
                    {
                        pivot_occurrences = occurrences_[lit.raw()].size();
                        pivot = lit;
                    }
                }
                const auto size = sizes_[clause];
                for (const auto other: occurrences_[pivot.raw()])
                {
                    if (effort_ == 0u)
                        break;
                    --effort_;
                    if (other == clause || alive_[other] == 0u || sizes_[other] != size)
                        continue;
                    literal extra {};
                    std::uint32_t marked {};
                    for (const auto lit: storage.view_literals(refs_[other]))
                    {
                        if (marks_[lit.raw()] != 0u)
                            ++marked;
                        else
                            extra = lit;
                    }
                    if (marked + 1u != size || extra.raw() == 0u)
                        continue;
                    if (extra.raw() == anchor.raw() || extra.variable_of().index() == anchor.variable_of().index())
                        continue;
                    if (selected_marks_[extra.raw()] != 0u || marks_[extra.negated().raw()] != 0u)
                        continue;
                    if (counts_[extra.raw()]++ == 0u)
                        touched_.push_back(extra);
                    matches_.push_back(match {row, extra.raw(), other});
                }
                for (const auto lit: literals)
                    marks_[lit.raw()] = 0u;
            }

            literal best {};
            std::uint32_t best_count {};
            for (const auto lit: touched_)
            {
                const auto count = counts_[lit.raw()];
                if (count > best_count || (count == best_count && lit.raw() < best.raw()))
                {
                    best = lit;
                    best_count = count;
                }
            }
            return best_count >= 2u ? best : literal {};
        }

        void clear_counts() noexcept
        {
            for (const auto lit: touched_)
                counts_[lit.raw()] = 0u;
            touched_.clear();
        }

        /// @brief Restricts the rectangle to the rows `partner` pairs with and records the pairing clause per row.
        void adopt_partner(const literal partner) noexcept
        {
            const auto width = selected_.size();
            pending_.assign(rows_.size(), invalid_clause);
            for (const auto& entry: matches_)
                if (entry.lit == partner.raw() && pending_[entry.row] == invalid_clause)
                    pending_[entry.row] = entry.clause;

            rows_scratch_.clear();
            matched_scratch_.clear();
            for (std::uint32_t row = 0u; row < rows_.size(); ++row)
            {
                if (pending_[row] == invalid_clause)
                    continue;
                rows_scratch_.push_back(rows_[row]);
                for (std::size_t column = 0u; column < width; ++column)
                    matched_scratch_.push_back(matched_[row * width + column]);
                matched_scratch_.push_back(pending_[row]);
            }
            rows_.swap(rows_scratch_);
            matched_.swap(matched_scratch_);
            selected_.push_back(partner);
            selected_marks_[partner.raw()] = 1u;
        }

        /// @brief Introduces the fresh variable, adds its definition and the factored bodies, deletes the rectangle.
        void apply(const literal anchor) noexcept
        {
            const auto& storage = database_->storage_of();
            const variable fresh {++highest_variable_};
            const literal positive {fresh, false};
            const literal negative {fresh, true};
            ensure_literal_capacity(negative.raw());

            if (extension_stack_ != nullptr)
            {
                // The group is recorded with the variable: reconstruction only drops the variable, but the search
                // needs the group to phase the variable consistently with a walk's assignment.
                const auto mark = extension_stack_->witness_mark();
                extension_stack_->append_witness_clause(selected_);
                extension_stack_->push_factor_transformation(fresh, mark);
            }

            // Definitions first: while x is fresh every clause holding ¬x is RAT on ¬x without a resolvent.
            for (const auto lit: selected_)
            {
                const std::array<literal, 2> definition {negative, lit};
                add_clause(definition);
            }
            // Bodies next: (x ∨ d) is RAT on x, its resolvents with the definitions being the (l ∨ d) still present.
            const auto width = selected_.size();
            for (std::size_t row = 0u; row < rows_.size(); ++row)
            {
                clause_scratch_.clear();
                clause_scratch_.push_back(positive);
                for (const auto lit: storage.view_literals(refs_[rows_[row]]))
                    if (lit.raw() != anchor.raw())
                        clause_scratch_.push_back(lit);
                add_clause(clause_scratch_);
            }
            // The rectangle itself is now implied and goes.
            for (std::size_t row = 0u; row < rows_.size(); ++row)
                for (std::size_t column = 0u; column < width; ++column)
                    delete_clause(matched_[row * width + column]);

            ++introduced_variable_count_;
            last_introduced_variable_ = fresh;
        }

        void add_clause(const std::span<const literal> literals) noexcept
        {
            const auto ref = database_->add_clause(literals, false);
            if (!ref.valid())
                return;
            if (proof_manager_ != nullptr)
                proof_manager_->on_add_derived(ref, literals);
            register_clause(ref, literals);
            ++added_clause_count_;
        }

        void delete_clause(const clause_id_t id) noexcept
        {
            if (id == invalid_clause || alive_[id] == 0u)
                return;
            alive_[id] = 0u;
            const auto ref = refs_[id];
            if (proof_manager_ != nullptr)
                proof_manager_->on_delete_clause(ref);
            database_->mark_garbage(ref);
            ++removed_clause_count_;
        }

        cdcl::clause::database* database_ {};
        cdcl::stack::extension* extension_stack_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::size_t maximum_clause_size_ {default_maximum_clause_size};
        std::size_t effort_per_clause_ {default_effort_per_clause};
        std::size_t effort_ {};
        variable::index_t highest_variable_ {};
        variable::index_t problem_variable_count_ {};

        std::vector<cdcl::clause::ref_t> refs_ {};
        std::vector<std::uint8_t> alive_ {};
        std::vector<std::uint32_t> sizes_ {};
        std::vector<std::vector<clause_id_t>> occurrences_ {};
        std::vector<std::uint8_t> marks_ {};
        std::vector<std::uint8_t> selected_marks_ {};
        std::vector<std::uint32_t> counts_ {};
        std::vector<literal> touched_ {};
        std::vector<match> matches_ {};
        std::vector<literal> candidates_ {};
        std::vector<literal> ordered_candidates_ {};
        std::vector<std::size_t> candidate_counts_ {};
        std::vector<std::uint32_t> order_ {};
        std::vector<clause_id_t> rows_ {};
        std::vector<clause_id_t> rows_scratch_ {};
        std::vector<clause_id_t> matched_ {};
        std::vector<clause_id_t> matched_scratch_ {};
        std::vector<clause_id_t> pending_ {};
        std::vector<literal> selected_ {};
        std::vector<literal> clause_scratch_ {};

        std::uint64_t run_count_ {};
        std::uint64_t introduced_variable_count_ {};
        std::uint64_t added_clause_count_ {};
        std::uint64_t removed_clause_count_ {};
        variable last_introduced_variable_ {};
    };
}
