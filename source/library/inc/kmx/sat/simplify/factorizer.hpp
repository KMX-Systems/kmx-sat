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

        /// @brief Clause ids in which each candidate literal occurs.
        using occurrence_list_t = std::vector<std::vector<clause_id_t>>;

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
        void run() noexcept;

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
        bool build_index() noexcept;

        void release_index() noexcept;

        void register_clause(const cdcl::clause::ref_t ref, const std::span<const literal> literals) noexcept;

        void ensure_literal_capacity(const literal::raw_t raw) noexcept;

        std::size_t alive_occurrences(const literal::raw_t raw) const noexcept;

        /// @brief Lists every literal with at least two live occurrences, most frequent first.
        void collect_candidates() noexcept;

        /// @brief Grows the best rectangle anchored at `anchor` and applies it if it saves at least one clause.
        bool factor_literal(const literal anchor) noexcept;

        static std::int64_t saving(const std::size_t literals, const std::size_t bodies) noexcept;

        /// @brief Counts, for every literal, the rows whose body it pairs with; returns the most frequent one.
        /// @details Leaves the counts in `counts_` (cleared by the caller) and the pairings in `matches_`.
        literal best_partner(const literal anchor) noexcept;

        void clear_counts() noexcept;

        /// @brief Restricts the rectangle to the rows `partner` pairs with and records the pairing clause per row.
        void adopt_partner(const literal partner) noexcept;

        /// @brief Introduces the fresh variable, adds its definition and the factored bodies, deletes the rectangle.
        void apply(const literal anchor) noexcept;

        void add_clause(const std::span<const literal> literals) noexcept;

        void delete_clause(const clause_id_t id) noexcept;

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
        occurrence_list_t occurrences_ {};
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
