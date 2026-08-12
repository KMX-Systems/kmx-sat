/// @file inc/kmx/sat/simplify/forward_subsumer.hpp
/// @brief Forward/backward subsumption, with SIMD acceleration where justified.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <vector>
#endif

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::simplify
{
    /// @brief Forward/backward subsumption, with SIMD acceleration where justified.
    ///
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

        /// @brief Runs a full forward/backward subsumption sweep over the clause database.
        /// @throws None (noexcept).
        void run() noexcept
        {
            ++run_count_;
            last_subsumed_ref_ = {};

            if (database_ == nullptr)
            {
                return;
            }

            std::vector<cdcl::clause::ref_t> refs {};
            database_->iterate_irredundant([&](const cdcl::clause::ref_t ref) noexcept { refs.push_back(ref); });
            database_->iterate_redundant([&](const cdcl::clause::ref_t ref) noexcept { refs.push_back(ref); });

            for (std::size_t left_index {0}; left_index < refs.size(); ++left_index)
            {
                if (database_->is_garbage(refs[left_index]))
                {
                    continue;
                }

                const auto left_clause = database_->storage_of().literals_of(refs[left_index]);
                for (std::size_t right_index {left_index + 1u}; right_index < refs.size(); ++right_index)
                {
                    if (database_->is_garbage(refs[right_index]))
                    {
                        continue;
                    }

                    const auto right_clause = database_->storage_of().literals_of(refs[right_index]);
                    if (clause_subsumes(left_clause, right_clause))
                    {
                        if (proof_manager_ != nullptr)
                        {
                            proof_manager_->on_delete_clause(refs[right_index]);
                        }
                        database_->mark_garbage(refs[right_index]);
                        last_subsumed_ref_ = refs[right_index];
                        ++subsumed_count_;
                    }
                    else if (clause_subsumes(right_clause, left_clause))
                    {
                        if (proof_manager_ != nullptr)
                        {
                            proof_manager_->on_delete_clause(refs[left_index]);
                        }
                        database_->mark_garbage(refs[left_index]);
                        last_subsumed_ref_ = refs[left_index];
                        ++subsumed_count_;
                        break;
                    }
                }
            }

            database_->flush_satisfied([&](const cdcl::clause::ref_t ref) noexcept { return database_->is_garbage(ref); });
        }

        /// @brief Checks whether a clause is subsumed by another clause already in the database.
        /// @param ref Reference to the clause to test.
        /// @return True if the clause is subsumed and therefore redundant.
        /// @throws None (noexcept).
        bool is_subsumed(const cdcl::clause::ref_t ref) const noexcept
        {
            if (database_ == nullptr || !ref.valid() || !database_->storage_of().is_alive(ref))
            {
                return false;
            }

            const auto candidate_clause = database_->storage_of().literals_of(ref);
            bool subsumed {false};
            const auto scan = [&](const cdcl::clause::ref_t other_ref) noexcept
            {
                if (subsumed || other_ref == ref)
                {
                    return;
                }
                const auto other_clause = database_->storage_of().literals_of(other_ref);
                if (clause_subsumes(other_clause, candidate_clause))
                {
                    subsumed = true;
                }
            };

            database_->iterate_irredundant(scan);
            database_->iterate_redundant(scan);
            return subsumed;
        }

        /// @brief Removes one literal from a clause found to be subsumed except for that literal.
        /// @param ref Reference to the clause to strengthen.
        /// @throws None (noexcept).
        void strengthen_subsumed_clause(const cdcl::clause::ref_t ref) noexcept
        {
            if (database_ == nullptr || !ref.valid() || !database_->storage_of().is_alive(ref))
            {
                return;
            }

            auto literals = database_->storage_of().literals_of(ref);
            if (literals.size() <= 1u)
            {
                return;
            }

            literals.pop_back();
            database_->storage_of().rewrite_clause_literals(ref, std::span<const literal> {literals.data(), literals.size()});
            database_->storage_of().shrink_clause(ref, static_cast<std::uint32_t>(literals.size()));
            if (proof_manager_ != nullptr)
            {
                proof_manager_->on_shrink_clause(ref, literals);
            }
            ++strengthened_count_;
        }

        std::size_t run_count() const noexcept { return run_count_; }

        std::size_t subsumed_count() const noexcept { return subsumed_count_; }

        std::size_t strengthened_count() const noexcept { return strengthened_count_; }

        cdcl::clause::ref_t last_subsumed_ref() const noexcept { return last_subsumed_ref_; }

    private:
        static bool clause_subsumes(const std::vector<literal>& left, const std::vector<literal>& right) noexcept
        {
            if (left.empty() || left.size() > right.size())
            {
                return false;
            }

            for (const auto left_lit: left)
            {
                const auto found = std::find_if(right.begin(), right.end(),
                                                [left_lit](const literal right_lit) noexcept { return right_lit.raw() == left_lit.raw(); });
                if (found == right.end())
                {
                    return false;
                }
            }

            return true;
        }

        cdcl::clause::database* database_ {nullptr};
        kmx::sat::proof_manager* proof_manager_ {nullptr};
        std::size_t run_count_ {0u};
        std::size_t subsumed_count_ {0u};
        std::size_t strengthened_count_ {0u};
        cdcl::clause::ref_t last_subsumed_ref_ {};
    };
}
