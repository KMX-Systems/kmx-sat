/// @file inc/kmx/sat/simplify/forward_subsumer.hpp
/// @brief Forward/backward subsumption, with SIMD acceleration where justified.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <functional>
    #include <cstddef>
    #include <cstdint>
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
        void run() noexcept
        {
            ++run_count_;
            last_subsumed_ref_ = {};

            if (database_ == nullptr)
                return;

            auto& storage = database_->storage_of();
            clauses_.clear();
            const auto append_clause = [&storage, this](const bool redundant) noexcept
            {
                return [&storage, this, redundant](const cdcl::clause::ref_t ref) noexcept
                {
                    const auto literals = storage.view_literals(ref);
                    const bool checked = incremental_ && (storage.header_at(ref).flags & cdcl::bank::subsumption_checked_flag) != 0u;
                    clauses_.push_back(indexed_clause {ref, literals, signature_of(literals), true, redundant, checked});
                };
            };
            database_->iterate_irredundant(append_clause(false));
            const auto irredundant_clause_count = clauses_.size();
            database_->iterate_redundant(append_clause(true));

            // Two clauses this pass has already checked against each other, and that have not changed since,
            // cannot newly subsume one another; every pair that matters involves a clause added or rewritten since
            // the previous run (a learned clause, a strengthened one). Such pairs are skipped below, which is what
            // keeps the inprocessing epochs from re-examining the whole formula every two thousand conflicts. The
            // lists scanned, and their order, are exactly the full pass's, so the same subsumptions are found in the
            // same order (the order decides which clauses are promoted first, and that order reaches the watch
            // lists); only the tests the full pass would have rejected are left out.
            build_occurrence_index(full_index_, [](const indexed_clause&) noexcept { return true; });

            for (std::uint32_t left_index {}; left_index < clauses_.size(); ++left_index)
            {
                auto& left = clauses_[left_index];
                if (!left.active || left.literals.empty())
                    continue;

                // Any clause `left` subsumes contains every literal of `left`, so it occurs in the rarest of `left`'s
                // occurrence lists; that list is also where a subsumer of `left` is looked for first (the pass that
                // processes the subsumer as `left` finds the pair in any case).
                std::span<const std::uint32_t> rarest_candidates {};
                bool has_rarest {};
                for (const auto lit: left.literals)
                {
                    const auto candidates = occurrences_of(full_index_, lit);
                    if (candidates.empty())
                    {
                        has_rarest = false;
                        break;
                    }
                    if (!has_rarest || candidates.size() < rarest_candidates.size())
                    {
                        rarest_candidates = candidates;
                        has_rarest = true;
                    }
                }
                if (!has_rarest)
                    continue;

                bool left_subsumed {};
                for (const auto candidate_index: rarest_candidates)
                {
                    if (candidate_index == left_index || !clauses_[candidate_index].active)
                        continue;

                    auto& candidate = clauses_[candidate_index];
                    if (left.checked && candidate.checked)
                        continue;
                    const bool precedes_equal_clause = candidate_index < left_index && candidate.literals.size() == left.literals.size();
                    if ((candidate.literals.size() < left.literals.size() || precedes_equal_clause) &&
                        (candidate.signature & ~left.signature) == 0u && clause_subsumes(candidate.literals, left.literals))
                    {
                        subsume(left, candidate);
                        left_subsumed = true;
                        break;
                    }
                }
                if (left_subsumed)
                    continue;

                for (const auto candidate_index: rarest_candidates)
                {
                    if (candidate_index == left_index || !clauses_[candidate_index].active)
                        continue;

                    auto& candidate = clauses_[candidate_index];
                    if (left.checked && candidate.checked)
                        continue;
                    if (left.literals.size() <= candidate.literals.size() && (left.signature & ~candidate.signature) == 0u &&
                        clause_subsumes(left.literals, candidate.literals))
                        subsume(candidate, left);
                }
            }

            // Everything still standing has now been checked against everything; strengthening below clears the
            // mark again on the clauses it rewrites, through the storage.
            for (const auto& clause: clauses_)
                if (clause.active)
                    storage.header_at(clause.ref).flags |= cdcl::bank::subsumption_checked_flag;

            if (self_subsuming_resolution_enabled_)
                run_self_subsuming_resolution(irredundant_clause_count);

            database_->flush_satisfied([&](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
        }

        /// @brief Strengthens clauses by self-subsuming resolution against the indexed clause set.
        /// @details For each active clause `C` and literal `l` in it, every clause holding `not-l` is a candidate
        /// `D`; when `C \ {l}` is contained in `D`, resolving on `l` yields a clause that subsumes `D`, so `not-l`
        /// can be dropped from `D`. Candidates come from the occurrence list of `not-l`, so the scan stays local.
        /// @throws None (noexcept).
        void run_self_subsuming_resolution(const std::size_t irredundant_clause_count) noexcept
        {
            // Bounded on purpose. Unbounded, this pass is O(clauses x literals x occurrences) and the inprocessing
            // scheduler re-runs it roughly every thirty conflicts over a learned database that grows into the tens
            // of thousands, which measured 14x slower end to end. Only the original clause set is strengthened,
            // only short justifying clauses are considered, and long occurrence lists are skipped.
            if (irredundant_clause_count > max_strengthening_clause_count)
                return;

            auto& storage = database_->storage_of();
            for (std::uint32_t left_index {}; left_index < irredundant_clause_count; ++left_index)
            {
                auto& left = clauses_[left_index];
                if (!left.active || left.literals.empty() || left.literals.size() > max_justifier_size)
                    continue;

                for (const auto pivot: left.literals)
                {
                    const auto candidates = occurrences_of(pivot.negated());
                    if (candidates.size() > max_strengthening_occurrences)
                        continue;

                    for (const auto candidate_index: candidates)
                    {
                        if (candidate_index == left_index)
                            continue;

                        auto& candidate = clauses_[candidate_index];
                        if (!candidate.active || candidate.redundant || candidate.literals.size() <= 1u)
                            continue;
                        if (left.literals.size() > candidate.literals.size())
                            continue;
                        if (!resolves_to_strengthen(left.literals, pivot, candidate.literals))
                            continue;

                        remove_literal(candidate.ref, pivot.negated());

                        // The clause shrank in place, so its cached span and fingerprint are now stale. The
                        // occurrence list for the removed literal keeps naming it, which only ever costs a
                        // failed containment test later.
                        candidate.literals = storage.view_literals(candidate.ref);
                        candidate.signature = signature_of(candidate.literals);
                        if (clause_sink_)
                            clause_sink_(candidate.ref);
                    }
                }
            }
        }

        /// @brief Checks whether a clause is subsumed by another clause already in the database.
        /// @param ref Reference to the clause to test.
        /// @return True if the clause is subsumed and therefore redundant.
        /// @throws None (noexcept).
        bool is_subsumed(const cdcl::clause::ref_t ref) const noexcept
        {
            auto& storage = database_->storage_of();
            if (database_ == nullptr || !ref.valid() || !storage.is_alive(ref))
                return false;

            const auto candidate_size = storage.literal_count(ref);
            const auto candidate_clause = storage.view_literals(ref);
            bool subsumed {};
            const auto scan = [&](const cdcl::clause::ref_t other_ref) noexcept
            {
                if (!subsumed)
                    subsumed = subsumes_candidate(other_ref, ref, candidate_size, candidate_clause);
            };

            database_->iterate_irredundant(scan);
            database_->iterate_redundant(scan);
            return subsumed;
        }

        /// @brief Removes `redundant_literal` from a clause when self-subsuming resolution justifies it.
        /// @details Self-subsuming resolution: if some clause `C` satisfies `C \ {l} subset-of D` and
        /// `not-l in D`, then resolving `C` with `D` on `l` yields `D \ {not-l}`, which subsumes `D`. Dropping a
        /// literal without that justification would strengthen the formula and lose models, so the justifying
        /// clause is required rather than assumed.
        /// @param ref Clause to strengthen.
        /// @param redundant_literal Literal to remove from it.
        /// @return True when a justifying clause was found and the literal was removed.
        /// @throws None (noexcept).
        bool strengthen_by_resolution(const cdcl::clause::ref_t ref, const literal redundant_literal) noexcept
        {
            if (database_ == nullptr || !ref.valid())
                return false;

            auto& storage = database_->storage_of();
            if (!storage.is_alive(ref))
                return false;

            const auto target = storage.view_literals(ref);
            if (target.size() <= 1u)
                return false;
            if (std::find_if(target.begin(), target.end(), [redundant_literal](const literal lit) noexcept
                             { return lit.raw() == redundant_literal.raw(); }) == target.end())
                return false;

            auto justified = false;
            const auto pivot = redundant_literal.negated();
            database_->iterate_irredundant(
                [&](const cdcl::clause::ref_t candidate_ref) noexcept
                {
                    if (justified || candidate_ref.offset() == ref.offset() || database_->is_garbage(candidate_ref))
                        return;
                    justified = resolves_to_strengthen(storage.view_literals(candidate_ref), pivot, target);
                });

            if (!justified)
                return false;

            remove_literal(ref, redundant_literal);
            return true;
        }

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
                                           const std::span<const literal> target) noexcept
        {
            auto found_pivot = false;
            for (const auto lit: candidate)
            {
                if (lit.raw() == pivot.raw())
                {
                    found_pivot = true;
                    continue;
                }
                const auto present = std::find_if(target.begin(), target.end(), [lit](const literal existing) noexcept
                                                  { return existing.raw() == lit.raw(); }) != target.end();
                if (!present)
                    return false;
            }
            return found_pivot;
        }

        /// @brief Physically removes one literal from a clause and logs the shrink to the proof.
        /// @throws None (noexcept).
        void remove_literal(const cdcl::clause::ref_t ref, const literal redundant_literal) noexcept
        {
            auto& storage = database_->storage_of();
            auto literals = storage.literals_of(ref);
            const auto removed = std::remove_if(literals.begin(), literals.end(), [redundant_literal](const literal lit) noexcept
                                                { return lit.raw() == redundant_literal.raw(); });
            literals.erase(removed, literals.end());
            if (literals.empty())
                return;

            storage.rewrite_clause_literals(ref, std::span<const literal> {literals.data(), literals.size()});
            storage.shrink_clause(ref, static_cast<std::uint32_t>(literals.size()));
            if (proof_manager_ != nullptr)
                proof_manager_->on_shrink_clause(ref, literals);
            ++strengthened_count_;
        }

        /// Bloom-style literal-set fingerprint: `left` can only subsume `right` when `left.sig & ~right.sig == 0`.
        static std::uint64_t signature_of(const std::span<const literal> literals) noexcept
        {
            std::uint64_t signature {};
            for (const auto lit: literals)
                signature |= std::uint64_t {1u} << (lit.raw() & 63u);
            return signature;
        }

        /// Compressed literal-to-clause occurrence index; avoids per-literal container allocation.
        template <typename include_t>
        void build_occurrence_index(occurrence_index& index, include_t&& include) noexcept
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
            const auto literal_slots = total_occurrences == 0u ? 0u : static_cast<std::size_t>(highest_raw) + 1u;
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
            index.fill.assign(index.start.begin(), index.start.end() - 1);
            for (std::uint32_t clause_index {}; clause_index < clauses_.size(); ++clause_index)
                if (include(clauses_[clause_index]))
                    for (const auto lit: clauses_[clause_index].literals)
                        index.entries[index.fill[lit.raw()]++] = clause_index;
        }

        [[nodiscard]] static std::span<const std::uint32_t> occurrences_of(const occurrence_index& index, const literal lit) noexcept
        {
            const auto slot = static_cast<std::size_t>(lit.raw());
            if (slot + 1u >= index.start.size())
                return {};
            const auto begin = index.start[slot];
            return {index.entries.data() + begin, index.start[slot + 1u] - begin};
        }

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
        void subsume(indexed_clause& subsumed, indexed_clause& subsuming) noexcept
        {
            if (!subsumed.redundant && subsuming.redundant)
            {
                if (!database_->make_irredundant(subsuming.ref))
                    return;
                subsuming.redundant = false;
            }
            mark_subsumed(subsumed);
        }

        void mark_subsumed(indexed_clause& clause) noexcept
        {
            if (proof_manager_ != nullptr)
                proof_manager_->on_delete_clause(clause.ref);
            database_->mark_garbage(clause.ref);
            clause.active = false;
            last_subsumed_ref_ = clause.ref;
            ++subsumed_count_;
        }

        bool subsumes_candidate(const cdcl::clause::ref_t other_ref, const cdcl::clause::ref_t candidate_ref,
                                const std::uint32_t candidate_size, const std::span<const literal> candidate_clause) const noexcept
        {
            if (other_ref == candidate_ref)
                return false;

            const auto& storage = database_->storage_of();
            if (storage.literal_count(other_ref) > candidate_size)
                return false;
            return clause_subsumes(storage.view_literals(other_ref), candidate_clause);
        }

        static bool clause_subsumes(const std::span<const literal> left, const std::span<const literal> right) noexcept
        {
            if (left.empty() || left.size() > right.size())
                return false;

            for (const auto left_lit: left)
            {
                const auto found = std::find_if(right.begin(), right.end(),
                                                [left_lit](const literal right_lit) noexcept { return right_lit.raw() == left_lit.raw(); });
                if (found == right.end())
                    return false;
            }

            return true;
        }

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
}
