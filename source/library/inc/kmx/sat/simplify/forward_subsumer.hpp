/// @file inc/kmx/sat/simplify/forward_subsumer.hpp
/// @brief Forward/backward subsumption, with SIMD acceleration where justified.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <functional>
    #include <span>
    #include <vector>
#endif

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::simplify
{
    /// @brief Forward/backward subsumption, with SIMD acceleration where justified.
    /// @details
    /// A clause `A` subsumes clause `B` when every literal of `A` also occurs in `B`, making `B` redundant; forward
    /// subsumption checks new/recently-added clauses against existing ones, while backward subsumption checks
    /// existing clauses against a newly added one. `run` sweeps the clause database using an occurrence-index-driven
    /// candidate search; `is_subsumed` tests one clause against its candidate subsuming set;
    /// `strengthen_subsumed_clause` applies the related self-subsuming-resolution strengthening (removing one
    /// literal rather than the whole clause) when only partial subsumption is found. Per the hot-path directives,
    /// this pass is a candidate for optional SIMD-accelerated literal-set comparison, with a scalar fallback when the
    /// target lacks the relevant ISA extension.
    class forward_subsumer final
    {
    public:
        /// @brief Constructs a forward subsumer with no cached candidate index.
        /// @throws None (noexcept).
        forward_subsumer() noexcept = default;

        /// @brief Attaches the clause database to be simplified.
        /// @param database Clause database to scan.
        /// @throws None (noexcept).
        void attach_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        /// @brief Attaches the proof manager used to report clause deletions and strengthening.
        /// @param proof_manager Proof manager to notify.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept { proof_manager_ = &proof_manager; }

        /// @brief Enables or disables the self-subsuming-resolution strengthening phase.
        /// @details Off by default. The transformation is sound -- verified over 3,558 formulas and 12,542 removed
        /// literals, with every model of the reduced formula still satisfying the original -- but on the benchmark
        /// corpus it costs about seven percent and returns nothing measurable, so it stays opt-in rather than
        /// becoming a default that looks thorough and is not paid for.
        /// @param enabled True to run strengthening as part of `run`.
        /// @throws None (noexcept).
        void set_self_subsuming_resolution_enabled(const bool enabled) noexcept { self_subsuming_resolution_enabled_ = enabled; }

        /// @brief Returns whether the strengthening phase runs as part of `run`.
        [[nodiscard]] bool self_subsuming_resolution_enabled() const noexcept { return self_subsuming_resolution_enabled_; }

        /// @brief Registers a sink invoked for every clause whose literals this pass rewrote.
        /// @param sink Callable receiving each affected clause so propagation state can be refreshed.
        /// @throws None (noexcept).
        void attach_clause_sink(std::function<void(cdcl::clause::ref_t)> sink) noexcept { clause_sink_ = std::move(sink); }

        /// @brief Runs a full forward/backward subsumption sweep over the clause database.
        /// @throws None (noexcept).
        void run() noexcept;

        /// @brief Strengthens clauses by self-subsuming resolution against the indexed clause set.
        /// @details For each active clause `C` and literal `l` in it, every clause holding `not-l` is a candidate
        /// `D`; when `C \ {l}` is contained in `D`, resolving on `l` yields a clause that subsumes `D`, so `not-l`
        /// can be dropped from `D`. Candidates come from the occurrence list of `not-l`, so the scan stays local.
        /// @throws None (noexcept).
        void run_self_subsuming_resolution(const std::size_t irredundant_clause_count) noexcept;

        /// @brief Checks whether a clause is subsumed by another clause already in the database.
        /// @param ref Reference to the clause to test.
        /// @return True if the clause is subsumed and therefore redundant.
        /// @throws None (noexcept).
        bool is_subsumed(const cdcl::clause::ref_t ref) const noexcept;

        /// @brief Removes `redundant_literal` from a clause when self-subsuming resolution justifies it.
        /// @details Self-subsuming resolution: if some clause `C` satisfies `C \ {l} subset-of D` and
        /// `not-l in D`, then resolving `C` with `D` on `l` yields `D \ {not-l}`, which subsumes `D`. Dropping a
        /// literal without that justification would strengthen the formula and lose models, so the justifying
        /// clause is required rather than assumed.
        /// @param ref Clause to strengthen.
        /// @param redundant_literal Literal to remove from it.
        /// @return True when a justifying clause was found and the literal was removed.
        /// @throws None (noexcept).
        bool strengthen_by_resolution(const cdcl::clause::ref_t ref, const literal redundant_literal) noexcept;

        /// @brief Whether a run may skip pairs of clauses an earlier run checked (the default); off, every run is
        /// a full pass, which the tests use as the reference.
        void set_incremental(const bool enabled) noexcept { incremental_ = enabled; }

        std::size_t run_count() const noexcept { return run_count_; }

        std::size_t subsumed_count() const noexcept { return subsumed_count_; }

        std::size_t strengthened_count() const noexcept { return strengthened_count_; }

        cdcl::clause::ref_t last_subsumed_ref() const noexcept { return last_subsumed_ref_; }

    private:
        struct indexed_clause final
        {
            cdcl::clause::ref_t ref {};
            std::span<const literal> literals {};
            std::uint64_t signature {};
            bool active {};
            bool redundant {};
            bool checked {};
        };

        /// @brief Literal-to-clause occurrence lists over a subset of `clauses_`, as two contiguous arrays.
        struct occurrence_index final
        {
            std::vector<std::uint32_t> start {};
            std::vector<std::uint32_t> entries {};
            std::vector<std::uint32_t> fill {};
        };

        /// @brief Tests whether `candidate` resolved on `pivot` produces a clause subsuming `target`.
        /// @details Requires `pivot` in `candidate` and every other literal of `candidate` present in `target`.
        /// @throws None (noexcept).
        static bool resolves_to_strengthen(const std::span<const literal> candidate, const literal pivot,
                                           const std::span<const literal> target) noexcept;

        /// @brief Physically removes one literal from a clause and logs the shrink to the proof.
        /// @throws None (noexcept).
        void remove_literal(const cdcl::clause::ref_t ref, const literal redundant_literal) noexcept;

        /// Bloom-style literal-set fingerprint: `left` can only subsume `right` when `left.sig & ~right.sig == 0`.
        static std::uint64_t signature_of(const std::span<const literal> literals) noexcept;

        /// Compressed literal-to-clause occurrence index; avoids per-literal container allocation.
        template <typename Include>
        void build_occurrence_index(occurrence_index& index, Include&& include) noexcept;

        [[nodiscard]] static std::span<const std::uint32_t> occurrences_of(const occurrence_index& index, const literal lit) noexcept;

        [[nodiscard]] std::span<const std::uint32_t> occurrences_of(const literal lit) const noexcept
        {
            return occurrences_of(full_index_, lit);
        }

        /// @brief Deletes `subsumed`, which `subsuming` makes redundant.
        /// @details An original clause may only be dropped in favour of a clause that is itself permanent: a
        /// learned clause can be reduced away later, and the formula would then be weaker than the original.
        /// So a learned clause that subsumes an original one is promoted to irredundant first (CaDiCaL does the
        /// same). Before this rule the pigeonhole and bounded-model-checking instances of the classic set were
        /// answered with models violating a deleted original clause.
        void subsume(indexed_clause& subsumed, indexed_clause& subsuming) noexcept;

        void mark_subsumed(indexed_clause& clause) noexcept;

        bool subsumes_candidate(const cdcl::clause::ref_t other_ref, const cdcl::clause::ref_t candidate_ref,
                                const std::uint32_t candidate_size, const std::span<const literal> candidate_clause) const noexcept;

        static bool clause_subsumes(const std::span<const literal> left, const std::span<const literal> right) noexcept;

        cdcl::clause::database* database_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        std::size_t run_count_ {};
        std::size_t subsumed_count_ {};
        bool self_subsuming_resolution_enabled_ {};
        static constexpr std::size_t max_justifier_size {3u};
        static constexpr std::size_t max_strengthening_occurrences {64u};
        static constexpr std::size_t max_strengthening_clause_count {20000u};
        std::size_t strengthened_count_ {};
        std::function<void(cdcl::clause::ref_t)> clause_sink_ {};
        cdcl::clause::ref_t last_subsumed_ref_ {};
        std::vector<indexed_clause> clauses_ {};
        occurrence_index full_index_ {};
        bool incremental_ {true};
    };

    template <typename Include>
    void forward_subsumer::build_occurrence_index(occurrence_index& index, Include&& include) noexcept
    {
        literal::raw_t highest_raw {};
        std::size_t total_occurrences {};
        for (const auto& clause: clauses_)
        {
            if (!include(clause))
                continue;
            total_occurrences += clause.literals.size();
            for (const auto lit: clause.literals)
                if (lit.raw() > highest_raw)
                    highest_raw = lit.raw();
        }
        const auto literal_slots = (total_occurrences == 0u) ? 0u : static_cast<std::size_t>(highest_raw) + 1u;
        index.start.assign(literal_slots + 1u, 0u);
        index.entries.clear();
        if (literal_slots == 0u)
            return;
        for (const auto& clause: clauses_)
            if (include(clause))
                for (const auto lit: clause.literals)
                    ++index.start[static_cast<std::size_t>(lit.raw()) + 1u];
        for (std::size_t slot {1u}; slot <= literal_slots; ++slot)
            index.start[slot] += index.start[slot - 1u];
        index.entries.resize(total_occurrences);
        index.fill.assign(index.start.begin(), index.start.end() - 1L);
        for (std::uint32_t clause_index {}; clause_index < clauses_.size(); ++clause_index)
            if (include(clauses_[clause_index]))
                for (const auto lit: clauses_[clause_index].literals)
                    index.entries[index.fill[lit.raw()]++] = clause_index;
    }
}
